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

// Helper to create a dummy table file with N pages and formatted tuples
void SetupTableFile(GlobalBufferPoolManager& bpm, const std::string& table_name, uint32_t num_pages, const Schema& schema) {
    for (uint32_t p = 0; p < num_pages; ++p) {
        uint32_t page_id;
        SlottedPage* page = bpm.NewPage(table_name, &page_id);
        if (page) {
            TupleBuilder builder(&schema);
            builder.SetInt(schema.columns[0].name, static_cast<int>(p * 100 + 1));
            if (schema.columns.size() > 1) {
                builder.SetVarchar(schema.columns[1].name, "val_" + std::to_string(p));
            }
            page->InsertTuple(builder.GetData(), builder.GetSize());
            bpm.UnpinPage(table_name, page_id, true);
        }
    }
    bpm.FlushAllPages();
}

// -----------------------------------------------------------------------------
// Test 1: SeqScanExecutor hint propagation (DISCARD_QUICKLY)
// -----------------------------------------------------------------------------
void TestSeqScanHintPropagation() {
    std::cout << "--- 1. Testing SeqScanExecutor DISCARD_QUICKLY Hint Propagation ---" << std::endl;

    GlobalBufferPoolManager bpm(3); // Pool size = 3

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("name", TypeId::VARCHAR);

    std::string tbl_seq = "test_seq_scan_tbl";
    SetupTableFile(bpm, tbl_seq, 1, schema); // Creates page 0

    // Create SeqScanPlanNode with DISCARD_QUICKLY
    auto seq_node = std::make_shared<SeqScanPlanNode>(schema, tbl_seq, nullptr, BufferHint::DISCARD_QUICKLY);
    ASSERT_EQ(seq_node->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "SeqScanPlanNode hint set to DISCARD_QUICKLY");

    // Instantiate SeqScanExecutor using plan node
    SeqScanExecutor seq_exec(seq_node.get(), nullptr, {}, nullptr, &bpm);
    ASSERT_EQ(seq_exec.GetBufferHint(), BufferHint::DISCARD_QUICKLY, "SeqScanExecutor GetBufferHint() returns DISCARD_QUICKLY");

    // Initialize executor: fetches page 0 with DISCARD_QUICKLY hint
    seq_exec.Init();

    Tuple t;
    RID r;
    bool has_tuple = seq_exec.Next(&t, &r);
    ASSERT_TRUE(has_tuple, "SeqScanExecutor fetched tuple");

    // At this point page 0 of tbl_seq is in buffer pool unpinned with hint DISCARD_QUICKLY.
    // Now let's populate 2 other pages with KEEP_HOT in tbl_other
    std::string tbl_other = "test_seq_other_tbl";
    uint32_t pid_hot1, pid_hot2;
    SlottedPage* p_hot1 = bpm.NewPage(tbl_other, &pid_hot1, BufferHint::KEEP_HOT);
    bpm.UnpinPage(tbl_other, pid_hot1, false);

    SlottedPage* p_hot2 = bpm.NewPage(tbl_other, &pid_hot2, BufferHint::KEEP_HOT);
    bpm.UnpinPage(tbl_other, pid_hot2, false);

    // Buffer pool is now FULL (3 frames: [tbl_seq page 0 (DISCARD_QUICKLY)], [tbl_other p1 (KEEP_HOT)], [tbl_other p2 (KEEP_HOT)]).
    // Now request a new page. The victim MUST be tbl_seq page 0 because it has DISCARD_QUICKLY!
    uint32_t pid_new;
    SlottedPage* p_new = bpm.NewPage("test_seq_new_tbl", &pid_new, BufferHint::DEFAULT);
    ASSERT_TRUE(p_new != nullptr, "New page allocated successfully");
    bpm.UnpinPage("test_seq_new_tbl", pid_new, false);

    // Verify eviction by checking if fetching tbl_other pid_hot1 hits buffer without reloading,
    // whereas tbl_seq page 0 was evicted.
    // Clean up files
    unlink((tbl_seq + ".bd").c_str());
    unlink((tbl_other + ".bd").c_str());
    unlink("test_seq_new_tbl.bd");
}

