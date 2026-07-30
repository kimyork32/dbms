#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <unistd.h>

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

static void AssertTrue(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "[FAIL] " << msg << std::endl;
        std::exit(1);
    }
}

static Schema BuildTestSchema() {
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("val", TypeId::INTEGER);
    schema.AddColumn("payload", TypeId::VARCHAR);
    return schema;
}

static Schema BuildJoinSchema(const Schema& left, const Schema& right) {
    Schema joined;
    for (const auto& col : left.columns) {
        joined.columns.push_back(col);
    }
    for (const auto& col : right.columns) {
        joined.columns.push_back(col);
    }
    return joined;
}

static void CreateTestTableData(GlobalBufferPoolManager& bpm, const std::string& table_name, uint32_t num_pages, const Schema& schema) {
    bpm.ClearTablePages(table_name);
    std::string filename = table_name + ".bd";
    unlink(filename.c_str());

    for (uint32_t p = 0; p < num_pages; ++p) {
        uint32_t pid;
        SlottedPage* page = bpm.NewPage(table_name, &pid);
        AssertTrue(page != nullptr, "NewPage failed during CreateTestTableData for " + table_name);

        for (int i = 0; i < 5; ++i) {
            TupleBuilder builder(&schema);
            int id_val = static_cast<int>(p * 5 + i);
            builder.SetInt("id", id_val);
            builder.SetInt("val", id_val % 50);
            builder.SetVarchar("payload", "stress_test_data_" + std::to_string(id_val));
            if (!page->InsertTuple(builder.GetData(), builder.GetSize())) {
                break;
            }
        }
        bpm.UnpinPage(table_name, pid, true);
    }
    bpm.FlushAllPages();
}

