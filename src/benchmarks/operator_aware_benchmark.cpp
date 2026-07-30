// =============================================================================
// Operator-Aware Buffer Management -- Comparative Benchmark
// Paper: "Operator-Aware Buffer Management: Gestión de Buffer Consciente del
//         Plan de Consulta en un Motor de Base de Datos Relacional No Distribuido"
//
// Policies compared:
//   1. CLOCK_SWEEP   -- Classic Clock-Sweep (baseline)
//   2. TWO_Q         -- Two-queue algorithm (Johnson & Shasha, VLDB 1994)
//   3. OPERATOR_AWARE-- Hint-driven 4-tier Clock-Sweep (this work)
//
// Workloads:
//   W1. Nested Loop Join  -- repeated rescans on inner table (primary result)
//   W2. Hash Join         -- build-side hot data + probe sweep
//   W3. Concurrent Scan + Join -- cache-pollution resistance
//
// Dataset calibration (Opción A):
//   W1: outer=120 pages, inner=40 pages → NLJ rescans inner 600 times
//       Buffer pool 10-80% of 160 pages → stress at 10-25%
//   W2: left=80, right=80 pages (total=160)
//   W3: scan=100 + left=60 + right=60 pages (total=220)
//
// Output: human-readable table + CSV file (benchmark_results.csv)
// =============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <unistd.h>
#include <cstdio>

#include "catalog/catalog.hpp"
#include "storage/record/schema.hpp"
#include "storage/record/tuple_builder.hpp"
#include "storage/page/buffer_pool_manager.hpp"
#include "execution/plan_node.hpp"
#include "execution/executor.hpp"
#include "optimizer/optimizer.hpp"

using namespace megatron;
using namespace megatron::execution;
using namespace megatron::optimizer;

// ---------------------------------------------------------------------------
// Result types
// ---------------------------------------------------------------------------

struct RunMetrics {
    std::string       workload;
    ReplacementPolicy policy;
    double            buffer_capacity_pct;
    size_t            pool_size_pages;
    double            miss_ratio_pct;
    size_t            disk_io_count;  // misses + writes (total)
    size_t            disk_misses;    // Fix #4: page misses only
    size_t            disk_writes;    // Fix #4: dirty evictions only
    double            latency_ms;
};

