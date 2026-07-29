#include <iostream>
#include <memory>
#include <string>
#include <cassert>
#include <stdexcept>

#include "catalog/catalog.hpp"
#include "storage/record/schema.hpp"
#include "parser/ast.hpp"
#include "binder/binder.hpp"
#include "binder/bound_statement.hpp"
#include "optimizer/optimizer.hpp"
#include "execution/plan_node.hpp"

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

void TestSchemaGetColIdx() {
    std::cout << "--- Test 1: Schema::GetColIdx ---\n";
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("name", TypeId::VARCHAR);
    schema.AddColumn("age", TypeId::INTEGER);

    ASSERT_EQ(schema.GetColIdx("id"), 0, "Col 'id' index is 0");
    ASSERT_EQ(schema.GetColIdx("name"), 1, "Col 'name' index is 1");
    ASSERT_EQ(schema.GetColIdx("age"), 2, "Col 'age' index is 2");
    ASSERT_EQ(schema.GetColIdx("non_existent"), -1, "Missing col returns -1");
}

void TestBinderValidAndInvalidTable() {
    std::cout << "--- Test 2: Binder Table Validation ---\n";
    Catalog catalog;
    Schema user_schema;
    user_schema.AddColumn("id", TypeId::INTEGER);
    user_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", user_schema);

    Binder binder(catalog);

    // Valid table
    SelectStatement valid_stmt;
    valid_stmt.table_name = "users";
    valid_stmt.select_all = true;

    auto bound = binder.BindSelect(valid_stmt);
    ASSERT_TRUE(bound != nullptr, "Binding valid table succeeded");
    ASSERT_EQ(bound->select_schema.columns.size(), 2, "Wildcard expanded to 2 columns");

    // Invalid table -> must throw std::runtime_error
    SelectStatement invalid_stmt;
    invalid_stmt.table_name = "non_existent_table";
    invalid_stmt.select_all = true;

    bool caught_exception = false;
    try {
        binder.BindSelect(invalid_stmt);
    } catch (const std::runtime_error& e) {
        caught_exception = true;
        std::cout << "  [EXPECTED EXCEPTION] " << e.what() << std::endl;
    }
    ASSERT_TRUE(caught_exception, "Binding missing table throws std::runtime_error");
}

void TestBinderValidAndInvalidColumn() {
    std::cout << "--- Test 3: Binder Column Validation ---\n";
    Catalog catalog;
    Schema user_schema;
    user_schema.AddColumn("id", TypeId::INTEGER);
    user_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", user_schema);

    Binder binder(catalog);

    // Valid column reference
    SelectStatement valid_col_stmt;
    valid_col_stmt.table_name = "users";
    valid_col_stmt.select_list.push_back(std::make_unique<ColumnRefExpression>("users", "id"));

    auto bound_valid = binder.BindSelect(valid_col_stmt);
    ASSERT_TRUE(bound_valid != nullptr, "Binding valid column succeeded");
    ASSERT_EQ(bound_valid->select_list.size(), 1, "Select list size 1");

    // Invalid column reference -> must throw std::runtime_error
    SelectStatement invalid_col_stmt;
    invalid_col_stmt.table_name = "users";
    invalid_col_stmt.select_list.push_back(std::make_unique<ColumnRefExpression>("users", "bogus_col"));

    bool caught_exception = false;
    try {
        binder.BindSelect(invalid_col_stmt);
    } catch (const std::runtime_error& e) {
        caught_exception = true;
        std::cout << "  [EXPECTED EXCEPTION] " << e.what() << std::endl;
    }
    ASSERT_TRUE(caught_exception, "Binding missing column throws std::runtime_error");
}

void TestOptimizerAndBufferHintInjection() {
    std::cout << "--- Test 4: Optimizer AST-to-Physical Plan & BufferHint Injection ---\n";
    Catalog catalog;
    Schema users_schema;
    users_schema.AddColumn("id", TypeId::INTEGER);
    users_schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", users_schema);

    Schema orders_schema;
    orders_schema.AddColumn("user_id", TypeId::INTEGER);
    orders_schema.AddColumn("total", TypeId::INTEGER);
    catalog.CreateTable("orders", orders_schema);

    Binder binder(catalog);
    Optimizer optimizer(catalog);

    // 4a. Index Scan BufferHint::KEEP_HOT
    SelectStatement index_stmt;
    index_stmt.table_name = "users";
    index_stmt.select_all = true;
    index_stmt.where_clause = std::make_unique<BinaryOpExpression>(
        "=",
        std::make_unique<ColumnRefExpression>("users", "id"),
        std::make_unique<LiteralExpression>("42", "INTEGER")
    );

    auto bound_index_stmt = binder.BindSelect(index_stmt);
    auto index_plan = optimizer.Optimize(*bound_index_stmt);

    ASSERT_TRUE(index_plan != nullptr, "Optimizer produced physical plan for point lookup");
    // Should be ProjectionPlanNode or FilterPlanNode wrapping IndexScanPlanNode
    const AbstractPlanNode* scan_node = index_plan.get();
    while (scan_node->GetChildren().size() > 0 && scan_node->GetType() != PlanType::IndexScan) {
        scan_node = scan_node->GetChildAt(0);
    }

    ASSERT_EQ(static_cast<int>(scan_node->GetType()), static_cast<int>(PlanType::IndexScan), "Index scan rule created IndexScanPlanNode");
    ASSERT_EQ(static_cast<int>(scan_node->GetBufferHint()), static_cast<int>(BufferHint::KEEP_HOT), "IndexScanPlanNode carries BufferHint::KEEP_HOT");

    // 4b. HashJoin build side BufferHint::DISCARD_QUICKLY
    SelectStatement join_stmt;
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
    join_stmt.from_table = std::move(join_ref);
    join_stmt.select_all = true;

    auto bound_join_stmt = binder.BindSelect(join_stmt);
    auto join_plan = optimizer.Optimize(*bound_join_stmt);

    ASSERT_TRUE(join_plan != nullptr, "Optimizer produced physical plan for join query");
    const AbstractPlanNode* hj_node = join_plan.get();
    while (hj_node->GetChildren().size() > 0 && hj_node->GetType() != PlanType::HashJoin) {
        hj_node = hj_node->GetChildAt(0);
    }

    ASSERT_EQ(static_cast<int>(hj_node->GetType()), static_cast<int>(PlanType::HashJoin), "HashJoinPlanNode constructed");
    const AbstractPlanNode* left_child = hj_node->GetChildAt(0);
    ASSERT_TRUE(left_child != nullptr, "HashJoin build side (left child) exists");
    ASSERT_EQ(static_cast<int>(left_child->GetBufferHint()), static_cast<int>(BufferHint::DISCARD_QUICKLY), "HashJoin build side carries BufferHint::DISCARD_QUICKLY");
    ASSERT_EQ(static_cast<int>(hj_node->GetBufferHint()), static_cast<int>(BufferHint::DEFAULT), "HashJoin root carries BufferHint::DEFAULT");
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   M3 Binder & Optimizer Unit Test Suite               \n";
    std::cout << "========================================================\n";

    TestSchemaGetColIdx();
    TestBinderValidAndInvalidTable();
    TestBinderValidAndInvalidColumn();
    TestOptimizerAndBufferHintInjection();

    std::cout << "========================================================\n";
    std::cout << " Summary: " << g_passed << " PASSED, " << g_failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}