// -----------------------------------------------------------------------------
// 1. Optimizer Hint Injection Rules Stress Verification (Milestone 2)
// -----------------------------------------------------------------------------
static void TestOptimizerHintInjectionRules() {
    std::cout << "[Test 1] Running TestOptimizerHintInjectionRules..." << std::endl;

    Catalog catalog;
    Optimizer optimizer(catalog);
    Schema schema = BuildTestSchema();

    // 1.1 HashJoin: Left child (build side) MUST be KEEP_HOT, Right child (probe side) MUST be DISCARD_QUICKLY
    auto left_scan = std::make_shared<SeqScanPlanNode>(schema, "table_left", nullptr, BufferHint::DEFAULT);
    auto right_scan = std::make_shared<SeqScanPlanNode>(schema, "table_right", nullptr, BufferHint::DEFAULT);
    auto hj_plan = std::make_shared<HashJoinPlanNode>(
        BuildJoinSchema(schema, schema), left_scan, right_scan, 0, 0, BufferHint::DEFAULT
    );

    std::shared_ptr<AbstractPlanNode> hj_root = hj_plan;
    optimizer.InjectBufferHints(hj_root);

    AssertTrue(hj_root->GetBufferHint() == BufferHint::DEFAULT, "HashJoin root hint should be DEFAULT");
    AssertTrue(hj_root->GetChildAt(0)->GetBufferHint() == BufferHint::KEEP_HOT, "HashJoin left child (build) MUST be KEEP_HOT");
    AssertTrue(hj_root->GetChildAt(1)->GetBufferHint() == BufferHint::DISCARD_QUICKLY, "HashJoin right child (probe) MUST be DISCARD_QUICKLY");

    // 1.2 NestedLoopJoin: Left child (outer loop) MUST be DISCARD_QUICKLY, Right child (inner loop) MUST be KEEP_HOT
    auto outer_scan = std::make_shared<SeqScanPlanNode>(schema, "table_outer", nullptr, BufferHint::DEFAULT);
    auto inner_scan = std::make_shared<SeqScanPlanNode>(schema, "table_inner", nullptr, BufferHint::DEFAULT);
    auto nlj_plan = std::make_shared<NestedLoopJoinPlanNode>(
        BuildJoinSchema(schema, schema), outer_scan, inner_scan, nullptr, BufferHint::DEFAULT
    );

    std::shared_ptr<AbstractPlanNode> nlj_root = nlj_plan;
    optimizer.InjectBufferHints(nlj_root);

    AssertTrue(nlj_root->GetBufferHint() == BufferHint::DEFAULT, "NestedLoopJoin root hint should be DEFAULT");
    AssertTrue(nlj_root->GetChildAt(0)->GetBufferHint() == BufferHint::DISCARD_QUICKLY, "NestedLoopJoin outer child MUST be DISCARD_QUICKLY");
    AssertTrue(nlj_root->GetChildAt(1)->GetBufferHint() == BufferHint::KEEP_HOT, "NestedLoopJoin inner child MUST be KEEP_HOT");

    // 1.3 Standalone SeqScan: Normal table MUST be DISCARD_QUICKLY, Catalog tables MUST be KEEP_HOT
    auto normal_scan = std::make_shared<SeqScanPlanNode>(schema, "user_orders", nullptr, BufferHint::DEFAULT);
    std::shared_ptr<AbstractPlanNode> normal_root = normal_scan;
    optimizer.InjectBufferHints(normal_root);
    AssertTrue(normal_root->GetBufferHint() == BufferHint::DISCARD_QUICKLY, "Standalone normal SeqScan MUST be DISCARD_QUICKLY");

    auto cat_scan1 = std::make_shared<SeqScanPlanNode>(schema, "catalog", nullptr, BufferHint::DEFAULT);
    std::shared_ptr<AbstractPlanNode> cat_root1 = cat_scan1;
    optimizer.InjectBufferHints(cat_root1);
    AssertTrue(cat_root1->GetBufferHint() == BufferHint::KEEP_HOT, "Standalone 'catalog' table MUST be KEEP_HOT");

    auto cat_scan2 = std::make_shared<SeqScanPlanNode>(schema, "__sys_tables", nullptr, BufferHint::DEFAULT);
    std::shared_ptr<AbstractPlanNode> cat_root2 = cat_scan2;
    optimizer.InjectBufferHints(cat_root2);
    AssertTrue(cat_root2->GetBufferHint() == BufferHint::KEEP_HOT, "Standalone '__sys_tables' table MUST be KEEP_HOT");

    auto cat_scan3 = std::make_shared<SeqScanPlanNode>(schema, "system_tables", nullptr, BufferHint::DEFAULT);
    std::shared_ptr<AbstractPlanNode> cat_root3 = cat_scan3;
    optimizer.InjectBufferHints(cat_root3);
    AssertTrue(cat_root3->GetBufferHint() == BufferHint::KEEP_HOT, "Standalone 'system_tables' table MUST be KEEP_HOT");

    // 1.4 Subtree Hint Propagation: Subtrees under HashJoin left child should get KEEP_HOT recursively
    auto sub_scan = std::make_shared<SeqScanPlanNode>(schema, "build_table", nullptr, BufferHint::DEFAULT);
    auto filter_node = std::make_shared<FilterPlanNode>(schema, sub_scan, nullptr, BufferHint::DEFAULT);
    auto hj_deep = std::make_shared<HashJoinPlanNode>(
        BuildJoinSchema(schema, schema), filter_node, right_scan, 0, 0, BufferHint::DEFAULT
    );

    std::shared_ptr<AbstractPlanNode> deep_root = hj_deep;
    optimizer.InjectBufferHints(deep_root);

    AssertTrue(filter_node->GetBufferHint() == BufferHint::KEEP_HOT, "Filter on build side MUST receive KEEP_HOT");
    AssertTrue(sub_scan->GetBufferHint() == BufferHint::KEEP_HOT, "SeqScan under Filter on build side MUST receive KEEP_HOT");

    std::cout << "[PASSED] TestOptimizerHintInjectionRules" << std::endl;
}

