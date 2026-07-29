#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

#include "catalog/catalog.hpp"
#include "storage/record/schema.hpp"
#include "storage/record/tuple_builder.hpp"
#include "storage/page/buffer_pool_manager.hpp"
#include "storage/index/b_plus_tree.hpp"
#include "parser/ast.hpp"
#include "binder/binder.hpp"
#include "binder/bound_statement.hpp"
#include "optimizer/optimizer.hpp"
#include "execution/plan_node.hpp"
#include "execution/executor.hpp"

using namespace megatron;
using namespace megatron::binder;
using namespace megatron::optimizer;
using namespace megatron::execution;

static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (cond) { \
            g_passed++; \
            std::cout << "  [PASS] " << msg << std::endl; \
        } else { \
            g_failed++; \
            std::cerr << "  [FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
        } \
    } while (0)

#define ASSERT_EQ(val1, val2, msg) \
    do { \
        if ((val1) == (val2)) { \
            g_passed++; \
            std::cout << "  [PASS] " << msg << std::endl; \
        } else { \
            g_failed++; \
            std::cerr << "  [FAIL] Line " << __LINE__ << ": " << msg \
                      << " (Expected: " << static_cast<int>(val2) \
                      << ", Actual: " << static_cast<int>(val1) << ")" << std::endl; \
        } \
    } while (0)

// Helper to inspect tree hints
static void TraversalCheck(const AbstractPlanNode* node, int depth = 0) {
    if (!node) return;
    std::string indent(depth * 2, ' ');
    std::string type_str = "Unknown";
    switch (node->GetType()) {
        case PlanType::SeqScan: type_str = "SeqScan"; break;
        case PlanType::IndexScan: type_str = "IndexScan"; break;
        case PlanType::HashJoin: type_str = "HashJoin"; break;
        case PlanType::NestedLoopJoin: type_str = "NestedLoopJoin"; break;
        case PlanType::Projection: type_str = "Projection"; break;
        case PlanType::Filter: type_str = "Filter"; break;
        case PlanType::Aggregation: type_str = "Aggregation"; break;
        case PlanType::Insert: type_str = "Insert"; break;
    }
    std::string hint_str = "DEFAULT";
    if (node->GetBufferHint() == BufferHint::KEEP_HOT) hint_str = "KEEP_HOT";
    else if (node->GetBufferHint() == BufferHint::DISCARD_QUICKLY) hint_str = "DISCARD_QUICKLY";

    std::cout << indent << "- Node: " << type_str << " | Hint: " << hint_str << std::endl;
    for (const auto& child : node->GetChildren()) {
        TraversalCheck(child.get(), depth + 1);
    }
}