static const char* PolicyName(ReplacementPolicy p) {
    switch (p) {
        case ReplacementPolicy::CLOCK_SWEEP:    return "LRU/Clock-Sweep";
        case ReplacementPolicy::TWO_Q:          return "2Q";
        case ReplacementPolicy::OPERATOR_AWARE: return "Operator-Aware";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Schema helpers
// ---------------------------------------------------------------------------

static Schema BuildTestSchema() {
    Schema schema;
    schema.AddColumn("id",      TypeId::INTEGER);
    schema.AddColumn("val",     TypeId::INTEGER);
    schema.AddColumn("payload", TypeId::VARCHAR);
    return schema;
}

static Schema BuildJoinSchema(const Schema& left, const Schema& right) {
    Schema joined;
    for (const auto& col : left.columns)  joined.columns.push_back(col);
    for (const auto& col : right.columns) joined.columns.push_back(col);
    return joined;
}

// ---------------------------------------------------------------------------
// Synthetic table generator
// ---------------------------------------------------------------------------

static void GenerateSyntheticTable(const std::string& table_name,
                                   uint32_t target_pages,
                                   const Schema& schema,
                                   int tuples_per_page = 5) {
    std::string filename = table_name + ".bd";
    unlink(filename.c_str());

    GlobalBufferPoolManager bpm(target_pages + 30, ReplacementPolicy::OPERATOR_AWARE);
    bpm.ClearTablePages(table_name);

    for (uint32_t p = 0; p < target_pages; ++p) {
        uint32_t pid;
        SlottedPage* page = bpm.NewPage(table_name, &pid);
        if (!page) break;

        for (int tuple_idx = 0; tuple_idx < tuples_per_page; ++tuple_idx) {
            TupleBuilder builder(&schema);
            int id_val = static_cast<int>(p * tuples_per_page + tuple_idx);
            builder.SetInt("id",      id_val);
            builder.SetInt("val",     id_val % 100);
            builder.SetVarchar("payload",
                "synthetic_payload_data_str_" + std::to_string(id_val));
            if (!page->InsertTuple(builder.GetData(), builder.GetSize())) break;
        }
        bpm.UnpinPage(table_name, pid, true);
    }
    bpm.FlushAllPages();

    std::cout << "  [DataGen] '" << table_name << "' -> "
              << bpm.GetNumPages(table_name) << " pages on disk.\n";
}

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------

static void PrintSeparator() {
    std::cout << "+------------------+-----------------+------------------+------------------+------------------+-------------------+\n";
}

static void PrintHeader() {
    PrintSeparator();
    std::cout << "| Buffer Pool %    | Policy          | Miss Ratio (%)   | Page Misses      | Disk Writes      | Latency (ms)      |\n";
    PrintSeparator();
}

static void PrintRow(const RunMetrics& r, bool last_of_group) {
    std::cout << "| "
              << std::setw(16) << std::left
              << (std::to_string(static_cast<int>(r.buffer_capacity_pct)) + "% (" +
                  std::to_string(r.pool_size_pages) + " pgs)")
              << " | "
              << std::setw(15) << std::left  << PolicyName(r.policy)
              << " | "
              << std::setw(14) << std::right << std::fixed << std::setprecision(2)
              << r.miss_ratio_pct << "%"
              << " | "
              << std::setw(16) << std::right << r.disk_misses
              << " | "
              << std::setw(16) << std::right << r.disk_writes
              << " | "
              << std::setw(14) << std::right << std::fixed << std::setprecision(2)
              << r.latency_ms << " ms"
              << " |\n";
    if (last_of_group) PrintSeparator();
}

static void PrintWorkloadTable(const std::string& title,
                               const std::vector<RunMetrics>& results,
                               size_t policies_count) {
    std::cout << "\n";
    std::cout << "=====================================================================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "=====================================================================================================\n";
    PrintHeader();

    for (size_t i = 0; i < results.size(); ++i) {
        bool last_of_group = ((i + 1) % policies_count == 0);
        PrintRow(results[i], last_of_group);
    }
}

// ---------------------------------------------------------------------------
// CSV export
// ---------------------------------------------------------------------------
static void ExportCSV(const std::string& filename,
                      const std::vector<RunMetrics>& all_results) {
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "[CSV] ERROR: cannot open " << filename << "\n";
        return;
    }
    // Fix #4: split disk_io_count into misses + writes for clearer comparison
    f << "workload,policy,buffer_pct,pool_pages,miss_ratio_pct,page_misses,disk_writes,disk_io_total,latency_ms\n";
    for (const auto& r : all_results) {
        f << r.workload << ","
          << PolicyName(r.policy) << ","
          << static_cast<int>(r.buffer_capacity_pct) << ","
          << r.pool_size_pages << ","
          << std::fixed << std::setprecision(4) << r.miss_ratio_pct << ","
          << r.disk_misses << ","
          << r.disk_writes << ","
          << r.disk_io_count << ","
          << std::fixed << std::setprecision(4) << r.latency_ms << "\n";
    }
    std::cout << "\n[CSV] Results written to: " << filename << "\n";
}

// ---------------------------------------------------------------------------
// W1: Nested Loop Join (Opción A — primary paper result)
//   outer=bench_nlj_outer (120 pgs), inner=bench_nlj_inner (40 pgs)
//   NLJ calls right_child_->Init() on every outer advance → pages re-fetched
//   from BPM on each inner rescan. KEEP_HOT on inner keeps it in pool.
// ---------------------------------------------------------------------------
static RunMetrics RunNestedLoopJoin(ReplacementPolicy policy,
                                    double cap_pct, size_t pool_size,
                                    const Schema& schema) {
    GlobalBufferPoolManager bpm(pool_size, policy);
    bpm.ResetMetrics();

    auto left_scan  = std::make_shared<SeqScanPlanNode>(
        schema, "bench_nlj_outer", nullptr, BufferHint::DEFAULT);
    auto right_scan = std::make_shared<SeqScanPlanNode>(
        schema, "bench_nlj_inner", nullptr, BufferHint::DEFAULT);

    // No predicate → full cross product → maximizes inner rescan count
    auto nlj_plan = std::make_shared<NestedLoopJoinPlanNode>(
        BuildJoinSchema(schema, schema), left_scan, right_scan, nullptr, BufferHint::DEFAULT);

    Catalog  catalog;
    Optimizer optimizer(catalog);
    std::shared_ptr<AbstractPlanNode> plan_root = nlj_plan;
    optimizer.InjectBufferHints(plan_root);  // injects DISCARD_QUICKLY(outer), KEEP_HOT(inner)

    auto left_exec  = std::make_unique<SeqScanExecutor>(
        static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(0)),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    auto right_exec = std::make_unique<SeqScanExecutor>(
        static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(1)),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);

    NestedLoopJoinExecutor nlj_exec(
        nlj_plan.get(), std::move(left_exec), std::move(right_exec));

    auto start = std::chrono::high_resolution_clock::now();
    nlj_exec.Init();
    Tuple t; RID r;
    while (nlj_exec.Next(&t, &r)) {}
    auto end = std::chrono::high_resolution_clock::now();

    return { "W1_NLJ", policy, cap_pct, pool_size,
             bpm.GetMissRatio() * 100.0,
             bpm.GetDiskIOCount(),
             bpm.GetPageMisses(),   // Fix #4
             bpm.GetDiskWrites(),   // Fix #4
             std::chrono::duration<double, std::milli>(end - start).count() };
}