// -----------------------------------------------------------------------------
// 2. Metrics Interface & Reset Behavior Verification (Milestones 1 & 4)
// -----------------------------------------------------------------------------
static void TestMetricResetAndAccuracyAcrossIterations() {
    std::cout << "[Test 2] Running TestMetricResetAndAccuracyAcrossIterations..." << std::endl;

    GlobalBufferPoolManager bpm(10);

    // Initial state check
    AssertTrue(bpm.GetPageHits() == 0, "Initial page hits must be 0");
    AssertTrue(bpm.GetPageMisses() == 0, "Initial page misses must be 0");
    AssertTrue(bpm.GetDiskWrites() == 0, "Initial disk writes must be 0");
    AssertTrue(bpm.GetDiskIOCount() == 0, "Initial disk I/O count must be 0");
    AssertTrue(bpm.GetMissRatio() == 0.0, "Initial miss ratio must be 0.0 (no fetches)");

    // Create table data
    Schema schema = BuildTestSchema();
    CreateTestTableData(bpm, "metric_test_table", 5, schema);

    // Clear table pages from buffer pool memory to force misses from disk
    bpm.ClearTablePages("metric_test_table");

    // Reset metrics after setup
    bpm.ResetMetrics();
    AssertTrue(bpm.GetPageHits() == 0, "Hits after reset must be 0");
    AssertTrue(bpm.GetPageMisses() == 0, "Misses after reset must be 0");
    AssertTrue(bpm.GetDiskWrites() == 0, "Writes after reset must be 0");
    AssertTrue(bpm.GetDiskIOCount() == 0, "Disk I/O after reset must be 0");
    AssertTrue(bpm.GetMissRatio() == 0.0, "Miss ratio after reset must be 0.0");

    // Perform controlled page fetches:
    // Fetch pages 0..4 for the first time -> 5 misses (since cleared from buffer pool)
    for (uint32_t i = 0; i < 5; ++i) {
        SlottedPage* p = nullptr;
        try {
            p = bpm.FetchPage("metric_test_table", i, BufferHint::DEFAULT);
        } catch (const std::exception& e) {
            std::cout << "FetchPage exception for page " << i << ": " << e.what() << std::endl;
        }
        AssertTrue(p != nullptr, "FetchPage failed for page " + std::to_string(i));
        if (p) bpm.UnpinPage("metric_test_table", i, false);
    }
    AssertTrue(bpm.GetPageMisses() == 5, "Should have exactly 5 page misses (got " + std::to_string(bpm.GetPageMisses()) + ")");
    AssertTrue(bpm.GetPageHits() == 0, "Should have 0 page hits so far");
    AssertTrue(bpm.GetMissRatio() == 1.0, "Miss ratio should be 1.0 (5/5 misses)");

    // Fetch pages 0..4 second time -> 5 hits (cached in buffer pool)
    for (uint32_t i = 0; i < 5; ++i) {
        SlottedPage* p = bpm.FetchPage("metric_test_table", i, BufferHint::DEFAULT);
        AssertTrue(p != nullptr, "FetchPage second call failed for page " + std::to_string(i));
        bpm.UnpinPage("metric_test_table", i, false);
    }
    AssertTrue(bpm.GetPageMisses() == 5, "Page misses should remain 5");
    AssertTrue(bpm.GetPageHits() == 5, "Page hits should now be 5");
    AssertTrue(std::abs(bpm.GetMissRatio() - 0.5) < 1e-6, "Miss ratio should be 0.5 (5 misses / 10 total)");
    AssertTrue(bpm.GetDiskIOCount() == bpm.GetPageMisses() + bpm.GetDiskWrites(), "DiskIOCount mismatch");

    // Repeated ResetMetrics across 20 iterations
    for (int iter = 0; iter < 20; ++iter) {
        bpm.ResetMetrics();
        AssertTrue(bpm.GetPageHits() == 0, "Hits reset failed in iteration " + std::to_string(iter));
        AssertTrue(bpm.GetPageMisses() == 0, "Misses reset failed in iteration " + std::to_string(iter));
        AssertTrue(bpm.GetDiskWrites() == 0, "Writes reset failed in iteration " + std::to_string(iter));
        AssertTrue(bpm.GetMissRatio() == 0.0, "Miss ratio reset failed in iteration " + std::to_string(iter));

        // Fetch page 0 twice (page 0 is resident in pool)
        SlottedPage* p1 = bpm.FetchPage("metric_test_table", 0, BufferHint::DEFAULT);
        bpm.UnpinPage("metric_test_table", 0, false);
        SlottedPage* p2 = bpm.FetchPage("metric_test_table", 0, BufferHint::DEFAULT);
        bpm.UnpinPage("metric_test_table", 0, false);

        (void)p1; (void)p2;

        AssertTrue(bpm.GetPageMisses() == 0, "Iter " + std::to_string(iter) + ": expected 0 misses for resident page");
        AssertTrue(bpm.GetPageHits() == 2, "Iter " + std::to_string(iter) + ": expected 2 hits for resident page");
        AssertTrue(bpm.GetMissRatio() == 0.0, "Iter " + std::to_string(iter) + ": miss ratio mismatch");
    }

    unlink("metric_test_table.bd");
    GlobalBufferPoolManager::ResetGlobalState();

    std::cout << "[PASSED] TestMetricResetAndAccuracyAcrossIterations" << std::endl;
}