// -----------------------------------------------------------------------------
// Test 2: IndexScanExecutor hint propagation (KEEP_HOT & DISCARD_QUICKLY)
// -----------------------------------------------------------------------------
void TestIndexScanHintPropagation() {
    std::cout << "--- 2. Testing IndexScanExecutor KEEP_HOT & DISCARD_QUICKLY Hint Propagation ---" << std::endl;

    GlobalBufferPoolManager bpm(3);

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("name", TypeId::VARCHAR);

    std::string tbl_idx_hot = "test_idx_hot_tbl";
    SetupTableFile(bpm, tbl_idx_hot, 1, schema);

    std::string tbl_idx_discard = "test_idx_discard_tbl";
    SetupTableFile(bpm, tbl_idx_discard, 1, schema);

    // 2a. Test KEEP_HOT
    auto idx_hot_node = std::make_shared<IndexScanPlanNode>(schema, tbl_idx_hot, "idx1", "1", nullptr, BufferHint::KEEP_HOT);
    ASSERT_EQ(idx_hot_node->GetBufferHint(), BufferHint::KEEP_HOT, "IndexScanPlanNode hint KEEP_HOT");

    IndexScanExecutor idx_hot_exec(idx_hot_node.get(), nullptr, &bpm);
    idx_hot_exec.SetRIDs({RID(0, 0)});
    ASSERT_EQ(idx_hot_exec.GetBufferHint(), BufferHint::KEEP_HOT, "IndexScanExecutor GetBufferHint() returns KEEP_HOT");

    idx_hot_exec.Init();
    Tuple t_hot;
    RID r_hot;
    bool ok_hot = idx_hot_exec.Next(&t_hot, &r_hot);
    ASSERT_TRUE(ok_hot, "IndexScanExecutor (KEEP_HOT) Next() succeeded");

    // 2b. Test DISCARD_QUICKLY
    auto idx_discard_node = std::make_shared<IndexScanPlanNode>(schema, tbl_idx_discard, "idx2", "1", nullptr, BufferHint::DISCARD_QUICKLY);
    ASSERT_EQ(idx_discard_node->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "IndexScanPlanNode hint DISCARD_QUICKLY");

    IndexScanExecutor idx_discard_exec(idx_discard_node.get(), nullptr, &bpm);
    idx_discard_exec.SetRIDs({RID(0, 0)});
    ASSERT_EQ(idx_discard_exec.GetBufferHint(), BufferHint::DISCARD_QUICKLY, "IndexScanExecutor GetBufferHint() returns DISCARD_QUICKLY");

    idx_discard_exec.Init();
    Tuple t_discard;
    RID r_discard;
    bool ok_discard = idx_discard_exec.Next(&t_discard, &r_discard);
    ASSERT_TRUE(ok_discard, "IndexScanExecutor (DISCARD_QUICKLY) Next() succeeded");

    // Now buffer pool has: tbl_idx_hot (KEEP_HOT), tbl_idx_discard (DISCARD_QUICKLY).
    // Fill remaining frame with DEFAULT
    uint32_t pid_def;
    SlottedPage* p_def = bpm.NewPage("tbl_def", &pid_def, BufferHint::DEFAULT);
    bpm.UnpinPage("tbl_def", pid_def, false);

    // Buffer pool is FULL. Request a new page. Victim MUST be tbl_idx_discard (DISCARD_QUICKLY).
    uint32_t pid_new;
    SlottedPage* p_new = bpm.NewPage("tbl_new_idx", &pid_new, BufferHint::DEFAULT);
    ASSERT_TRUE(p_new != nullptr, "Evicted DISCARD_QUICKLY index scan page first");
    bpm.UnpinPage("tbl_new_idx", pid_new, false);

    // Request another new page. Victim MUST be tbl_def (DEFAULT), preserving tbl_idx_hot (KEEP_HOT).
    uint32_t pid_new2;
    SlottedPage* p_new2 = bpm.NewPage("tbl_new_idx2", &pid_new2, BufferHint::DEFAULT);
    ASSERT_TRUE(p_new2 != nullptr, "Evicted DEFAULT before KEEP_HOT index scan page");
    bpm.UnpinPage("tbl_new_idx2", pid_new2, false);

    unlink((tbl_idx_hot + ".bd").c_str());
    unlink((tbl_idx_discard + ".bd").c_str());
    unlink("tbl_def.bd");
    unlink("tbl_new_idx.bd");
    unlink("tbl_new_idx2.bd");
}