// ---------------------------------------------------------------------------
// W2: Hash Join workload
// ---------------------------------------------------------------------------
static RunMetrics RunHashJoin(ReplacementPolicy policy,
                              double cap_pct, size_t pool_size,
                              const Schema& schema) {
    GlobalBufferPoolManager bpm(pool_size, policy);
    bpm.ResetMetrics();

    auto left_scan  = std::make_shared<SeqScanPlanNode>(
        schema, "bench_hj_left",  nullptr, BufferHint::DEFAULT);
    auto right_scan = std::make_shared<SeqScanPlanNode>(
        schema, "bench_hj_right", nullptr, BufferHint::DEFAULT);

    auto hj_plan = std::make_shared<HashJoinPlanNode>(
        BuildJoinSchema(schema, schema), left_scan, right_scan, 0, 0, BufferHint::DEFAULT);

    Catalog   catalog;
    Optimizer optimizer(catalog);
    std::shared_ptr<AbstractPlanNode> plan_root = hj_plan;
    optimizer.InjectBufferHints(plan_root);

    auto left_exec  = std::make_unique<SeqScanExecutor>(
        static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(0)),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    auto right_exec = std::make_unique<SeqScanExecutor>(
        static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(1)),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);

    HashJoinExecutor hj_exec(hj_plan.get(), std::move(left_exec), std::move(right_exec));

    auto start = std::chrono::high_resolution_clock::now();
    hj_exec.Init();
    Tuple t; RID r;
    while (hj_exec.Next(&t, &r)) {}
    auto end = std::chrono::high_resolution_clock::now();

    return { "W2_HJ", policy, cap_pct, pool_size,
             bpm.GetMissRatio() * 100.0,
             bpm.GetDiskIOCount(),
             bpm.GetPageMisses(),   // Fix #4
             bpm.GetDiskWrites(),   // Fix #4
             std::chrono::duration<double, std::milli>(end - start).count() };
}

