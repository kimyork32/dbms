#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <cmath>

#include "catalog/catalog.hpp"
#include "storage/record/schema.hpp"
#include "storage/record/tuple_builder.hpp"
#include "storage/page/buffer_pool_manager.hpp"
#include "execution/plan_node.hpp"
#include "execution/executor.hpp"
#include "optimizer/optimizer.hpp"
#include "binder/binder.hpp"

using namespace megatron;
using namespace megatron::execution;
using namespace megatron::optimizer;
using namespace megatron::binder;

static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_EQ(val1, val2, msg) \
    do { \
        if ((val1) == (val2)) { \
            g_passed++; \
        } else { \
            g_failed++; \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg \
                      << " (Expected: " << static_cast<long long>(val2) \
                      << ", Actual: " << static_cast<long long>(val1) << ")" << std::endl; \
        } \
    } while (0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (cond) { \
            g_passed++; \
        } else { \
            g_failed++; \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
        } \
    } while (0)

#define ASSERT_DOUBLE_EQ(val1, val2, eps, msg) \
    do { \
        if (std::abs((val1) - (val2)) <= (eps)) { \
            g_passed++; \
        } else { \
            g_failed++; \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg \
                      << " (Expected: " << (val2) << ", Actual: " << (val1) << ")" << std::endl; \
        } \
    } while (0)

// Helper to create synthetic tables with target_pages
static void CreateSyntheticTable(const std::string& table_name, uint32_t target_pages, const Schema& schema) {
    std::string filename = table_name + ".bd";
    unlink(filename.c_str());

    GlobalBufferPoolManager bpm(target_pages + 10);
    bpm.ClearTablePages(table_name);

    for (uint32_t p = 0; p < target_pages; ++p) {
        uint32_t pid;
        SlottedPage* page = bpm.NewPage(table_name, &pid);
        if (!page) break;

        for (int tuple_idx = 0; tuple_idx < 4; ++tuple_idx) {
            TupleBuilder builder(&schema);
            int id_val = static_cast<int>(p * 4 + tuple_idx);
            builder.SetInt("id", id_val);
            builder.SetInt("val", id_val % 50);
            if (schema.columns.size() > 2) {
                builder.SetVarchar("payload", "str_" + std::to_string(id_val));
            }
            if (!page->InsertTuple(builder.GetData(), builder.GetSize())) {
                break;
            }
        }
        bpm.UnpinPage(table_name, pid, true);
    }
    bpm.FlushAllPages();
}

static Schema MakeTestSchema() {
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("val", TypeId::INTEGER);
    schema.AddColumn("payload", TypeId::VARCHAR);
    return schema;
}

