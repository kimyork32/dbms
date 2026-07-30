#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unistd.h>

#include "execution/plan_node.hpp"
#include "execution/executor.hpp"
#include "storage/page/buffer_pool_manager.hpp"
#include "storage/record/schema.hpp"
#include "storage/record/tuple_builder.hpp"
#include "storage/engine/disk_storage_engine.hpp"
#include "optimizer/optimizer.hpp"
#include "binder/binder.hpp"
#include "catalog/catalog.hpp"

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
                      << " (Expected: " << static_cast<int>(val2) \
                      << ", Actual: " << static_cast<int>(val1) << ")" << std::endl; \
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

// Helper to set up a schema
Schema CreateTestSchema() {
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("val", TypeId::VARCHAR);
    return schema;
}

// -----------------------------------------------------------------------------
// Test 1: Page Allocation Lifecycle with Table Creation / Unlink / Recreation Cycles
// -----------------------------------------------------------------------------
void TestTableCreationDeletionLifecycle() {
    std::cout << "--- 1. Testing Page Allocation Lifecycle (Create/Unlink/Recreate Cycles) ---" << std::endl;

    GlobalBufferPoolManager bpm(5);
    GlobalBufferPoolManager::ResetGlobalState();

    std::string tbl = "test_lifecycle_tbl";
    
    // Cycle 1: Allocate 5 pages
    for (uint32_t i = 0; i < 5; ++i) {
        uint32_t page_id = 999;
        SlottedPage* p = bpm.NewPage(tbl, &page_id);
        ASSERT_TRUE(p != nullptr, "NewPage returned valid page pointer in Cycle 1");
        ASSERT_EQ(page_id, i, "Page ID allocated sequentially in Cycle 1");
        bpm.UnpinPage(tbl, page_id, true);
    }
    bpm.FlushAllPages();

    // Verify page count on disk / BPM
    ASSERT_EQ(bpm.GetNumPages(tbl), 5u, "GetNumPages returns 5 after Cycle 1");

    // Unlink table file and clear table pages
    unlink((tbl + ".bd").c_str());
    bpm.ClearTablePages(tbl);

    // Verify page count after clear
    ASSERT_EQ(bpm.GetNumPages(tbl), 0u, "GetNumPages returns 0 after ClearTablePages and unlink");

    // Cycle 2: Re-create table. New pages MUST start at page_id 0!
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t page_id = 999;
        SlottedPage* p = bpm.NewPage(tbl, &page_id);
        ASSERT_TRUE(p != nullptr, "NewPage returned valid page pointer in Cycle 2");
        ASSERT_EQ(page_id, i, "Page ID starts at 0 after table recreation in Cycle 2");
        bpm.UnpinPage(tbl, page_id, true);
    }
    bpm.FlushAllPages();

    ASSERT_EQ(bpm.GetNumPages(tbl), 3u, "GetNumPages returns 3 after Cycle 2");

    // Cycle 3: Perform 50 rapid creation/deletion iterations
    for (int cycle = 0; cycle < 50; ++cycle) {
        std::string cycle_tbl = "cycle_tbl_" + std::to_string(cycle % 5);
        unlink((cycle_tbl + ".bd").c_str());
        bpm.ClearTablePages(cycle_tbl);

        uint32_t p0, p1;
        bpm.NewPage(cycle_tbl, &p0);
        ASSERT_EQ(p0, 0u, "Rapid cycle page 0 starts at page_id 0");
        bpm.UnpinPage(cycle_tbl, p0, true);

        bpm.NewPage(cycle_tbl, &p1);
        ASSERT_EQ(p1, 1u, "Rapid cycle page 1 is page_id 1");
        bpm.UnpinPage(cycle_tbl, p1, true);

        bpm.FlushAllPages();
        unlink((cycle_tbl + ".bd").c_str());
        bpm.ClearTablePages(cycle_tbl);
    }

    unlink((tbl + ".bd").c_str());
    bpm.ClearTablePages(tbl);
}