// 1. Point Lookup: IndexScan receives KEEP_HOT, wrappers receive DEFAULT
void TestPointLookupIndexScanHint() {
    std::cout << "--- Test 1: Point Lookup IndexScan BufferHint Verification ---\n";
    Catalog catalog;
    Schema user_schema;
    user_schema.AddColumn("id", TypeId::INTEGER);
    user_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", user_schema);

    Binder binder(catalog);
    Optimizer optimizer(catalog);

    SelectStatement stmt;
    stmt.table_name = "users";
    stmt.select_all = true;
    stmt.where_clause = std::make_unique<BinaryOpExpression>(
        "=",
        std::make_unique<ColumnRefExpression>("users", "id"),
        std::make_unique<LiteralExpression>("42", "INTEGER")
    );

    auto bound_stmt = binder.BindSelect(stmt);
    auto plan = optimizer.Optimize(*bound_stmt);

    ASSERT_TRUE(plan != nullptr, "Plan generated for point lookup");
    std::cout << "Plan structure:\n";
    TraversalCheck(plan.get());

    ASSERT_EQ(static_cast<int>(plan->GetType()), static_cast<int>(PlanType::Projection), "Root is Projection");
    ASSERT_EQ(static_cast<int>(plan->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Projection root receives DEFAULT");

    const AbstractPlanNode* filter_node = plan->GetChildAt(0);
    ASSERT_TRUE(filter_node != nullptr, "Filter node exists");
    ASSERT_EQ(static_cast<int>(filter_node->GetType()), static_cast<int>(PlanType::Filter), "Child is Filter");
    ASSERT_EQ(static_cast<int>(filter_node->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Filter node receives DEFAULT");

    const AbstractPlanNode* index_scan_node = filter_node->GetChildAt(0);
    ASSERT_TRUE(index_scan_node != nullptr, "IndexScan node exists");
    ASSERT_EQ(static_cast<int>(index_scan_node->GetType()), static_cast<int>(PlanType::IndexScan), "Leaf is IndexScan");
    ASSERT_EQ(static_cast<int>(index_scan_node->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "IndexScan node receives KEEP_HOT");
}

// 2. 2-Table HashJoin: Build side receives DISCARD_QUICKLY, Probe side receives DEFAULT, HashJoin receives DEFAULT
void TestTwoTableHashJoinHints() {
    std::cout << "--- Test 2: 2-Table HashJoin BufferHint Verification ---\n";
    Catalog catalog;
    Schema users_schema;
    users_schema.AddColumn("id", TypeId::INTEGER);
    users_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", users_schema);

    Schema orders_schema;
    orders_schema.AddColumn("user_id", TypeId::INTEGER);
    orders_schema.AddColumn("amount", TypeId::INTEGER);
    catalog.CreateTable("orders", orders_schema);

    Binder binder(catalog);
    Optimizer optimizer(catalog);

    SelectStatement stmt;
    stmt.from_table = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>("users"),
        std::make_unique<BaseTableRef>("orders"),
        "INNER",
        std::make_unique<BinaryOpExpression>(
            "=",
            std::make_unique<ColumnRefExpression>("users", "id"),
            std::make_unique<ColumnRefExpression>("orders", "user_id")
        )
    );
    stmt.select_all = true;

    auto bound_stmt = binder.BindSelect(stmt);
    auto plan = optimizer.Optimize(*bound_stmt);

    ASSERT_TRUE(plan != nullptr, "Plan generated for 2-table join");
    std::cout << "Plan structure:\n";
    TraversalCheck(plan.get());

    ASSERT_EQ(static_cast<int>(plan->GetType()), static_cast<int>(PlanType::Projection), "Root is Projection");
    ASSERT_EQ(static_cast<int>(plan->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Projection receives DEFAULT");

    const AbstractPlanNode* hj_node = plan->GetChildAt(0);
    ASSERT_TRUE(hj_node != nullptr, "HashJoin node exists");
    ASSERT_EQ(static_cast<int>(hj_node->GetType()), static_cast<int>(PlanType::HashJoin), "Node is HashJoin");
    ASSERT_EQ(static_cast<int>(hj_node->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "HashJoin root receives DEFAULT");

    const AbstractPlanNode* build_side = hj_node->GetChildAt(0);
    ASSERT_TRUE(build_side != nullptr, "Build side child exists");
    ASSERT_EQ(static_cast<int>(build_side->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "Build side receives DISCARD_QUICKLY");

    const AbstractPlanNode* probe_side = hj_node->GetChildAt(1);
    ASSERT_TRUE(probe_side != nullptr, "Probe side child exists");
    ASSERT_EQ(static_cast<int>(probe_side->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Probe side receives DEFAULT");
}

// 3. 3-Table Join (Nested HashJoin): Left subtree receives DISCARD_QUICKLY
void TestThreeTableMultiJoinHints() {
    std::cout << "--- Test 3: 3-Table Multi-Table Join BufferHint Verification ---\n";
    Catalog catalog;
    Schema users_schema;
    users_schema.AddColumn("id", TypeId::INTEGER);
    users_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", users_schema);

    Schema orders_schema;
    orders_schema.AddColumn("id", TypeId::INTEGER);
    orders_schema.AddColumn("user_id", TypeId::INTEGER);
    catalog.CreateTable("orders", orders_schema);

    Schema items_schema;
    items_schema.AddColumn("id", TypeId::INTEGER);
    items_schema.AddColumn("order_id", TypeId::INTEGER);
    catalog.CreateTable("items", items_schema);

    Binder binder(catalog);
    Optimizer optimizer(catalog);

    // ((users JOIN orders ON users.id = orders.user_id) JOIN items ON orders.id = items.order_id)
    auto join_users_orders = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>("users"),
        std::make_unique<BaseTableRef>("orders"),
        "INNER",
        std::make_unique<BinaryOpExpression>(
            "=",
            std::make_unique<ColumnRefExpression>("users", "id"),
            std::make_unique<ColumnRefExpression>("orders", "user_id")
        )
    );

    auto join_all = std::make_unique<JoinTableRef>(
        std::move(join_users_orders),
        std::make_unique<BaseTableRef>("items"),
        "INNER",
        std::make_unique<BinaryOpExpression>(
            "=",
            std::make_unique<ColumnRefExpression>("orders", "id"),
            std::make_unique<ColumnRefExpression>("items", "order_id")
        )
    );

    SelectStatement stmt;
    stmt.from_table = std::move(join_all);
    stmt.select_all = true;

    auto bound_stmt = binder.BindSelect(stmt);
    auto plan = optimizer.Optimize(*bound_stmt);

    ASSERT_TRUE(plan != nullptr, "Plan generated for 3-table join");
    std::cout << "Plan structure:\n";
    TraversalCheck(plan.get());

    const AbstractPlanNode* outer_hj = plan->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(outer_hj->GetType()), static_cast<int>(PlanType::HashJoin), "Outer join is HashJoin");
    ASSERT_EQ(static_cast<int>(outer_hj->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Outer HashJoin is DEFAULT");

    const AbstractPlanNode* inner_hj = outer_hj->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(inner_hj->GetType()), static_cast<int>(PlanType::HashJoin), "Inner join is HashJoin");
    ASSERT_EQ(static_cast<int>(inner_hj->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "Inner HashJoin is DISCARD_QUICKLY");

    const AbstractPlanNode* users_scan = inner_hj->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(users_scan->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "Users scan in build subtree is DISCARD_QUICKLY");

    const AbstractPlanNode* orders_scan = inner_hj->GetChildAt(1);
    ASSERT_EQ(static_cast<int>(orders_scan->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "Orders scan in build subtree is DISCARD_QUICKLY");

    const AbstractPlanNode* items_scan = outer_hj->GetChildAt(1);
    ASSERT_EQ(static_cast<int>(items_scan->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Items scan on probe side is DEFAULT");
}

// 4. Complex Query: Join + Where + Aggregation + Projection
void TestComplexQueryWithAggAndWhere() {
    std::cout << "--- Test 4: Complex Join + Where + GroupBy Aggregation + Projection ---\n";
    Catalog catalog;
    Schema users_schema;
    users_schema.AddColumn("id", TypeId::INTEGER);
    users_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", users_schema);

    Schema orders_schema;
    orders_schema.AddColumn("user_id", TypeId::INTEGER);
    orders_schema.AddColumn("amount", TypeId::INTEGER);
    catalog.CreateTable("orders", orders_schema);

    Binder binder(catalog);
    Optimizer optimizer(catalog);

    SelectStatement stmt;
    stmt.from_table = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>("users"),
        std::make_unique<BaseTableRef>("orders"),
        "INNER",
        std::make_unique<BinaryOpExpression>(
            "=",
            std::make_unique<ColumnRefExpression>("users", "id"),
            std::make_unique<ColumnRefExpression>("orders", "user_id")
        )
    );
    stmt.select_list.push_back(std::make_unique<ColumnRefExpression>("users", "name"));
    stmt.select_list.push_back(std::make_unique<AggregateExpression>(
        "SUM",
        std::make_unique<ColumnRefExpression>("orders", "amount")
    ));
    stmt.group_by.push_back(std::make_unique<ColumnRefExpression>("users", "name"));
    stmt.where_clause = std::make_unique<BinaryOpExpression>(
        ">",
        std::make_unique<ColumnRefExpression>("orders", "amount"),
        std::make_unique<LiteralExpression>("100", "INTEGER")
    );

    auto bound_stmt = binder.BindSelect(stmt);
    auto plan = optimizer.Optimize(*bound_stmt);

    ASSERT_TRUE(plan != nullptr, "Plan generated for complex query");
    std::cout << "Plan structure:\n";
    TraversalCheck(plan.get());

    ASSERT_EQ(static_cast<int>(plan->GetType()), static_cast<int>(PlanType::Projection), "Root is Projection");
    ASSERT_EQ(static_cast<int>(plan->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Projection is DEFAULT");

    const AbstractPlanNode* agg_node = plan->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(agg_node->GetType()), static_cast<int>(PlanType::Aggregation), "Child of Projection is Aggregation");
    ASSERT_EQ(static_cast<int>(agg_node->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Aggregation is DEFAULT");

    const AbstractPlanNode* filter_node = agg_node->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(filter_node->GetType()), static_cast<int>(PlanType::Filter), "Child of Aggregation is Filter");
    ASSERT_EQ(static_cast<int>(filter_node->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "Filter is DEFAULT");

    const AbstractPlanNode* hj_node = filter_node->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(hj_node->GetType()), static_cast<int>(PlanType::HashJoin), "Child of Filter is HashJoin");
    ASSERT_EQ(static_cast<int>(hj_node->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "HashJoin is DEFAULT");

    const AbstractPlanNode* left_scan = hj_node->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(left_scan->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "HashJoin left scan is DISCARD_QUICKLY");

    const AbstractPlanNode* right_scan = hj_node->GetChildAt(1);
    ASSERT_EQ(static_cast<int>(right_scan->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "HashJoin right scan is DEFAULT");
}

// 5. Nested Loop Join (Non-equi join): Outer receives DISCARD_QUICKLY, Inner receives KEEP_HOT
void TestNestedLoopJoinHints() {
    std::cout << "--- Test 5: Non-Equi Join (NestedLoopJoin) BufferHint Verification ---\n";
    Catalog catalog;
    Schema users_schema;
    users_schema.AddColumn("id", TypeId::INTEGER);
    catalog.CreateTable("users", users_schema);

    Schema orders_schema;
    orders_schema.AddColumn("user_id", TypeId::INTEGER);
    catalog.CreateTable("orders", orders_schema);

    Binder binder(catalog);
    Optimizer optimizer(catalog);

    SelectStatement stmt;
    stmt.from_table = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>("users"),
        std::make_unique<BaseTableRef>("orders"),
        "INNER",
        std::make_unique<BinaryOpExpression>(
            ">",
            std::make_unique<ColumnRefExpression>("users", "id"),
            std::make_unique<ColumnRefExpression>("orders", "user_id")
        )
    );
    stmt.select_all = true;

    auto bound_stmt = binder.BindSelect(stmt);
    auto plan = optimizer.Optimize(*bound_stmt);

    ASSERT_TRUE(plan != nullptr, "Plan generated for NLJ query");
    std::cout << "Plan structure:\n";
    TraversalCheck(plan.get());

    const AbstractPlanNode* nlj_node = plan->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(nlj_node->GetType()), static_cast<int>(PlanType::NestedLoopJoin), "Node is NestedLoopJoin");
    ASSERT_EQ(static_cast<int>(nlj_node->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "NLJ root is DEFAULT");

    const AbstractPlanNode* outer_child = nlj_node->GetChildAt(0);
    ASSERT_EQ(static_cast<int>(outer_child->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "NLJ outer child is DISCARD_QUICKLY");

    const AbstractPlanNode* inner_child = nlj_node->GetChildAt(1);
    ASSERT_EQ(static_cast<int>(inner_child->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "NLJ inner child is KEEP_HOT");
}

// 6. Memory Pressure Execution Test: HashJoin + BPM with pool_size = 3
void TestExecutionUnderMemoryPressure() {
    std::cout << "--- Test 6: Join Execution under Buffer Pool Memory Pressure ---\n";

    const std::string table_users = "m3_stress_users";
    const std::string table_orders = "m3_stress_orders";
    unlink((table_users + ".bd").c_str());
    unlink((table_orders + ".bd").c_str());

    // Create BPM with tiny pool size (3 pages)
    GlobalBufferPoolManager bpm(3);

    Schema users_schema;
    users_schema.AddColumn("id", TypeId::INTEGER);
    users_schema.AddColumn("name", TypeId::VARCHAR);

    Schema orders_schema;
    orders_schema.AddColumn("user_id", TypeId::INTEGER);
    orders_schema.AddColumn("amount", TypeId::INTEGER);

    // Populate users table across multiple pages (e.g. 50 users)
    {
        uint32_t current_page_id = 0;
        SlottedPage* page = bpm.NewPage(table_users, &current_page_id, BufferHint::DEFAULT);
        for (int i = 1; i <= 50; ++i) {
            TupleBuilder b(&users_schema);
            b.SetInt("id", i);
            b.SetVarchar("name", "User_" + std::to_string(i));
            if (!page->InsertTuple(b.GetData(), b.GetSize())) {
                bpm.UnpinPage(table_users, current_page_id, true);
                page = bpm.NewPage(table_users, &current_page_id, BufferHint::DEFAULT);
                ASSERT_TRUE(page != nullptr, "New page allocation succeeded");
                bool ok = page->InsertTuple(b.GetData(), b.GetSize());
                ASSERT_TRUE(ok, "Insert tuple on new page succeeded");
            }
        }
        bpm.UnpinPage(table_users, current_page_id, true);
    }

    // Populate orders table across multiple pages (e.g. 100 orders, 2 per user)
    {
        uint32_t current_page_id = 0;
        SlottedPage* page = bpm.NewPage(table_orders, &current_page_id, BufferHint::DEFAULT);
        for (int i = 1; i <= 100; ++i) {
            TupleBuilder b(&orders_schema);
            int uid = ((i - 1) % 50) + 1;
            b.SetInt("user_id", uid);
            b.SetInt("amount", i * 10);
            if (!page->InsertTuple(b.GetData(), b.GetSize())) {
                bpm.UnpinPage(table_orders, current_page_id, true);
                page = bpm.NewPage(table_orders, &current_page_id, BufferHint::DEFAULT);
                ASSERT_TRUE(page != nullptr, "New page allocation succeeded");
                bool ok = page->InsertTuple(b.GetData(), b.GetSize());
                ASSERT_TRUE(ok, "Insert tuple on new page succeeded");
            }
        }
        bpm.UnpinPage(table_orders, current_page_id, true);
    }

    bpm.FlushAllPages();

    // Set up Catalog, Binder, Optimizer
    Catalog catalog;
    catalog.CreateTable(table_users, users_schema);
    catalog.CreateTable(table_orders, orders_schema);

    Binder binder(catalog);
    Optimizer optimizer(catalog);

    SelectStatement stmt;
    stmt.from_table = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>(table_users),
        std::make_unique<BaseTableRef>(table_orders),
        "INNER",
        std::make_unique<BinaryOpExpression>(
            "=",
            std::make_unique<ColumnRefExpression>(table_users, "id"),
            std::make_unique<ColumnRefExpression>(table_orders, "user_id")
        )
    );
    stmt.select_all = true;

    auto bound_stmt = binder.BindSelect(stmt);
    auto plan = optimizer.Optimize(*bound_stmt);
    ASSERT_TRUE(plan != nullptr, "Optimizer created physical join plan");

    // Construct volcano physical execution tree manually with BPM
    const AbstractPlanNode* proj_plan = plan.get();
    const AbstractPlanNode* hj_plan = proj_plan->GetChildAt(0);
    const SeqScanPlanNode* build_scan_plan = static_cast<const SeqScanPlanNode*>(hj_plan->GetChildAt(0));
    const SeqScanPlanNode* probe_scan_plan = static_cast<const SeqScanPlanNode*>(hj_plan->GetChildAt(1));

    const TableMetadata* meta_users = catalog.GetTable(table_users);
    const TableMetadata* meta_orders = catalog.GetTable(table_orders);

    auto build_exec = std::make_unique<SeqScanExecutor>(build_scan_plan, meta_users, std::vector<Tuple>{}, nullptr, &bpm);
    auto probe_exec = std::make_unique<SeqScanExecutor>(probe_scan_plan, meta_orders, std::vector<Tuple>{}, nullptr, &bpm);

    auto hj_exec = std::make_unique<HashJoinExecutor>(static_cast<const HashJoinPlanNode*>(hj_plan), std::move(build_exec), std::move(probe_exec));
    auto proj_exec = std::make_unique<ProjectionExecutor>(static_cast<const ProjectionPlanNode*>(proj_plan), std::move(hj_exec));

    proj_exec->Init();
    Tuple row;
    RID rid;
    int row_count = 0;
    while (proj_exec->Next(&row, &rid)) {
        row_count++;
    }

    std::cout << "Executed join query under memory pressure (pool_size=3). Output rows: " << row_count << std::endl;
    ASSERT_EQ(row_count, 100, "Join query under memory pressure returned exactly 100 joined rows");

    // Flush and cleanup
    bpm.FlushAllPages();
    unlink((table_users + ".bd").c_str());
    unlink((table_orders + ".bd").c_str());
}

// 7. Point Lookup IndexScan Execution with BPM under memory pressure
void TestIndexScanExecutionUnderMemoryPressure() {
    std::cout << "--- Test 7: IndexScan Execution under Buffer Pool Memory Pressure ---\n";

    const std::string table_name = "m3_idx_table";
    const std::string idx_name = "m3_idx_bplus.db";
    unlink((table_name + ".bd").c_str());
    unlink(idx_name.c_str());

    GlobalBufferPoolManager bpm(2); // extremely small buffer pool size

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("data", TypeId::VARCHAR);

    uint32_t p_id = 0;
    SlottedPage* page = bpm.NewPage(table_name, &p_id, BufferHint::DEFAULT);
    TupleBuilder b(&schema);
    b.SetInt("id", 999);
    b.SetVarchar("data", "EmpiricalStressData");
    uint16_t slot = page->GetHeader()->num_slots;
    page->InsertTuple(b.GetData(), b.GetSize());
    bpm.UnpinPage(table_name, p_id, true);
    bpm.FlushAllPages();

    // Create B+ tree entry for key 999
    {
        BPlusTreeDisk tree(idx_name.c_str());
        int64_t packed_rid = (static_cast<int64_t>(p_id) << 16) | slot;
        tree.Insert(999, packed_rid);
    }

    TableMetadata meta;
    meta.table_name = table_name;
    meta.schema = schema;

    auto plan = std::make_shared<IndexScanPlanNode>(schema, table_name, idx_name, "999", &meta, BufferHint::KEEP_HOT);
    IndexScanExecutor exec(plan.get(), &meta, &bpm);
    exec.Init();

    Tuple t;
    RID r;
    ASSERT_TRUE(exec.Next(&t, &r), "IndexScan found row via B+ tree and BPM");
    ASSERT_EQ(static_cast<int>(t.size()), 2, "Tuple has 2 columns");
    ASSERT_TRUE(t[0] == "999" && t[1] == "EmpiricalStressData", "Data retrieved matches expected");

    bpm.FlushAllPages();
    unlink((table_name + ".bd").c_str());
    unlink(idx_name.c_str());
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   M3 Challenger 2 Optimizer BufferHint Stress Suite    \n";
    std::cout << "========================================================\n";

    TestPointLookupIndexScanHint();
    TestTwoTableHashJoinHints();
    TestThreeTableMultiJoinHints();
    TestComplexQueryWithAggAndWhere();
    TestNestedLoopJoinHints();
    TestExecutionUnderMemoryPressure();
    TestIndexScanExecutionUnderMemoryPressure();

    std::cout << "========================================================\n";
    std::cout << " Summary: " << g_passed << " PASSED, " << g_failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}