// -----------------------------------------------------------------------------
// Test 1: Optimizer Hint Injection Rules on Complex Nested Plan Trees
// -----------------------------------------------------------------------------
static void TestComplexPlanTreeHintInjection() {
    std::cout << "=== Test 1: Complex Nested Plan Tree Hint Injection Rules ===" << std::endl;

    Catalog catalog;
    Schema schema = MakeTestSchema();
    catalog.CreateTable("t1", schema);
    catalog.CreateTable("t2", schema);
    catalog.CreateTable("t3", schema);
    catalog.CreateTable("t4", schema);
    catalog.CreateTable("catalog", schema);
    catalog.CreateTable("__sys_cat", schema);

    Optimizer optimizer(catalog);

    // 1a. Complex HashJoin containing NestedLoopJoin on left child and SeqScan on right child
    {
        auto scan1 = std::make_shared<SeqScanPlanNode>(schema, "t1", catalog.GetTable("t1"), BufferHint::DEFAULT);
        auto scan2 = std::make_shared<SeqScanPlanNode>(schema, "t2", catalog.GetTable("t2"), BufferHint::DEFAULT);
        auto nlj_child = std::make_shared<NestedLoopJoinPlanNode>(schema, scan1, scan2, nullptr, BufferHint::DEFAULT);

        auto scan3 = std::make_shared<SeqScanPlanNode>(schema, "t3", catalog.GetTable("t3"), BufferHint::DEFAULT);
        auto filter_right = std::make_shared<FilterPlanNode>(schema, scan3, [](const Tuple&){ return true; }, BufferHint::DEFAULT);

        auto root_hj = std::make_shared<HashJoinPlanNode>(schema, nlj_child, filter_right, 0, 0, BufferHint::DEFAULT);

        std::shared_ptr<AbstractPlanNode> plan = root_hj;
        optimizer.InjectBufferHints(plan);

        // Root HashJoin -> left subtree gets KEEP_HOT, right subtree gets DISCARD_QUICKLY
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "HJ left child gets KEEP_HOT");
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(0)->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "HJ left child scan1 gets KEEP_HOT");
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(0)->GetChildAt(1)->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "HJ left child scan2 gets KEEP_HOT");

        ASSERT_EQ(static_cast<int>(plan->GetChildAt(1)->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "HJ right child filter gets DISCARD_QUICKLY");
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(1)->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "HJ right child scan3 gets DISCARD_QUICKLY");
    }

    // 1b. Deep 4-table join tree: NLJ( HJ(t1, t2), HJ(t3, t4) )
    {
        auto t1_scan = std::make_shared<SeqScanPlanNode>(schema, "t1", catalog.GetTable("t1"), BufferHint::DEFAULT);
        auto t2_scan = std::make_shared<SeqScanPlanNode>(schema, "t2", catalog.GetTable("t2"), BufferHint::DEFAULT);
        auto hj_left = std::make_shared<HashJoinPlanNode>(schema, t1_scan, t2_scan, 0, 0, BufferHint::DEFAULT);

        auto t3_scan = std::make_shared<SeqScanPlanNode>(schema, "t3", catalog.GetTable("t3"), BufferHint::DEFAULT);
        auto t4_scan = std::make_shared<SeqScanPlanNode>(schema, "t4", catalog.GetTable("t4"), BufferHint::DEFAULT);
        auto hj_right = std::make_shared<HashJoinPlanNode>(schema, t3_scan, t4_scan, 0, 0, BufferHint::DEFAULT);

        auto root_nlj = std::make_shared<NestedLoopJoinPlanNode>(schema, hj_left, hj_right, nullptr, BufferHint::DEFAULT);

        std::shared_ptr<AbstractPlanNode> plan = root_nlj;
        optimizer.InjectBufferHints(plan);

        // Root NLJ -> left subtree gets DISCARD_QUICKLY, right subtree gets KEEP_HOT
        // Left subtree (hj_left) & all descendants -> DISCARD_QUICKLY
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "NLJ outer subtree root gets DISCARD_QUICKLY");
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(0)->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "NLJ outer hj_left t1 gets DISCARD_QUICKLY");
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(0)->GetChildAt(1)->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "NLJ outer hj_left t2 gets DISCARD_QUICKLY");

        // Right subtree (hj_right) & all descendants -> KEEP_HOT
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(1)->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "NLJ inner subtree root gets KEEP_HOT");
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(1)->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "NLJ inner hj_right t3 gets KEEP_HOT");
        ASSERT_EQ(static_cast<int>(plan->GetChildAt(1)->GetChildAt(1)->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "NLJ inner hj_right t4 gets KEEP_HOT");
    }

    // 1c. Mix Projection and Aggregation wrapping catalog & user table scans
    {
        auto sys_scan = std::make_shared<SeqScanPlanNode>(schema, "catalog", catalog.GetTable("catalog"), BufferHint::DEFAULT);
        auto proj_sys = std::make_shared<ProjectionPlanNode>(schema, sys_scan, std::vector<size_t>{0, 1}, BufferHint::DEFAULT);

        auto user_scan = std::make_shared<SeqScanPlanNode>(schema, "t1", catalog.GetTable("t1"), BufferHint::DEFAULT);
        auto agg_user = std::make_shared<AggregationPlanNode>(schema, user_scan, std::vector<size_t>{0}, 1, AggregateType::SUM, BufferHint::DEFAULT);

        std::shared_ptr<AbstractPlanNode> p1 = proj_sys;
        optimizer.InjectBufferHints(p1);
        ASSERT_EQ(static_cast<int>(p1->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "Catalog scan wrapped in Projection gets KEEP_HOT");

        std::shared_ptr<AbstractPlanNode> p2 = agg_user;
        optimizer.InjectBufferHints(p2);
        ASSERT_EQ(static_cast<int>(p2->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "Normal scan wrapped in Aggregation gets DISCARD_QUICKLY");
    }
}

// -----------------------------------------------------------------------------
// Test 2: Edge Case Buffer Capacities (1%, 100%, and extreme boundaries)
// -----------------------------------------------------------------------------
static void TestEdgeCaseBufferCapacities() {
    std::cout << "\n=== Test 2: Edge Case Buffer Capacities (1%, 100%, Extreme Limits) ===" << std::endl;

    Schema schema = MakeTestSchema();
    uint32_t dataset_pages = 100;
    CreateSyntheticTable("edge_t1", dataset_pages, schema);
    CreateSyntheticTable("edge_t2", dataset_pages, schema);

    // 2a. Edge case: Buffer pool capacity = 1 frame (1% of dataset)
    {
        std::cout << "  - Subtest 2a: Buffer Pool Size = 1 (1% edge capacity)" << std::endl;
        GlobalBufferPoolManager bpm(1);
        bpm.ResetMetrics();

        // Sequential Scan with DISCARD_QUICKLY over 100 pages
        auto scan_node = std::make_shared<SeqScanPlanNode>(schema, "edge_t1", nullptr, BufferHint::DISCARD_QUICKLY);
        SeqScanExecutor scan_exec(scan_node.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
        scan_exec.Init();

        Tuple t;
        RID r;
        size_t tuple_count = 0;
        while (scan_exec.Next(&t, &r)) {
            tuple_count++;
        }

        ASSERT_TRUE(tuple_count > 0, "Scan under pool_size=1 returned tuples");
        ASSERT_EQ(bpm.GetPageMisses(), static_cast<size_t>(dataset_pages), "100 page misses for 100 distinct pages under pool_size=1");
        ASSERT_EQ(bpm.GetPageHits(), static_cast<size_t>(0), "0 page hits under pool_size=1 single pass scan");
        ASSERT_DOUBLE_EQ(bpm.GetMissRatio(), 1.0, 1e-6, "Miss ratio is 100% (1.0) under pool_size=1 single pass");
    }

    // 2b. Edge case: Nested Loop Join with Buffer pool size = 2 frames (2% capacity limit)
    {
        std::cout << "  - Subtest 2b: NLJ under Buffer Pool Size = 2" << std::endl;
        GlobalBufferPoolManager bpm(2);
        bpm.ResetMetrics();

        // 10 pages outer, 10 pages inner
        CreateSyntheticTable("nlj_out", 10, schema);
        CreateSyntheticTable("nlj_in", 10, schema);

        auto left_scan = std::make_shared<SeqScanPlanNode>(schema, "nlj_out", nullptr, BufferHint::DISCARD_QUICKLY);
        auto right_scan = std::make_shared<SeqScanPlanNode>(schema, "nlj_in", nullptr, BufferHint::KEEP_HOT);

        auto join_pred = [](const Tuple& left, const Tuple& right) -> bool {
            return (!left.empty() && !right.empty() && left[0] == right[0]);
        };

        auto nlj_plan = std::make_shared<NestedLoopJoinPlanNode>(schema, left_scan, right_scan, join_pred, BufferHint::DEFAULT);

        auto left_exec = std::make_unique<SeqScanExecutor>(left_scan.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
        auto right_exec = std::make_unique<SeqScanExecutor>(right_scan.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);

        NestedLoopJoinExecutor nlj_exec(nlj_plan.get(), std::move(left_exec), std::move(right_exec));
        nlj_exec.Init();

        Tuple t;
        RID r;
        size_t count = 0;
        while (nlj_exec.Next(&t, &r)) {
            count++;
        }

        ASSERT_TRUE(count > 0, "NLJ completed under pool_size=2");
        ASSERT_TRUE(bpm.GetDiskIOCount() > 0, "Disk I/O recorded under pool_size=2 NLJ");

        unlink("nlj_out.bd");
        unlink("nlj_in.bd");
    }

    // 2c. Edge case: Buffer pool capacity = 100% (100 frames for 100 page dataset)
    {
        std::cout << "  - Subtest 2c: Buffer Pool Size = 100 (100% capacity)" << std::endl;
        GlobalBufferPoolManager bpm(dataset_pages);
        bpm.ResetMetrics();

        auto scan_node = std::make_shared<SeqScanPlanNode>(schema, "edge_t1", nullptr, BufferHint::KEEP_HOT);
        
        // First pass: loads all 100 pages into buffer pool
        {
            SeqScanExecutor scan_exec1(scan_node.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
            scan_exec1.Init();
            Tuple t;
            RID r;
            while (scan_exec1.Next(&t, &r)) {}
        }

        size_t first_pass_misses = bpm.GetPageMisses();
        ASSERT_EQ(first_pass_misses, static_cast<size_t>(dataset_pages), "First pass has 100 misses");

        // Second pass: all 100 pages are cached in memory (KEEP_HOT)
        {
            SeqScanExecutor scan_exec2(scan_node.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
            scan_exec2.Init();
            Tuple t;
            RID r;
            while (scan_exec2.Next(&t, &r)) {}
        }

        size_t second_pass_misses = bpm.GetPageMisses() - first_pass_misses;
        size_t second_pass_hits = bpm.GetPageHits();

        ASSERT_EQ(second_pass_misses, static_cast<size_t>(0), "Second pass under 100% buffer size has ZERO misses");
        ASSERT_EQ(second_pass_hits, static_cast<size_t>(dataset_pages), "Second pass under 100% buffer size has 100 hits");
    }

    // 2d. Edge case: Buffer pool capacity = 200% (>100% oversized buffer pool)
    {
        std::cout << "  - Subtest 2d: Oversized Buffer Pool (200% capacity)" << std::endl;
        GlobalBufferPoolManager bpm(dataset_pages * 2);
        bpm.ResetMetrics();

        auto scan_node = std::make_shared<SeqScanPlanNode>(schema, "edge_t2", nullptr, BufferHint::DEFAULT);
        SeqScanExecutor scan_exec(scan_node.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
        scan_exec.Init();
        Tuple t;
        RID r;
        while (scan_exec.Next(&t, &r)) {}

        ASSERT_EQ(bpm.GetPageMisses(), static_cast<size_t>(dataset_pages), "100 misses for oversized pool");
        ASSERT_EQ(bpm.GetDiskWrites(), static_cast<size_t>(0), "0 disk writes during read-only scan");
    }

    unlink("edge_t1.bd");
    unlink("edge_t2.bd");
}

// -----------------------------------------------------------------------------
// Test 3: Stress Testing Complex Nested Plan Execution & Resilience
// -----------------------------------------------------------------------------
static void TestComplexPlanExecutionStress() {
    std::cout << "\n=== Test 3: Complex Nested Plan Execution & Buffer Resilience ===" << std::endl;

    Schema schema = MakeTestSchema();
    uint32_t pages = 20;

    CreateSyntheticTable("stress_t1", pages, schema);
    CreateSyntheticTable("stress_t2", pages, schema);
    CreateSyntheticTable("stress_t3", pages, schema);

    // Build 3-way join plan tree: HashJoin( t1, NestedLoopJoin(t2, t3) )
    // Total dataset pages = 60. We use a buffer size of 5 frames (~8% capacity).
    GlobalBufferPoolManager bpm(5);
    bpm.ResetMetrics();

    Catalog catalog;
    Optimizer optimizer(catalog);

    auto scan1 = std::make_shared<SeqScanPlanNode>(schema, "stress_t1", nullptr, BufferHint::DEFAULT);
    auto scan2 = std::make_shared<SeqScanPlanNode>(schema, "stress_t2", nullptr, BufferHint::DEFAULT);
    auto scan3 = std::make_shared<SeqScanPlanNode>(schema, "stress_t3", nullptr, BufferHint::DEFAULT);

    auto join_pred = [](const Tuple& left, const Tuple& right) -> bool {
        return (!left.empty() && !right.empty() && left[0] == right[0]);
    };

    auto nlj_child = std::make_shared<NestedLoopJoinPlanNode>(schema, scan2, scan3, join_pred, BufferHint::DEFAULT);
    auto hj_root = std::make_shared<HashJoinPlanNode>(schema, scan1, nlj_child, 0, 0, BufferHint::DEFAULT);

    std::shared_ptr<AbstractPlanNode> plan_root = hj_root;
    optimizer.InjectBufferHints(plan_root);

    // Verify injected hints:
    // HashJoin build side (scan1) -> KEEP_HOT
    // HashJoin probe side (nlj_child) -> DISCARD_QUICKLY
    // nlj_child outer (scan2) -> DISCARD_QUICKLY
    // nlj_child inner (scan3) -> DISCARD_QUICKLY (overridden by top-down recursive assignment from HJ probe side)
    ASSERT_EQ(static_cast<int>(plan_root->GetChildAt(0)->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "HJ build side gets KEEP_HOT");
    ASSERT_EQ(static_cast<int>(plan_root->GetChildAt(1)->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "HJ probe side gets DISCARD_QUICKLY");

    // Execute 3-way join pipeline
    auto exec1 = std::make_unique<SeqScanExecutor>(static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(0)), nullptr, std::vector<Tuple>{}, nullptr, &bpm);

    auto exec2 = std::make_unique<SeqScanExecutor>(static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(1)->GetChildAt(0)), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    auto exec3 = std::make_unique<SeqScanExecutor>(static_cast<const SeqScanPlanNode*>(plan_root->GetChildAt(1)->GetChildAt(1)), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    auto nlj_exec = std::make_unique<NestedLoopJoinExecutor>(static_cast<const NestedLoopJoinPlanNode*>(plan_root->GetChildAt(1)), std::move(exec2), std::move(exec3));

    HashJoinExecutor hj_exec(hj_root.get(), std::move(exec1), std::move(nlj_exec));

    auto start_time = std::chrono::high_resolution_clock::now();
    hj_exec.Init();
    Tuple t;
    RID r;
    size_t joined_count = 0;
    while (hj_exec.Next(&t, &r)) {
        joined_count++;
    }
    auto end_time = std::chrono::high_resolution_clock::now();

    double latency_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "  - Joined output tuple count: " << joined_count << std::endl;
    std::cout << "  - Buffer Hits: " << bpm.GetPageHits() << ", Misses: " << bpm.GetPageMisses() << std::endl;
    std::cout << "  - Disk I/O Count: " << bpm.GetDiskIOCount() << std::endl;
    std::cout << "  - Miss Ratio: " << std::fixed << std::setprecision(2) << (bpm.GetMissRatio() * 100.0) << "%" << std::endl;
    std::cout << "  - Execution Latency: " << std::fixed << std::setprecision(2) << latency_ms << " ms" << std::endl;

    ASSERT_TRUE(joined_count > 0, "3-way join execution completed with matching tuples");
    ASSERT_TRUE(bpm.GetDiskIOCount() > 0, "Metrics correctly recorded disk I/O");

    unlink("stress_t1.bd");
    unlink("stress_t2.bd");
    unlink("stress_t3.bd");
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "   M2 & M4 Challenger 1 Stress & Edge Case Verification Suite  " << std::endl;
    std::cout << "================================================================" << std::endl;

    TestComplexPlanTreeHintInjection();
    TestEdgeCaseBufferCapacities();
    TestComplexPlanExecutionStress();

    std::cout << "\n================================================================" << std::endl;
    std::cout << " Test Summary: " << g_passed << " PASSED, " << g_failed << " FAILED" << std::endl;
    std::cout << "================================================================" << std::endl;

    return (g_failed == 0) ? 0 : 1;
}