// -----------------------------------------------------------------------------
// Test 2: Buffer Pool Eviction with Cleared Frames (Tier 0 Invalid Victim Selection)
// -----------------------------------------------------------------------------
void TestBufferPoolEvictionWithClearedFrames() {
    std::cout << "--- 2. Testing Eviction with Cleared Invalid Frames ---" << std::endl;

    GlobalBufferPoolManager bpm(4); // 4 frames capacity
    GlobalBufferPoolManager::ResetGlobalState();

    // Fill pool with 2 pages of table A (KEEP_HOT) and 2 pages of table B (KEEP_HOT)
    uint32_t a0, a1, b0, b1;
    SlottedPage* pa0 = bpm.NewPage("tbl_A", &a0, BufferHint::KEEP_HOT);
    SlottedPage* pa1 = bpm.NewPage("tbl_A", &a1, BufferHint::KEEP_HOT);
    SlottedPage* pb0 = bpm.NewPage("tbl_B", &b0, BufferHint::KEEP_HOT);
    SlottedPage* pb1 = bpm.NewPage("tbl_B", &b1, BufferHint::KEEP_HOT);

    (void)pa0; (void)pa1; (void)pb0; (void)pb1;

    bpm.UnpinPage("tbl_A", a0, true);
    bpm.UnpinPage("tbl_A", a1, true);
    bpm.UnpinPage("tbl_B", b0, true);
    bpm.UnpinPage("tbl_B", b1, true);

    // Now buffer pool is 100% full with KEEP_HOT pages.
    // Clear Table A
    unlink("tbl_A.bd");
    bpm.ClearTablePages("tbl_A");

    // Buffer pool now has 2 invalid frames (formerly tbl_A) and 2 valid KEEP_HOT frames (tbl_B).
    // Reset metrics to track hits/misses
    bpm.ResetMetrics();

    // Allocate new page for Table C. Victim selection MUST pick invalid frame (Tier 0) instantly!
    uint32_t c0;
    SlottedPage* pc0 = bpm.NewPage("tbl_C", &c0, BufferHint::DEFAULT);
    ASSERT_TRUE(pc0 != nullptr, "New page allocated into cleared frame");
    ASSERT_EQ(c0, 0u, "tbl_C initial page is 0");
    bpm.UnpinPage("tbl_C", c0, false);

    // Fetching tbl_B page 0 should still be a HIT because it was NOT evicted!
    SlottedPage* fetch_b0 = bpm.FetchPage("tbl_B", b0);
    ASSERT_TRUE(fetch_b0 != nullptr, "tbl_B page 0 fetched successfully");
    ASSERT_EQ(bpm.GetPageHits(), 1u, "tbl_B page 0 fetch was a BUFFER HIT");
    bpm.UnpinPage("tbl_B", b0, false);

    unlink("tbl_B.bd");
    unlink("tbl_C.bd");
    bpm.ClearTablePages("tbl_B");
    bpm.ClearTablePages("tbl_C");
}

// -----------------------------------------------------------------------------
// Test 3: DiskStorageEngine CreateTable Interoperability & Page Clear
// -----------------------------------------------------------------------------
void TestDiskStorageEngineCreateTableClear() {
    std::cout << "--- 3. Testing DiskStorageEngine CreateTable Clear Logic ---" << std::endl;

    std::string tbl_engine = "engine_test_tbl";
    std::vector<std::string> cols = {"id", "val"};

    // 1. Create table via engine 1
    {
        DiskStorageEngine engine1(5);
        bool ok1 = engine1.CreateTable(tbl_engine, cols);
        ASSERT_TRUE(ok1, "CreateTable succeeded on first engine");

        Tuple t;
        t.push_back("1");
        t.push_back("hello");

        bool ok_ins1 = engine1.InsertTuple(tbl_engine, t);
        ASSERT_TRUE(ok_ins1, "InsertTuple succeeded on first engine");
        
        auto res1 = engine1.FullScan(tbl_engine);
        ASSERT_EQ(res1.size(), 1u, "FullScan returned 1 tuple from engine 1");
    }

    // 2. Instantiate engine 2 (new engine instance for same table name, calling CreateTable which unlinks old file and clears BPM)
    {
        DiskStorageEngine engine2(5);
        bool ok2 = engine2.CreateTable(tbl_engine, cols);
        ASSERT_TRUE(ok2, "CreateTable succeeded on engine 2 (recreation)");

        auto res_before = engine2.FullScan(tbl_engine);
        ASSERT_EQ(res_before.size(), 0u, "Recreated engine table starts empty");

        Tuple t2;
        t2.push_back("2");
        t2.push_back("world");

        bool ok_ins2 = engine2.InsertTuple(tbl_engine, t2);
        ASSERT_TRUE(ok_ins2, "InsertTuple succeeded on recreated engine table");

        auto res_after = engine2.FullScan(tbl_engine);
        ASSERT_EQ(res_after.size(), 1u, "FullScan returns 1 tuple after insertion into recreated table");
    }

    // Clean up
    unlink((tbl_engine + ".bd").c_str());
    unlink((tbl_engine + "_index.db").c_str());
}