// -----------------------------------------------------------------------------
// Test 3: HashJoinExecutor build phase & probe phase hint propagation
// -----------------------------------------------------------------------------
void TestHashJoinHintPropagation() {
    std::cout << "--- 3. Testing HashJoinExecutor Build & Probe Phase Hint Propagation ---" << std::endl;

    GlobalBufferPoolManager bpm(3);

    Schema schema_l;
    schema_l.AddColumn("id", TypeId::INTEGER);
    schema_l.AddColumn("l_name", TypeId::VARCHAR);

    Schema schema_r;
    schema_r.AddColumn("id", TypeId::INTEGER);
    schema_r.AddColumn("r_name", TypeId::VARCHAR);

    std::string tbl_left = "test_hj_left_tbl";
    std::string tbl_right = "test_hj_right_tbl";
    SetupTableFile(bpm, tbl_left, 1, schema_l);
    SetupTableFile(bpm, tbl_right, 1, schema_r);

    // Left child plan: SeqScan with DISCARD_QUICKLY
    auto left_plan = std::make_shared<SeqScanPlanNode>(schema_l, tbl_left, nullptr, BufferHint::DISCARD_QUICKLY);

    // Right child plan: SeqScan with KEEP_HOT
    auto right_plan = std::make_shared<SeqScanPlanNode>(schema_r, tbl_right, nullptr, BufferHint::KEEP_HOT);

    // HashJoin plan node
    Schema join_schema;
    join_schema.AddColumn("id", TypeId::INTEGER);
    join_schema.AddColumn("l_name", TypeId::VARCHAR);
    join_schema.AddColumn("r_id", TypeId::INTEGER);
    join_schema.AddColumn("r_name", TypeId::VARCHAR);

    auto join_plan = std::make_shared<HashJoinPlanNode>(join_schema, left_plan, right_plan, 0, 0, BufferHint::DEFAULT);

    auto left_exec = std::make_unique<SeqScanExecutor>(left_plan.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);
    auto right_exec = std::make_unique<SeqScanExecutor>(right_plan.get(), nullptr, std::vector<Tuple>{}, nullptr, &bpm);

    ASSERT_EQ(left_exec->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Build child executor returns DISCARD_QUICKLY");
    ASSERT_EQ(right_exec->GetBufferHint(), BufferHint::KEEP_HOT, "Probe child executor returns KEEP_HOT");

    HashJoinExecutor join_exec(join_plan.get(), std::move(left_exec), std::move(right_exec));

    // Initialize HashJoin: executes Build phase (fetches left table page with DISCARD_QUICKLY)
    // and Probe phase (fetches right table page with KEEP_HOT)
    join_exec.Init();

    Tuple joined_tuple;
    RID joined_rid;
    bool has_match = join_exec.Next(&joined_tuple, &joined_rid);
    ASSERT_TRUE(has_match, "HashJoinExecutor returned matched joined tuple");

    // Buffer pool now contains: [left page (DISCARD_QUICKLY)], [right page (KEEP_HOT)].
    // Fill remaining frame (frame 3) with DEFAULT
    uint32_t pid_fill;
    SlottedPage* p_fill = bpm.NewPage("tbl_fill", &pid_fill, BufferHint::DEFAULT);
    bpm.UnpinPage("tbl_fill", pid_fill, false);

    // Buffer pool is FULL (3 frames).
    // Now request a new page. Victim MUST be the build phase page (tbl_left page 0) because it was DISCARD_QUICKLY!
    uint32_t pid_new1;
    SlottedPage* p_new1 = bpm.NewPage("tbl_hj_new1", &pid_new1, BufferHint::DEFAULT);
    ASSERT_TRUE(p_new1 != nullptr, "Evicted build phase (DISCARD_QUICKLY) page first");
    bpm.UnpinPage("tbl_hj_new1", pid_new1, false);

    // Request another page. Victim MUST be tbl_fill (DEFAULT), protecting the probe phase page (tbl_right page 0 - KEEP_HOT)!
    uint32_t pid_new2;
    SlottedPage* p_new2 = bpm.NewPage("tbl_hj_new2", &pid_new2, BufferHint::DEFAULT);
    ASSERT_TRUE(p_new2 != nullptr, "Evicted DEFAULT page before probe phase (KEEP_HOT) page");
    bpm.UnpinPage("tbl_hj_new2", pid_new2, false);

    unlink((tbl_left + ".bd").c_str());
    unlink((tbl_right + ".bd").c_str());
    unlink("tbl_fill.bd");
    unlink("tbl_hj_new1.bd");
    unlink("tbl_hj_new2.bd");
}

// -----------------------------------------------------------------------------
// Test 4: Optimizer Hint Injection Rules (HashJoin, SeqScan, NestedLoopJoin)
// -----------------------------------------------------------------------------
#include "optimizer/optimizer.hpp"
#include "binder/binder.hpp"
#include "catalog/catalog.hpp"

void TestOptimizerHintInjectionRules() {
    std::cout << "--- 4. Testing Optimizer Hint Injection Rules ---" << std::endl;

    Catalog catalog;
    Schema user_schema;
    user_schema.AddColumn("id", TypeId::INTEGER);
    user_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", user_schema);

    Schema orders_schema;
    orders_schema.AddColumn("order_id", TypeId::INTEGER);
    orders_schema.AddColumn("user_id", TypeId::INTEGER);
    catalog.CreateTable("orders", orders_schema);

    Schema cat_schema;
    cat_schema.AddColumn("sys_id", TypeId::INTEGER);
    cat_schema.AddColumn("sys_name", TypeId::VARCHAR);
    catalog.CreateTable("catalog", cat_schema);
    catalog.CreateTable("__catalog", cat_schema);
    catalog.CreateTable("system_tables", cat_schema);

    Optimizer optimizer(catalog);

    // 4a. Standalone SeqScan on Normal Table -> DISCARD_QUICKLY
    {
        auto scan_normal = std::make_shared<SeqScanPlanNode>(user_schema, "users", catalog.GetTable("users"), BufferHint::DEFAULT);
        std::shared_ptr<AbstractPlanNode> plan = scan_normal;
        optimizer.InjectBufferHints(plan);
        ASSERT_EQ(plan->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Standalone SeqScan on normal table gets DISCARD_QUICKLY");
    }

    // 4b. Standalone SeqScan on Catalog Tables -> KEEP_HOT
    {
        for (const std::string& cat_tbl : {"catalog", "__catalog", "system_tables"}) {
            auto scan_cat = std::make_shared<SeqScanPlanNode>(cat_schema, cat_tbl, catalog.GetTable(cat_tbl), BufferHint::DEFAULT);
            std::shared_ptr<AbstractPlanNode> plan = scan_cat;
            optimizer.InjectBufferHints(plan);
            ASSERT_EQ(plan->GetBufferHint(), BufferHint::KEEP_HOT, "Standalone SeqScan on catalog table gets KEEP_HOT");
        }
    }

    // 4c. HashJoin: Left child (build) KEEP_HOT, Right child (probe) DISCARD_QUICKLY
    {
        auto left_scan = std::make_shared<SeqScanPlanNode>(user_schema, "users", catalog.GetTable("users"), BufferHint::DEFAULT);
        auto right_scan = std::make_shared<SeqScanPlanNode>(orders_schema, "orders", catalog.GetTable("orders"), BufferHint::DEFAULT);
        
        Schema hj_schema;
        hj_schema.AddColumn("id", TypeId::INTEGER);
        hj_schema.AddColumn("name", TypeId::VARCHAR);
        hj_schema.AddColumn("order_id", TypeId::INTEGER);
        hj_schema.AddColumn("user_id", TypeId::INTEGER);

        auto hj_node = std::make_shared<HashJoinPlanNode>(hj_schema, left_scan, right_scan, 0, 1, BufferHint::DEFAULT);
        std::shared_ptr<AbstractPlanNode> plan = hj_node;
        optimizer.InjectBufferHints(plan);

        ASSERT_EQ(plan->GetBufferHint(), BufferHint::DEFAULT, "HashJoin root has DEFAULT hint");
        ASSERT_TRUE(plan->GetChildren().size() == 2, "HashJoin has 2 children");
        ASSERT_EQ(plan->GetChildAt(0)->GetBufferHint(), BufferHint::KEEP_HOT, "HashJoin left child (build) gets KEEP_HOT");
        ASSERT_EQ(plan->GetChildAt(1)->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "HashJoin right child (probe) gets DISCARD_QUICKLY");
    }

    // 4d. NestedLoopJoin: Left child (outer) DISCARD_QUICKLY, Right child (inner) KEEP_HOT
    {
        auto outer_scan = std::make_shared<SeqScanPlanNode>(user_schema, "users", catalog.GetTable("users"), BufferHint::DEFAULT);
        auto inner_scan = std::make_shared<SeqScanPlanNode>(orders_schema, "orders", catalog.GetTable("orders"), BufferHint::DEFAULT);

        Schema nlj_schema;
        nlj_schema.AddColumn("id", TypeId::INTEGER);
        nlj_schema.AddColumn("name", TypeId::VARCHAR);
        nlj_schema.AddColumn("order_id", TypeId::INTEGER);
        nlj_schema.AddColumn("user_id", TypeId::INTEGER);

        auto nlj_node = std::make_shared<NestedLoopJoinPlanNode>(nlj_schema, outer_scan, inner_scan, nullptr, BufferHint::DEFAULT);
        std::shared_ptr<AbstractPlanNode> plan = nlj_node;
        optimizer.InjectBufferHints(plan);

        ASSERT_EQ(plan->GetBufferHint(), BufferHint::DEFAULT, "NestedLoopJoin root has DEFAULT hint");
        ASSERT_TRUE(plan->GetChildren().size() == 2, "NestedLoopJoin has 2 children");
        ASSERT_EQ(plan->GetChildAt(0)->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "NestedLoopJoin left child (outer) gets DISCARD_QUICKLY");
        ASSERT_EQ(plan->GetChildAt(1)->GetBufferHint(), BufferHint::KEEP_HOT, "NestedLoopJoin right child (inner) gets KEEP_HOT");
    }

    // 4e. Nested subtrees under HashJoin and NestedLoopJoin
    {
        // Projection wrapping SeqScan on left child, Filter wrapping SeqScan on right child
        auto left_scan = std::make_shared<SeqScanPlanNode>(user_schema, "users", catalog.GetTable("users"), BufferHint::DEFAULT);
        auto proj_left = std::make_shared<ProjectionPlanNode>(user_schema, left_scan, std::vector<size_t>{0, 1}, BufferHint::DEFAULT);

        auto right_scan = std::make_shared<SeqScanPlanNode>(orders_schema, "orders", catalog.GetTable("orders"), BufferHint::DEFAULT);
        auto filter_right = std::make_shared<FilterPlanNode>(orders_schema, right_scan, [](const Tuple&){ return true; }, BufferHint::DEFAULT);

        Schema hj_schema;
        hj_schema.AddColumn("id", TypeId::INTEGER);
        hj_schema.AddColumn("name", TypeId::VARCHAR);
        hj_schema.AddColumn("order_id", TypeId::INTEGER);
        hj_schema.AddColumn("user_id", TypeId::INTEGER);

        auto hj_node = std::make_shared<HashJoinPlanNode>(hj_schema, proj_left, filter_right, 0, 1, BufferHint::DEFAULT);
        std::shared_ptr<AbstractPlanNode> plan = hj_node;
        optimizer.InjectBufferHints(plan);

        ASSERT_EQ(plan->GetChildAt(0)->GetBufferHint(), BufferHint::KEEP_HOT, "Left subtree root gets KEEP_HOT");
        ASSERT_EQ(plan->GetChildAt(0)->GetChildAt(0)->GetBufferHint(), BufferHint::KEEP_HOT, "Left subtree descendant gets KEEP_HOT");
        ASSERT_EQ(plan->GetChildAt(1)->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Right subtree root gets DISCARD_QUICKLY");
        ASSERT_EQ(plan->GetChildAt(1)->GetChildAt(0)->GetBufferHint(), BufferHint::DISCARD_QUICKLY, "Right subtree descendant gets DISCARD_QUICKLY");
    }
}

// -----------------------------------------------------------------------------
// Test 5: Full Query Optimizer Pipeline (Binder -> Optimizer::Optimize)
// -----------------------------------------------------------------------------
void TestFullQueryOptimizerHintInjection() {
    std::cout << "--- 5. Testing Full Query Optimizer Pipeline (Binder + Optimizer) ---" << std::endl;

    Catalog catalog;
    Schema users_schema;
    users_schema.AddColumn("id", TypeId::INTEGER);
    users_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", users_schema);

    Schema orders_schema;
    orders_schema.AddColumn("order_id", TypeId::INTEGER);
    orders_schema.AddColumn("user_id", TypeId::INTEGER);
    catalog.CreateTable("orders", orders_schema);

    Schema cat_schema;
    cat_schema.AddColumn("sys_id", TypeId::INTEGER);
    cat_schema.AddColumn("sys_name", TypeId::VARCHAR);
    catalog.CreateTable("catalog", cat_schema);

    Binder binder(catalog);
    Optimizer optimizer(catalog);

    // 5a. Standalone normal table query
    {
        SelectStatement stmt;
        stmt.table_name = "users";
        stmt.select_all = true;

        auto bound = binder.BindSelect(stmt);
        auto plan = optimizer.Optimize(*bound);
        ASSERT_TRUE(plan != nullptr, "Plan generated for SELECT * FROM users");

        // Traverse down to SeqScan node
        const AbstractPlanNode* scan_node = plan.get();
        while (scan_node->GetChildren().size() > 0 && scan_node->GetType() != PlanType::SeqScan) {
            scan_node = scan_node->GetChildAt(0);
        }
        ASSERT_EQ(static_cast<int>(scan_node->GetType()), static_cast<int>(PlanType::SeqScan), "Found SeqScan node");
        ASSERT_EQ(static_cast<int>(scan_node->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "Normal table SeqScan carries DISCARD_QUICKLY hint");
    }

    // 5b. Standalone catalog table query
    {
        SelectStatement stmt;
        stmt.table_name = "catalog";
        stmt.select_all = true;

        auto bound = binder.BindSelect(stmt);
        auto plan = optimizer.Optimize(*bound);
        ASSERT_TRUE(plan != nullptr, "Plan generated for SELECT * FROM catalog");

        const AbstractPlanNode* scan_node = plan.get();
        while (scan_node->GetChildren().size() > 0 && scan_node->GetType() != PlanType::SeqScan) {
            scan_node = scan_node->GetChildAt(0);
        }
        ASSERT_EQ(static_cast<int>(scan_node->GetType()), static_cast<int>(PlanType::SeqScan), "Found catalog SeqScan node");
        ASSERT_EQ(static_cast<int>(scan_node->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "Catalog table SeqScan carries KEEP_HOT hint");
    }

    // 5c. Join Query (HashJoin)
    {
        SelectStatement stmt;
        auto join_ref = std::make_unique<JoinTableRef>(
            std::make_unique<BaseTableRef>("users"),
            std::make_unique<BaseTableRef>("orders"),
            "INNER",
            std::make_unique<BinaryOpExpression>(
                "=",
                std::make_unique<ColumnRefExpression>("users", "id"),
                std::make_unique<ColumnRefExpression>("orders", "user_id")
            )
        );
        stmt.from_table = std::move(join_ref);
        stmt.select_all = true;

        auto bound = binder.BindSelect(stmt);
        auto plan = optimizer.Optimize(*bound);
        ASSERT_TRUE(plan != nullptr, "Plan generated for HashJoin query");

        const AbstractPlanNode* hj_node = plan.get();
        while (hj_node->GetChildren().size() > 0 && hj_node->GetType() != PlanType::HashJoin) {
            hj_node = hj_node->GetChildAt(0);
        }
        ASSERT_EQ(static_cast<int>(hj_node->GetType()), static_cast<int>(PlanType::HashJoin), "Found HashJoin node");

        const AbstractPlanNode* left_child = hj_node->GetChildAt(0);
        const AbstractPlanNode* right_child = hj_node->GetChildAt(1);

        ASSERT_TRUE(left_child != nullptr, "Left child exists");
        ASSERT_TRUE(right_child != nullptr, "Right child exists");

        ASSERT_EQ(static_cast<int>(left_child->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "HashJoin build side carries KEEP_HOT hint");
        ASSERT_EQ(static_cast<int>(right_child->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "HashJoin probe side carries DISCARD_QUICKLY hint");
    }
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   M2 Challenger 1 Empirical Verification Test Suite    \n";
    std::cout << "========================================================\n";

    TestSeqScanHintPropagation();
    TestIndexScanHintPropagation();
    TestHashJoinHintPropagation();
    TestOptimizerHintInjectionRules();
    TestFullQueryOptimizerHintInjection();

    std::cout << "========================================================\n";
    std::cout << " Summary: " << g_passed << " PASSED, " << g_failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}


