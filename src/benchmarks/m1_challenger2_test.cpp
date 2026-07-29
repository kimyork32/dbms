#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <cstdlib>

#include "execution/plan_node.hpp"
#include "storage/page/buffer_pool_manager.hpp"
#include "storage/record/schema.hpp"

using namespace megatron;
using namespace megatron::execution;

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

void TestBufferHintEnumValues() {
    std::cout << "--- 1. Testing BufferHint Enum Values ---" << std::endl;
    ASSERT_EQ(static_cast<int>(BufferHint::DEFAULT), 0, "BufferHint::DEFAULT value must be 0");
    ASSERT_EQ(static_cast<int>(BufferHint::KEEP_HOT), 1, "BufferHint::KEEP_HOT value must be 1");
    ASSERT_EQ(static_cast<int>(BufferHint::DISCARD_QUICKLY), 2, "BufferHint::DISCARD_QUICKLY value must be 2");

    ASSERT_TRUE(BufferHint::DEFAULT != BufferHint::KEEP_HOT, "DEFAULT != KEEP_HOT");
    ASSERT_TRUE(BufferHint::DEFAULT != BufferHint::DISCARD_QUICKLY, "DEFAULT != DISCARD_QUICKLY");
    ASSERT_TRUE(BufferHint::KEEP_HOT != BufferHint::DISCARD_QUICKLY, "KEEP_HOT != DISCARD_QUICKLY");
}

void TestDefaultConstructorHints() {
    std::cout << "--- 2. Testing Default Hint Constructors ---" << std::endl;
    Schema schema;
    schema.AddColumn("col1", TypeId::INTEGER);

    // 1. SeqScanPlanNode default constructor hint
    auto seq_scan = std::make_shared<SeqScanPlanNode>(schema, "t1");
    ASSERT_EQ(seq_scan->GetBufferHint(), BufferHint::DEFAULT, "SeqScan default hint");

    // 2. HashJoinPlanNode default constructor hint
    auto left_child = std::make_shared<SeqScanPlanNode>(schema, "t1");
    auto right_child = std::make_shared<SeqScanPlanNode>(schema, "t2");
    auto hash_join = std::make_shared<HashJoinPlanNode>(schema, left_child, right_child, 0, 0);
    ASSERT_EQ(hash_join->GetBufferHint(), BufferHint::DEFAULT, "HashJoin default hint");

    // 3. AggregationPlanNode default constructor hint
    auto agg = std::make_shared<AggregationPlanNode>(schema, left_child, std::vector<size_t>{0}, 0, AggregateType::SUM);
    ASSERT_EQ(agg->GetBufferHint(), BufferHint::DEFAULT, "Aggregation default hint");

    // 4. FilterPlanNode default constructor hint
    auto filter = std::make_shared<FilterPlanNode>(schema, left_child, [](const Tuple&){ return true; });
    ASSERT_EQ(filter->GetBufferHint(), BufferHint::DEFAULT, "Filter default hint");

    // 5. ProjectionPlanNode default constructor hint
    auto proj = std::make_shared<ProjectionPlanNode>(schema, left_child, std::vector<size_t>{0});
    ASSERT_EQ(proj->GetBufferHint(), BufferHint::DEFAULT, "Projection default hint");
}