// -----------------------------------------------------------------------------
// Test 4: Optimizer Hint Injection & Complex Plan Trees
// -----------------------------------------------------------------------------
void TestOptimizerComplexTreeHintInjection() {
    std::cout << "--- 4. Testing Optimizer Complex Plan Subtree Hint Injection ---" << std::endl;

    Catalog catalog;
    Schema s1, s2, s3, s4;
    s1.AddColumn("a_id", TypeId::INTEGER); s1.AddColumn("a_val", TypeId::VARCHAR);
    s2.AddColumn("b_id", TypeId::INTEGER); s2.AddColumn("b_val", TypeId::VARCHAR);
    s3.AddColumn("c_id", TypeId::INTEGER); s3.AddColumn("c_val", TypeId::VARCHAR);
    s4.AddColumn("d_id", TypeId::INTEGER); s4.AddColumn("d_val", TypeId::VARCHAR);

    catalog.CreateTable("t_a", s1);
    catalog.CreateTable("t_b", s2);
    catalog.CreateTable("t_c", s3);
    catalog.CreateTable("t_d", s4);

    Optimizer optimizer(catalog);

    // Build subtrees:
    // Left subtree: HashJoin(t_a, t_b)
    auto scan_a = std::make_shared<SeqScanPlanNode>(s1, "t_a", catalog.GetTable("t_a"), BufferHint::DEFAULT);
    auto scan_b = std::make_shared<SeqScanPlanNode>(s2, "t_b", catalog.GetTable("t_b"), BufferHint::DEFAULT);
    Schema s_ab;
    s_ab.AddColumn("a_id", TypeId::INTEGER); s_ab.AddColumn("a_val", TypeId::VARCHAR);
    s_ab.AddColumn("b_id", TypeId::INTEGER); s_ab.AddColumn("b_val", TypeId::VARCHAR);
    auto hj_left = std::make_shared<HashJoinPlanNode>(s_ab, scan_a, scan_b, 0, 0, BufferHint::DEFAULT);

    // Right subtree: NestedLoopJoin(t_c, t_d)
    auto scan_c = std::make_shared<SeqScanPlanNode>(s3, "t_c", catalog.GetTable("t_c"), BufferHint::DEFAULT);
    auto scan_d = std::make_shared<SeqScanPlanNode>(s4, "t_d", catalog.GetTable("t_d"), BufferHint::DEFAULT);
    Schema s_cd;
    s_cd.AddColumn("c_id", TypeId::INTEGER); s_cd.AddColumn("c_val", TypeId::VARCHAR);
    s_cd.AddColumn("d_id", TypeId::INTEGER); s_cd.AddColumn("d_val", TypeId::VARCHAR);
    auto nlj_right = std::make_shared<NestedLoopJoinPlanNode>(s_cd, scan_c, scan_d, nullptr, BufferHint::DEFAULT);

    // Top-level HashJoin(hj_left, nlj_right)
    Schema s_top;
    s_top.AddColumn("a_id", TypeId::INTEGER); s_top.AddColumn("a_val", TypeId::VARCHAR);
    s_top.AddColumn("b_id", TypeId::INTEGER); s_top.AddColumn("b_val", TypeId::VARCHAR);
    s_top.AddColumn("c_id", TypeId::INTEGER); s_top.AddColumn("c_val", TypeId::VARCHAR);
    s_top.AddColumn("d_id", TypeId::INTEGER); s_top.AddColumn("d_val", TypeId::VARCHAR);
    auto top_plan = std::make_shared<HashJoinPlanNode>(s_top, hj_left, nlj_right, 0, 0, BufferHint::DEFAULT);

    std::shared_ptr<AbstractPlanNode> root = top_plan;
    optimizer.InjectBufferHints(root);

    // Verification:
    // Top HashJoin: left child (hj_left) receives KEEP_HOT across its subtree.
    // hj_left subtree: scan_a and scan_b both carry KEEP_HOT!
    ASSERT_EQ(hj_left->GetBufferHint(), BufferHint::KEEP_HOT, "Top HashJoin left child (build) gets KEEP_HOT");
    ASSERT_EQ(scan_a->GetBufferHint(), BufferHint::KEEP_HOT, "Descendant scan_a gets KEEP_HOT");
    ASSERT_EQ(scan_b->GetBufferHint(), BufferHint::KEEP_HOT, "Descendant scan_b gets KEEP_HOT");

    // Top HashJoin: right child (nlj_right) receives DISCARD_QUICKLY across its subtree.
    // nlj_right subtree: scan_c and scan_d both carry DISCARD_QUICKLY!
    ASSERT_EQ(nlj_right->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Top HashJoin right child (probe) gets DISCARD_QUICKLY");
    ASSERT_EQ(scan_c->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Descendant scan_c gets DISCARD_QUICKLY");
    ASSERT_EQ(scan_d->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Descendant scan_d gets DISCARD_QUICKLY");
}

// -----------------------------------------------------------------------------
// Test 5: Synthetic Benchmark Data Refetching & Rescan
// -----------------------------------------------------------------------------
void TestSyntheticBenchmarkRefetch() {
    std::cout << "--- 5. Testing Synthetic Data Generator Refetching ---" << std::endl;

    GlobalBufferPoolManager bpm(5);
    GlobalBufferPoolManager::ResetGlobalState();

    Schema schema = CreateTestSchema();
    std::string tbl = "bench_refetch_tbl";

    // 1. Create table file with 10 pages
    for (uint32_t p = 0; p < 10; ++p) {
        uint32_t pid;
        SlottedPage* page = bpm.NewPage(tbl, &pid);
        ASSERT_TRUE(page != nullptr, "Allocated synthetic page");
        TupleBuilder b(&schema);
        b.SetInt("id", p * 10);
        b.SetVarchar("val", "str_" + std::to_string(p));
        page->InsertTuple(b.GetData(), b.GetSize());
        bpm.UnpinPage(tbl, pid, true);
    }
    bpm.FlushAllPages();

    // 2. Unlink table file and regenerate with 4 pages (simulating benchmark workload reset)
    unlink((tbl + ".bd").c_str());
    bpm.ClearTablePages(tbl);

    for (uint32_t p = 0; p < 4; ++p) {
        uint32_t pid;
        SlottedPage* page = bpm.NewPage(tbl, &pid);
        ASSERT_EQ(pid, p, "Regenerated synthetic page starts at page_id 0");
        TupleBuilder b(&schema);
        b.SetInt("id", p * 100);
        b.SetVarchar("val", "new_" + std::to_string(p));
        page->InsertTuple(b.GetData(), b.GetSize());
        bpm.UnpinPage(tbl, pid, true);
    }
    bpm.FlushAllPages();

    // 3. Scan all 4 pages using SeqScanExecutor
    auto scan_node = std::make_shared<SeqScanPlanNode>(schema, tbl, nullptr, BufferHint::DISCARD_QUICKLY);
    SeqScanExecutor exec(scan_node.get(), nullptr, {}, nullptr, &bpm);

    exec.Init();
    Tuple t; RID r;
    int tuple_count = 0;
    while (exec.Next(&t, &r)) {
        tuple_count++;
    }

    ASSERT_EQ(tuple_count, 4, "SeqScanExecutor successfully scanned all 4 regenerated tuples without I/O errors");

    unlink((tbl + ".bd").c_str());
    bpm.ClearTablePages(tbl);
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  M2 Remediation Challenger 1 Stress & Lifecycle Suite  \n";
    std::cout << "========================================================\n";

    TestTableCreationDeletionLifecycle();
    TestBufferPoolEvictionWithClearedFrames();
    TestDiskStorageEngineCreateTableClear();
    TestOptimizerComplexTreeHintInjection();
    TestSyntheticBenchmarkRefetch();

    std::cout << "========================================================\n";
    std::cout << " Summary: " << g_passed << " PASSED, " << g_failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}