// ---------------------------------------------------------------------------
// W3: Concurrent SeqScan + Join workload
// ---------------------------------------------------------------------------
static RunMetrics RunConcurrentScanJoin(ReplacementPolicy policy,
                                        double cap_pct, size_t pool_size,
                                        const Schema& schema) {
    GlobalBufferPoolManager bpm(pool_size, policy);
    bpm.ResetMetrics();

    Catalog   catalog;
    Optimizer optimizer(catalog);

    auto standalone_plan = std::make_shared<SeqScanPlanNode>(
        schema, "bench_scan", nullptr, BufferHint::DEFAULT);
    std::shared_ptr<AbstractPlanNode> scan_root = standalone_plan;
    optimizer.InjectBufferHints(scan_root);

    auto hj_left  = std::make_shared<SeqScanPlanNode>(
        schema, "bench_hj_left",  nullptr, BufferHint::DEFAULT);
    auto hj_right = std::make_shared<SeqScanPlanNode>(
        schema, "bench_hj_right", nullptr, BufferHint::DEFAULT);
    auto hj_plan  = std::make_shared<HashJoinPlanNode>(
        BuildJoinSchema(schema, schema), hj_left, hj_right, 0, 0, BufferHint::DEFAULT);
    std::shared_ptr<AbstractPlanNode> join_root = hj_plan;
    optimizer.InjectBufferHints(join_root);

    auto start = std::chrono::high_resolution_clock::now();

    // 1. Pollute cache with standalone scan
    SeqScanExecutor scan1(
        static_cast<const SeqScanPlanNode*>(scan_root.get()),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    scan1.Init();
    Tuple st; RID sr;
    while (scan1.Next(&st, &sr)) {}

    // 2. Hash Join (build side should survive in OPERATOR_AWARE mode)
    auto left_exec  = std::make_unique<SeqScanExecutor>(
        static_cast<const SeqScanPlanNode*>(join_root->GetChildAt(0)),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    auto right_exec = std::make_unique<SeqScanExecutor>(
        static_cast<const SeqScanPlanNode*>(join_root->GetChildAt(1)),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    HashJoinExecutor hj_exec(hj_plan.get(), std::move(left_exec), std::move(right_exec));
    hj_exec.Init();
    Tuple jt; RID jr;
    while (hj_exec.Next(&jt, &jr)) {}

    // 3. Second scan — re-pollution stress
    SeqScanExecutor scan2(
        static_cast<const SeqScanPlanNode*>(scan_root.get()),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    scan2.Init();
    while (scan2.Next(&st, &sr)) {}

    auto end = std::chrono::high_resolution_clock::now();

    return { "W3_CONC", policy, cap_pct, pool_size,
             bpm.GetMissRatio() * 100.0,
             bpm.GetDiskIOCount(),
             bpm.GetPageMisses(),   // Fix #4
             bpm.GetDiskWrites(),   // Fix #4
             std::chrono::duration<double, std::milli>(end - start).count() };
}

// ---------------------------------------------------------------------------
// Fix #5: W4 — Tight NLJ: pool = inner_size + 2
// This workload creates maximum real competition between hints:
//   - inner table = NLJ_INNER_PAGES
//   - outer table = very large (200 pages)
//   - pool = inner + 2  → exactly 2 frames free for the outer sweep
// With KEEP_HOT on the inner, the OA policy should protect all inner pages;
// baseline policies evict them on every outer advance → 100% inner miss.
// ---------------------------------------------------------------------------
static RunMetrics RunTightNLJ(ReplacementPolicy policy,
                              size_t inner_pages,
                              const Schema& schema) {
    // pool = inner_pages + 2: two frames for the outer sweep, rest for inner
    const size_t pool_size = inner_pages + 2;
    const double cap_pct   = static_cast<double>(pool_size) /
                             static_cast<double>(inner_pages + 200) * 100.0;

    GlobalBufferPoolManager bpm(pool_size, policy);
    bpm.ResetMetrics();

    auto left_scan  = std::make_shared<SeqScanPlanNode>(
        schema, "bench_nlj_tight_outer", nullptr, BufferHint::DEFAULT);
    auto right_scan = std::make_shared<SeqScanPlanNode>(
        schema, "bench_nlj_inner",       nullptr, BufferHint::DEFAULT);

    auto nlj_plan = std::make_shared<NestedLoopJoinPlanNode>(
        BuildJoinSchema(schema, schema), left_scan, right_scan, nullptr, BufferHint::DEFAULT);

    Catalog  catalog;
    Optimizer optimizer(catalog);
    std::shared_ptr<AbstractPlanNode> plan_root = nlj_plan;
    optimizer.InjectBufferHints(plan_root);  // outer=DISCARD_QUICKLY, inner=KEEP_HOT

    auto left_exec  = std::make_unique<SeqScanExecutor>(
        static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(0)),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    auto right_exec = std::make_unique<SeqScanExecutor>(
        static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(1)),
        nullptr, std::vector<Tuple>{}, nullptr, &bpm);

    NestedLoopJoinExecutor nlj_exec(
        nlj_plan.get(), std::move(left_exec), std::move(right_exec));

    auto start = std::chrono::high_resolution_clock::now();
    nlj_exec.Init();
    Tuple t; RID r;
    while (nlj_exec.Next(&t, &r)) {}
    auto end = std::chrono::high_resolution_clock::now();

    return { "W4_TIGHT_NLJ", policy, cap_pct, pool_size,
             bpm.GetMissRatio() * 100.0,
             bpm.GetDiskIOCount(),
             bpm.GetPageMisses(),
             bpm.GetDiskWrites(),
             std::chrono::duration<double, std::milli>(end - start).count() };
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "\n=====================================================================================================\n";
    std::cout << "         OPERATOR-AWARE BUFFER MANAGEMENT -- COMPARATIVE BENCHMARK\n";
    std::cout << "         Policies: LRU/Clock-Sweep | 2Q (Johnson&Shasha 1994) | Operator-Aware\n";
    std::cout << "         Metrics:  Buffer Miss Ratio | Disk I/O Count | Execution Time\n";
    std::cout << "=====================================================================================================\n";

    Schema schema = BuildTestSchema();

    // -------------------------------------------------------------------------
    // Dataset calibration for observable KEEP_HOT benefit (Opción A):
    //
    // W1 — NLJ:
    //   inner = 12 pages (small, should survive in pool with KEEP_HOT)
    //   outer = 100 pages (large, swept with DISCARD_QUICKLY)
    //   Pool at 10% of total (112 pgs) = ~11 pages → inner DOESN'T fit → 100% on inner
    //   Pool at 15% = ~17 pages → inner (12) FITS with KEEP_HOT but not with baselines
    //   Pool at 25% = ~28 pages → inner clearly fits in both → gap narrows
    //   Pool at 50%/80% → all fit → policies converge
    //
    //   NLJ rescans inner once per outer tuple → 100*5=500 rescans.
    //   Total re-fetches = 500 * 12 = 6000 (KEEP_HOT prevents most; baselines don't)
    //
    // W2 — HJ: left=80, right=80 (single-pass build+probe; hints affect eviction priority)
    // W3 — Concurrent: scan=100 + left=60 + right=60 (pollution test)
    // -------------------------------------------------------------------------
    const uint32_t NLJ_OUTER_PAGES = 100;
    const uint32_t NLJ_INNER_PAGES = 12;   // small inner: fits in pool at ≥15%
    const uint32_t HJ_LEFT_PAGES   = 80;
    const uint32_t HJ_RIGHT_PAGES  = 80;
    const uint32_t SCAN_PAGES      = 100;

    // -- Data generation ------------------------------------------------------
    std::cout << "\n[Step 1/5] Generating synthetic tables...\n";
    GenerateSyntheticTable("bench_nlj_outer",       NLJ_OUTER_PAGES, schema);
    GenerateSyntheticTable("bench_nlj_inner",       NLJ_INNER_PAGES, schema);
    GenerateSyntheticTable("bench_nlj_tight_outer", 200,             schema); // Fix #5
    GenerateSyntheticTable("bench_hj_left",         HJ_LEFT_PAGES,   schema);
    GenerateSyntheticTable("bench_hj_right",        HJ_RIGHT_PAGES,  schema);
    GenerateSyntheticTable("bench_scan",            SCAN_PAGES,      schema);
    std::cout << "  W1 NLJ: outer=" << NLJ_OUTER_PAGES << " pgs, inner=" << NLJ_INNER_PAGES << " pgs\n";
    std::cout << "  W2 HJ:  left="  << HJ_LEFT_PAGES   << " pgs, right=" << HJ_RIGHT_PAGES  << " pgs\n";
    std::cout << "  W3:     scan="  << SCAN_PAGES       << " pgs + HJ tables above\n";
    std::cout << "  W4 Tight NLJ: outer=200 pgs, inner=" << NLJ_INNER_PAGES
              << " pgs, pool=" << (NLJ_INNER_PAGES + 2) << " pgs (inner+2)\n";

    // W1 uses a finer-grained capacity sweep to capture the transition zone
    // where KEEP_HOT begins to help (inner_pages < pool < inner+outer pages).
    // Pool sizes in absolute pages (NLJ_TOTAL = outer+inner = 112 pages, transition zone below):
    //   8  pages (~7%)  : inner(12) > pool → full miss on inner
    //   12 pages (~11%) : pool == inner     → boundary
    //   13 pages (~12%) : Fix #2 — one free frame above inner boundary
    //   14 pages (~13%) : Fix #2 — two free frames: KEEP_HOT should start winning
    //   16 pages (~14%) : pool > inner      → KEEP_HOT should help
    //   22 pages (~20%) : pool clearly fits inner
    //   28 pages (~25%) : comfortable fit
    //   56 pages (~50%) : large pool
    //   90 pages (~80%) : almost everything fits
    const std::vector<std::pair<double,size_t>> NLJ_POOL_CONFIGS = {
        { 7.1,  8},   //  7% — pool < inner → miss on every rescan
        {10.7, 12},   // 11% — pool ≈ inner boundary
        {11.6, 13},   // 12% — Fix #2: one free frame; KEEP_HOT starts shielding inner
        {12.5, 14},   // 13% — Fix #2: two free frames; gap should be measurable
        {14.3, 16},   // 14% — pool > inner: KEEP_HOT wins
        {19.6, 22},   // 20% — pool > inner: gap present
        {25.0, 28},   // 25% — comfortable fit
        {50.0, 56},   // 50%
        {80.4, 90},   // 80%
    };

    const std::vector<double> CAPACITIES_PCT = {10.0, 25.0, 50.0, 80.0};
    const std::vector<ReplacementPolicy> POLICIES = {
        ReplacementPolicy::CLOCK_SWEEP,
        ReplacementPolicy::TWO_Q,
        ReplacementPolicy::OPERATOR_AWARE
    };

    std::vector<RunMetrics> all_results;

    // -------------------------------------------------------------------------
    // W1: Nested Loop Join — finer-grained sweep
    // -------------------------------------------------------------------------
    std::cout << "\n[Step 2/4] Running W1: Nested Loop Join (Opcion A — primary result)...\n";
    std::cout << "  inner=" << NLJ_INNER_PAGES << " pgs (KEEP_HOT), outer="
              << NLJ_OUTER_PAGES << " pgs (DISCARD_QUICKLY)\n";
    std::cout << "  " << (NLJ_OUTER_PAGES * 5) << " inner rescans total\n";
    std::vector<RunMetrics> nlj_results;
    for (auto [pct, pool_size] : NLJ_POOL_CONFIGS) {
        for (auto pol : POLICIES) {
            auto r = RunNestedLoopJoin(pol, pct, pool_size, schema);
            nlj_results.push_back(r);
            all_results.push_back(r);
        }
    }
    PrintWorkloadTable(
        "W1: NESTED LOOP JOIN  (Operator-Aware: outer=DISCARD_QUICKLY, inner=KEEP_HOT)",
        nlj_results, POLICIES.size());

    // -------------------------------------------------------------------------
    // W2: Hash Join
    // -------------------------------------------------------------------------
    std::cout << "\n[Step 3/4] Running W2: Hash Join...\n";
    const uint32_t HJ_TOTAL = HJ_LEFT_PAGES + HJ_RIGHT_PAGES;
    std::vector<RunMetrics> hj_results;
    for (double pct : CAPACITIES_PCT) {
        size_t pool_size = std::max<size_t>(
            4, static_cast<size_t>(std::round(HJ_TOTAL * (pct / 100.0))));
        for (auto pol : POLICIES) {
            auto r = RunHashJoin(pol, pct, pool_size, schema);
            hj_results.push_back(r);
            all_results.push_back(r);
        }
    }
    PrintWorkloadTable(
        "W2: HASH JOIN  (Operator-Aware: build=KEEP_HOT, probe=DISCARD_QUICKLY)",
        hj_results, POLICIES.size());

    // -------------------------------------------------------------------------
    // W3: Concurrent SeqScan + Join
    // -------------------------------------------------------------------------
    std::cout << "\n[Step 4/4] Running W3: Concurrent SeqScan + Join...\n";
    const uint32_t CONC_TOTAL = SCAN_PAGES + HJ_LEFT_PAGES + HJ_RIGHT_PAGES;
    std::vector<RunMetrics> conc_results;
    for (double pct : CAPACITIES_PCT) {
        size_t pool_size = std::max<size_t>(
            4, static_cast<size_t>(std::round(CONC_TOTAL * (pct / 100.0))));
        for (auto pol : POLICIES) {
            auto r = RunConcurrentScanJoin(pol, pct, pool_size, schema);
            conc_results.push_back(r);
            all_results.push_back(r);
        }
    }
    PrintWorkloadTable(
        "W3: CONCURRENT SEQSCAN + JOIN  (Operator-Aware: scan=DISCARD_QUICKLY, join-build=KEEP_HOT)",
        conc_results, POLICIES.size());

    // -------------------------------------------------------------------------
    // Fix #5: W4 — Tight NLJ (pool = inner_size + 2)
    // -------------------------------------------------------------------------
    std::cout << "\n[Step 5/5] Running W4: Tight NLJ (Fix #5 — pool=inner+2 frames)...\n";
    std::cout << "  inner=" << NLJ_INNER_PAGES << " pgs (KEEP_HOT), outer=200 pgs (DISCARD_QUICKLY)\n";
    std::cout << "  pool=" << (NLJ_INNER_PAGES + 2) << " pgs: 2 free frames for outer sweep\n";
    std::vector<RunMetrics> tight_results;
    for (auto pol : POLICIES) {
        auto r = RunTightNLJ(pol, NLJ_INNER_PAGES, schema);
        tight_results.push_back(r);
        all_results.push_back(r);
    }
    PrintWorkloadTable(
        "W4: TIGHT NLJ  (pool=inner+2: maximum KEEP_HOT vs baseline competition)",
        tight_results, POLICIES.size());

    // -- CSV export -----------------------------------------------------------
    ExportCSV("benchmark_results.csv", all_results);

    // -- Cleanup --------------------------------------------------------------
    unlink("bench_nlj_outer.bd");
    unlink("bench_nlj_inner.bd");
    unlink("bench_nlj_tight_outer.bd"); // Fix #5
    unlink("bench_hj_left.bd");
    unlink("bench_hj_right.bd");
    unlink("bench_scan.bd");
    GlobalBufferPoolManager::ResetGlobalState();

    std::cout << "\n=====================================================================================================\n";
    std::cout << "  Benchmark completed.\n";
    std::cout << "  -> Graphs: python3 utils/plot_benchmark.py benchmark_results.csv\n";
    std::cout << "=====================================================================================================\n\n";

    return 0;
}