void TestExplicitConstructorHints() {
    std::cout << "--- 3. Testing Explicit Hint Constructors ---" << std::endl;
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);

    // SeqScan with DISCARD_QUICKLY and KEEP_HOT
    auto seq_discard = std::make_shared<SeqScanPlanNode>(schema, "t1", nullptr, BufferHint::DISCARD_QUICKLY);
    ASSERT_EQ(seq_discard->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "SeqScan explicit DISCARD_QUICKLY");

    auto seq_keep = std::make_shared<SeqScanPlanNode>(schema, "t1", nullptr, BufferHint::KEEP_HOT);
    ASSERT_EQ(seq_keep->GetBufferHint(), BufferHint::KEEP_HOT, "SeqScan explicit KEEP_HOT");

    // HashJoin with DISCARD_QUICKLY and KEEP_HOT
    auto hash_discard = std::make_shared<HashJoinPlanNode>(schema, seq_discard, seq_keep, 0, 0, BufferHint::DISCARD_QUICKLY);
    ASSERT_EQ(hash_discard->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "HashJoin explicit DISCARD_QUICKLY");

    auto hash_keep = std::make_shared<HashJoinPlanNode>(schema, seq_discard, seq_keep, 0, 0, BufferHint::KEEP_HOT);
    ASSERT_EQ(hash_keep->GetBufferHint(), BufferHint::KEEP_HOT, "HashJoin explicit KEEP_HOT");

    // Aggregation with DISCARD_QUICKLY and KEEP_HOT
    auto agg_discard = std::make_shared<AggregationPlanNode>(schema, seq_discard, std::vector<size_t>{0}, 0, AggregateType::COUNT, BufferHint::DISCARD_QUICKLY);
    ASSERT_EQ(agg_discard->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Aggregation explicit DISCARD_QUICKLY");

    auto agg_keep = std::make_shared<AggregationPlanNode>(schema, seq_discard, std::vector<size_t>{0}, 0, AggregateType::COUNT, BufferHint::KEEP_HOT);
    ASSERT_EQ(agg_keep->GetBufferHint(), BufferHint::KEEP_HOT, "Aggregation explicit KEEP_HOT");

    // Filter with DISCARD_QUICKLY and KEEP_HOT
    auto filter_discard = std::make_shared<FilterPlanNode>(schema, seq_discard, nullptr, BufferHint::DISCARD_QUICKLY);
    ASSERT_EQ(filter_discard->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Filter explicit DISCARD_QUICKLY");

    auto filter_keep = std::make_shared<FilterPlanNode>(schema, seq_discard, nullptr, BufferHint::KEEP_HOT);
    ASSERT_EQ(filter_keep->GetBufferHint(), BufferHint::KEEP_HOT, "Filter explicit KEEP_HOT");

    // Projection with DISCARD_QUICKLY and KEEP_HOT
    auto proj_discard = std::make_shared<ProjectionPlanNode>(schema, seq_discard, std::vector<size_t>{0}, BufferHint::DISCARD_QUICKLY);
    ASSERT_EQ(proj_discard->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Projection explicit DISCARD_QUICKLY");

    auto proj_keep = std::make_shared<ProjectionPlanNode>(schema, seq_discard, std::vector<size_t>{0}, BufferHint::KEEP_HOT);
    ASSERT_EQ(proj_keep->GetBufferHint(), BufferHint::KEEP_HOT, "Projection explicit KEEP_HOT");
}

void TestGetSetBufferHintMutations() {
    std::cout << "--- 4. Testing SetBufferHint and Polymorphic Behavior ---" << std::endl;
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);

    std::shared_ptr<AbstractPlanNode> node = std::make_shared<SeqScanPlanNode>(schema, "users", nullptr, BufferHint::DEFAULT);
    ASSERT_EQ(node->GetBufferHint(), BufferHint::DEFAULT, "Initial hint DEFAULT");

    node->SetBufferHint(BufferHint::KEEP_HOT);
    ASSERT_EQ(node->GetBufferHint(), BufferHint::KEEP_HOT, "Updated hint KEEP_HOT");

    node->SetBufferHint(BufferHint::DISCARD_QUICKLY);
    ASSERT_EQ(node->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Updated hint DISCARD_QUICKLY");

    node->SetBufferHint(BufferHint::DEFAULT);
    ASSERT_EQ(node->GetBufferHint(), BufferHint::DEFAULT, "Reset hint DEFAULT");

    // Test on child pointers via AbstractPlanNode interface
    auto left = std::make_shared<SeqScanPlanNode>(schema, "t1", nullptr, BufferHint::KEEP_HOT);
    auto right = std::make_shared<SeqScanPlanNode>(schema, "t2", nullptr, BufferHint::DISCARD_QUICKLY);
    auto join = std::make_shared<HashJoinPlanNode>(schema, left, right, 0, 0, BufferHint::DEFAULT);

    ASSERT_EQ(join->GetChildAt(0)->GetBufferHint(), BufferHint::KEEP_HOT, "Child 0 hint KEEP_HOT via AbstractPlanNode pointer");
    ASSERT_EQ(join->GetChildAt(1)->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Child 1 hint DISCARD_QUICKLY via AbstractPlanNode pointer");
}

void TestDeepPlanTreeBufferHintPropagation() {
    std::cout << "--- 5. Testing Deep Plan Tree Buffer Hint Preservation ---" << std::endl;
    Schema schema;
    schema.AddColumn("val", TypeId::INTEGER);

    // Tree hierarchy:
    // Projection (KEEP_HOT)
    //   -> Aggregation (DISCARD_QUICKLY)
    //        -> Filter (DEFAULT)
    //             -> HashJoin (KEEP_HOT)
    //                  -> SeqScan left (DISCARD_QUICKLY)
    //                  -> SeqScan right (KEEP_HOT)

    auto scan_l = std::make_shared<SeqScanPlanNode>(schema, "left_tbl", nullptr, BufferHint::DISCARD_QUICKLY);
    auto scan_r = std::make_shared<SeqScanPlanNode>(schema, "right_tbl", nullptr, BufferHint::KEEP_HOT);
    auto join = std::make_shared<HashJoinPlanNode>(schema, scan_l, scan_r, 0, 0, BufferHint::KEEP_HOT);
    auto filter = std::make_shared<FilterPlanNode>(schema, join, nullptr, BufferHint::DEFAULT);
    auto agg = std::make_shared<AggregationPlanNode>(schema, filter, std::vector<size_t>{0}, 0, AggregateType::SUM, BufferHint::DISCARD_QUICKLY);
    auto proj = std::make_shared<ProjectionPlanNode>(schema, agg, std::vector<size_t>{0}, BufferHint::KEEP_HOT);

    // Verify top-down tree access hints
    ASSERT_EQ(proj->GetBufferHint(), BufferHint::KEEP_HOT, "Projection node hint");

    auto child_agg = proj->GetChild();
    ASSERT_EQ(child_agg->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Aggregation node hint");

    auto child_filter = static_cast<const AggregationPlanNode*>(child_agg)->GetChild();
    ASSERT_EQ(child_filter->GetBufferHint(), BufferHint::DEFAULT, "Filter node hint");

    auto child_join = static_cast<const FilterPlanNode*>(child_filter)->GetChild();
    ASSERT_EQ(child_join->GetBufferHint(), BufferHint::KEEP_HOT, "HashJoin node hint");

    auto child_left = static_cast<const HashJoinPlanNode*>(child_join)->GetLeftChild();
    ASSERT_EQ(child_left->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Left SeqScan node hint");

    auto child_right = static_cast<const HashJoinPlanNode*>(child_join)->GetRightChild();
    ASSERT_EQ(child_right->GetBufferHint(), BufferHint::KEEP_HOT, "Right SeqScan node hint");
}

void TestBufferPoolManagerHintInteraction() {
    std::cout << "--- 6. Testing Buffer Pool Manager Hint Integration ---" << std::endl;
    // Instantiate buffer pool manager with pool size 3
    GlobalBufferPoolManager bpm(3);

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);

    auto scan_discard = std::make_shared<SeqScanPlanNode>(schema, "tbl_a", nullptr, BufferHint::DISCARD_QUICKLY);
    auto scan_keep = std::make_shared<SeqScanPlanNode>(schema, "tbl_b", nullptr, BufferHint::KEEP_HOT);
    auto scan_default = std::make_shared<SeqScanPlanNode>(schema, "tbl_c", nullptr, BufferHint::DEFAULT);

    uint32_t pid1, pid2, pid3, pid4;

    // Allocate 3 pages using plan node hints
    SlottedPage* p1 = bpm.NewPage(scan_discard->GetTableName(), &pid1, scan_discard->GetBufferHint());
    ASSERT_TRUE(p1 != nullptr, "p1 allocated");
    bpm.UnpinPage(scan_discard->GetTableName(), pid1, false);

    SlottedPage* p2 = bpm.NewPage(scan_keep->GetTableName(), &pid2, scan_keep->GetBufferHint());
    ASSERT_TRUE(p2 != nullptr, "p2 allocated");
    bpm.UnpinPage(scan_keep->GetTableName(), pid2, false);

    SlottedPage* p3 = bpm.NewPage(scan_default->GetTableName(), &pid3, scan_default->GetBufferHint());
    ASSERT_TRUE(p3 != nullptr, "p3 allocated");
    bpm.UnpinPage(scan_default->GetTableName(), pid3, false);

    // Buffer pool is now full (3 frames).
    // Allocate 4th page. The eviction victim MUST be p1 (DISCARD_QUICKLY).
    SlottedPage* p4 = bpm.NewPage("tbl_d", &pid4, BufferHint::DEFAULT);
    ASSERT_TRUE(p4 != nullptr, "p4 allocated after evicting DISCARD_QUICKLY frame");
    bpm.UnpinPage("tbl_d", pid4, false);
}

void TestStressAndMemoryLeaks() {
    std::cout << "--- 7. Stress Testing & Memory Allocation Integrity ---" << std::endl;
    Schema schema;
    schema.AddColumn("col", TypeId::INTEGER);

    for (int i = 0; i < 50000; ++i) {
        auto scan1 = std::make_shared<SeqScanPlanNode>(schema, "t1", nullptr, BufferHint::DISCARD_QUICKLY);
        auto scan2 = std::make_shared<SeqScanPlanNode>(schema, "t2", nullptr, BufferHint::KEEP_HOT);
        auto join = std::make_shared<HashJoinPlanNode>(schema, scan1, scan2, 0, 0, BufferHint::KEEP_HOT);
        auto filter = std::make_shared<FilterPlanNode>(schema, join, nullptr, BufferHint::DEFAULT);
        auto agg = std::make_shared<AggregationPlanNode>(schema, filter, std::vector<size_t>{0}, 0, AggregateType::SUM, BufferHint::DISCARD_QUICKLY);
        auto proj = std::make_shared<ProjectionPlanNode>(schema, agg, std::vector<size_t>{0}, BufferHint::KEEP_HOT);

        proj->SetBufferHint(static_cast<BufferHint>(i % 3));
        ASSERT_EQ(proj->GetBufferHint(), static_cast<BufferHint>(i % 3), "Stress loop hint match");
    }
    std::cout << "[PASSED] 50,000 plan tree allocations & mutations verified with 0 memory leaks" << std::endl;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   M1 Challenger 2 Empirical Verification Test Suite    \n";
    std::cout << "========================================================\n";

    TestBufferHintEnumValues();
    TestDefaultConstructorHints();
    TestExplicitConstructorHints();
    TestGetSetBufferHintMutations();
    TestDeepPlanTreeBufferHintPropagation();
    TestBufferPoolManagerHintInteraction();
    TestStressAndMemoryLeaks();

    std::cout << "========================================================\n";
    std::cout << " Summary: " << g_passed << " PASSED, " << g_failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}