// -----------------------------------------------------------------------------
// 3. Page Unpinning & Pin Leak Safety Verification across Workloads
// -----------------------------------------------------------------------------
static void TestPageUnpinningAndPinLeakSafety() {
    std::cout << "[Test 3] Running TestPageUnpinningAndPinLeakSafety..." << std::endl;

    const size_t pool_size = 4;
    GlobalBufferPoolManager bpm(pool_size);

    Schema schema = BuildTestSchema();
    CreateTestTableData(bpm, "pin_left", 10, schema);
    CreateTestTableData(bpm, "pin_right", 10, schema);

    // 3.1 Verify NestedLoopJoin pin leak safety across 30 repeated workload iterations
    auto left_scan = std::make_shared<SeqScanPlanNode>(schema, "pin_left", nullptr, BufferHint::DISCARD_QUICKLY);
    auto right_scan = std::make_shared<SeqScanPlanNode>(schema, "pin_right", nullptr, BufferHint::KEEP_HOT);

    auto join_pred = [](const Tuple& left, const Tuple& right) -> bool {
        return !left.empty() && !right.empty() && left[0] == right[0];
    };

    auto nlj_plan = std::make_shared<NestedLoopJoinPlanNode>(
        BuildJoinSchema(schema, schema), left_scan, right_scan, join_pred, BufferHint::DEFAULT
    );

    for (int iter = 0; iter < 30; ++iter) {
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

        // Stress check: Verify every single frame in the buffer pool has pin_count == 0!
        // We do this by attempting to pin pool_size distinct pages simultaneously.
        // If any frame was leaked with pin_count > 0, the pool would run out of victim frames!
        std::vector<uint32_t> test_pids(pool_size);
        std::vector<SlottedPage*> test_pages(pool_size);
        for (size_t f = 0; f < pool_size; ++f) {
            test_pages[f] = bpm.NewPage("temp_pin_check", &test_pids[f], BufferHint::DEFAULT);
            AssertTrue(test_pages[f] != nullptr, "Pin count leak detected in NLJ iter " + std::to_string(iter) + "! Frame " + std::to_string(f) + " could not be allocated.");
        }
        // Clean up test pages
        for (size_t f = 0; f < pool_size; ++f) {
            bpm.UnpinPage("temp_pin_check", test_pids[f], false);
        }
        bpm.ClearTablePages("temp_pin_check");
        unlink("temp_pin_check.bd");
    }

    // 3.2 Verify HashJoin pin leak safety across 30 repeated workload iterations
    auto hj_left_scan = std::make_shared<SeqScanPlanNode>(schema, "pin_left", nullptr, BufferHint::KEEP_HOT);
    auto hj_right_scan = std::make_shared<SeqScanPlanNode>(schema, "pin_right", nullptr, BufferHint::DISCARD_QUICKLY);
    auto hj_plan = std::make_shared<HashJoinPlanNode>(
        BuildJoinSchema(schema, schema), hj_left_scan, hj_right_scan, 0, 0, BufferHint::DEFAULT
    );

    for (int iter = 0; iter < 30; ++iter) {
        auto left_exec = std::make_unique<SeqScanExecutor>(hj_left_scan.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
        auto right_exec = std::make_unique<SeqScanExecutor>(hj_right_scan.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
        HashJoinExecutor hj_exec(hj_plan.get(), std::move(left_exec), std::move(right_exec));

        hj_exec.Init();
        Tuple t;
        RID r;
        while (hj_exec.Next(&t, &r)) {}

        // Verify all pool_size frames are unpinned
        std::vector<uint32_t> test_pids(pool_size);
        std::vector<SlottedPage*> test_pages(pool_size);
        for (size_t f = 0; f < pool_size; ++f) {
            test_pages[f] = bpm.NewPage("temp_pin_check_hj", &test_pids[f], BufferHint::DEFAULT);
            AssertTrue(test_pages[f] != nullptr, "Pin count leak detected in HJ iter " + std::to_string(iter) + "!");
        }
        for (size_t f = 0; f < pool_size; ++f) {
            bpm.UnpinPage("temp_pin_check_hj", test_pids[f], false);
        }
        bpm.ClearTablePages("temp_pin_check_hj");
        unlink("temp_pin_check_hj.bd");
    }

    unlink("pin_left.bd");
    unlink("pin_right.bd");
    GlobalBufferPoolManager::ResetGlobalState();

    std::cout << "[PASSED] TestPageUnpinningAndPinLeakSafety" << std::endl;
}

// -----------------------------------------------------------------------------
// 4. Memory Safety & Rescan Stress Under Constrained Buffer Pool Capacity
// -----------------------------------------------------------------------------
static void TestMemorySafetyUnderTinyBufferPool() {
    std::cout << "[Test 4] Running TestMemorySafetyUnderTinyBufferPool..." << std::endl;

    // Buffer pool size = 2 (minimum pool size for joining 2 tables)
    const size_t pool_size = 2;
    GlobalBufferPoolManager bpm(pool_size);

    Schema schema = BuildTestSchema();
    CreateTestTableData(bpm, "tiny_left", 8, schema);
    CreateTestTableData(bpm, "tiny_right", 8, schema);

    auto left_scan = std::make_shared<SeqScanPlanNode>(schema, "tiny_left", nullptr, BufferHint::DISCARD_QUICKLY);
    auto right_scan = std::make_shared<SeqScanPlanNode>(schema, "tiny_right", nullptr, BufferHint::KEEP_HOT);

    auto join_pred = [](const Tuple& left, const Tuple& right) -> bool {
        return !left.empty() && !right.empty() && left[0] == right[0];
    };

    auto nlj_plan = std::make_shared<NestedLoopJoinPlanNode>(
        BuildJoinSchema(schema, schema), left_scan, right_scan, join_pred, BufferHint::DEFAULT
    );

    // 40 tuples in left table -> inner table scanned 40 times -> 320 inner page reads in pool of size 2!
    auto left_exec = std::make_unique<SeqScanExecutor>(left_scan.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    auto right_exec = std::make_unique<SeqScanExecutor>(right_scan.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    NestedLoopJoinExecutor nlj_exec(nlj_plan.get(), std::move(left_exec), std::move(right_exec));

    nlj_exec.Init();
    Tuple t;
    RID r;
    size_t match_count = 0;
    while (nlj_exec.Next(&t, &r)) {
        match_count++;
    }

    AssertTrue(match_count == 40, "Nested loop join under tiny pool (size=2) returned wrong match count: " + std::to_string(match_count) + " (expected 40)");

    unlink("tiny_left.bd");
    unlink("tiny_right.bd");
    GlobalBufferPoolManager::ResetGlobalState();

    std::cout << "[PASSED] TestMemorySafetyUnderTinyBufferPool" << std::endl;
}

// -----------------------------------------------------------------------------
// 5. Multi-Threaded Concurrent Metrics & Page Access Stress Test
// -----------------------------------------------------------------------------
static void TestConcurrentMetricsAndBufferPoolAccess() {
    std::cout << "[Test 5] Running TestConcurrentMetricsAndBufferPoolAccess..." << std::endl;

    const size_t pool_size = 10;
    GlobalBufferPoolManager bpm(pool_size);

    Schema schema = BuildTestSchema();
    CreateTestTableData(bpm, "concurrent_table", 10, schema);

    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};

    const int num_threads = 4;
    std::vector<std::thread> workers;

    for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
        workers.emplace_back([&bpm, &start_flag, &stop_flag, thread_id]() {
            while (!start_flag.load()) {
                std::this_thread::yield();
            }
            uint32_t page_id = thread_id % 10;
            while (!stop_flag.load()) {
                SlottedPage* page = bpm.FetchPage("concurrent_table", page_id, (thread_id % 2 == 0) ? BufferHint::KEEP_HOT : BufferHint::DISCARD_QUICKLY);
                if (page != nullptr) {
                    bpm.UnpinPage("concurrent_table", page_id, false);
                }
                size_t hits = bpm.GetPageHits();
                size_t misses = bpm.GetPageMisses();
                size_t disk_io = bpm.GetDiskIOCount();
                double ratio = bpm.GetMissRatio();
                (void)hits; (void)misses; (void)disk_io; (void)ratio;
            }
        });
    }

    start_flag.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    bpm.ResetMetrics();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop_flag.store(true);

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    unlink("concurrent_table.bd");
    GlobalBufferPoolManager::ResetGlobalState();

    std::cout << "[PASSED] TestConcurrentMetricsAndBufferPoolAccess" << std::endl;
}

int main() {
    std::cout << "=====================================================================" << std::endl;
    std::cout << "=== Milestone 2 & Milestone 4 Challenger 2 Empirical Test Suite   ===" << std::endl;
    std::cout << "=====================================================================" << std::endl;

    TestOptimizerHintInjectionRules();
    TestMetricResetAndAccuracyAcrossIterations();
    TestPageUnpinningAndPinLeakSafety();
    TestMemorySafetyUnderTinyBufferPool();
    TestConcurrentMetricsAndBufferPoolAccess();

    std::cout << "=====================================================================" << std::endl;
    std::cout << "=== ALL Milestone 2 & Milestone 4 Challenger 2 Tests PASSED!      ===" << std::endl;
    std::cout << "=====================================================================" << std::endl;

    return 0;
}
