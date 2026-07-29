#include <iostream>
#include <memory>
#include <string>
#include <cassert>
#include <stdexcept>
#include <vector>

#include "catalog/catalog.hpp"
#include "storage/record/schema.hpp"
#include "parser/ast.hpp"
#include "binder/binder.hpp"
#include "binder/bound_statement.hpp"

using namespace megatron;
using namespace megatron::binder;

static int g_passed = 0;
static int g_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            g_passed++; \
            std::cout << "  [PASS] " << msg << std::endl; \
        } else { \
            g_failed++; \
            std::cerr << "  [FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
        } \
    } while (0)

#define TEST_ASSERT_THROWS(expr, expected_exception, msg) \
    do { \
        bool caught = false; \
        try { \
            expr; \
        } catch (const expected_exception& e) { \
            caught = true; \
            std::cout << "  [PASS] Caught expected exception for (" << msg << "): " << e.what() << std::endl; \
        } catch (...) { \
            std::cerr << "  [FAIL] Caught unexpected exception type for (" << msg << ")" << std::endl; \
        } \
        if (!caught) { \
            g_failed++; \
            std::cerr << "  [FAIL] Line " << __LINE__ << ": Expected exception was not thrown for (" << msg << ")" << std::endl; \
        } else { \
            g_passed++; \
        } \
    } while (0)

void TestEmptyCatalogConditions() {
    std::cout << "=== Stress Test 1: Empty Catalog Conditions ===\n";
    Catalog catalog;
    Binder binder(catalog);

    TEST_ASSERT(catalog.GetTable("users") == nullptr, "GetTable on empty catalog returns nullptr");
    TEST_ASSERT(catalog.TableExists("users") == false, "TableExists on empty catalog returns false");

    // 1a. Select from non-existent table via table_name
    SelectStatement stmt1;
    stmt1.table_name = "users";
    stmt1.select_all = true;
    TEST_ASSERT_THROWS(binder.BindSelect(stmt1), std::runtime_error, "Select from non-existent table");

    // 1b. Select from non-existent table via from_table
    SelectStatement stmt2;
    stmt2.from_table = std::make_unique<BaseTableRef>("users");
    stmt2.select_all = true;
    TEST_ASSERT_THROWS(binder.BindSelect(stmt2), std::runtime_error, "Select from non-existent BaseTableRef");

    // 1c. Select with no table specified at all
    SelectStatement stmt3;
    stmt3.from_table = nullptr;
    stmt3.table_name = "";
    TEST_ASSERT_THROWS(binder.BindSelect(stmt3), std::runtime_error, "No table specified in SelectStatement");
}

void TestInvalidTables() {
    std::cout << "=== Stress Test 2: Invalid Table References ===\n";
    Catalog catalog;
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    catalog.CreateTable("users", schema);
    catalog.CreateTable("orders", schema);

    Binder binder(catalog);

    // 2a. Non-existent table in single FROM
    SelectStatement stmt1;
    stmt1.table_name = "products";
    TEST_ASSERT_THROWS(binder.BindSelect(stmt1), std::runtime_error, "Table 'products' does not exist");

    // 2b. JOIN with invalid right table
    SelectStatement stmt2;
    stmt2.from_table = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>("users"),
        std::make_unique<BaseTableRef>("invalid_table"),
        "INNER",
        nullptr
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt2), std::runtime_error, "Join with invalid right table");

    // 2c. JOIN with invalid left table
    SelectStatement stmt3;
    stmt3.from_table = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>("invalid_table"),
        std::make_unique<BaseTableRef>("orders"),
        "INNER",
        nullptr
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt3), std::runtime_error, "Join with invalid left table");

    // 2d. JOIN with null left reference
    SelectStatement stmt4;
    stmt4.from_table = std::make_unique<JoinTableRef>(
        nullptr,
        std::make_unique<BaseTableRef>("orders"),
        "INNER",
        nullptr
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt4), std::runtime_error, "Join with null left TableRef");

    // 2e. JOIN with null right reference
    SelectStatement stmt5;
    stmt5.from_table = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>("users"),
        nullptr,
        "INNER",
        nullptr
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt5), std::runtime_error, "Join with null right TableRef");
}

void TestInvalidColumns() {
    std::cout << "=== Stress Test 3: Invalid Column References ===\n";
    Catalog catalog;
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("name", TypeId::VARCHAR);
    catalog.CreateTable("users", schema);

    Binder binder(catalog);

    // 3a. Invalid column in select list
    SelectStatement stmt1;
    stmt1.table_name = "users";
    stmt1.select_list.push_back(std::make_unique<ColumnRefExpression>("users", "bogus_col"));
    TEST_ASSERT_THROWS(binder.BindSelect(stmt1), std::runtime_error, "Invalid column in select list");

    // 3b. Invalid column in WHERE clause (left operand)
    SelectStatement stmt2;
    stmt2.table_name = "users";
    stmt2.select_all = true;
    stmt2.where_clause = std::make_unique<BinaryOpExpression>(
        "=",
        std::make_unique<ColumnRefExpression>("users", "invalid_where_left"),
        std::make_unique<LiteralExpression>("1", "INTEGER")
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt2), std::runtime_error, "Invalid left col in WHERE");

    // 3c. Invalid column in WHERE clause (right operand)
    SelectStatement stmt3;
    stmt3.table_name = "users";
    stmt3.select_all = true;
    stmt3.where_clause = std::make_unique<BinaryOpExpression>(
        "=",
        std::make_unique<ColumnRefExpression>("users", "id"),
        std::make_unique<ColumnRefExpression>("users", "invalid_where_right")
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt3), std::runtime_error, "Invalid right col in WHERE");

    // 3d. Invalid column in GROUP BY
    SelectStatement stmt4;
    stmt4.table_name = "users";
    stmt4.select_all = true;
    stmt4.group_by.push_back(std::make_unique<ColumnRefExpression>("users", "bad_group_col"));
    TEST_ASSERT_THROWS(binder.BindSelect(stmt4), std::runtime_error, "Invalid col in GROUP BY");

    // 3e. Invalid column in HAVING
    SelectStatement stmt5;
    stmt5.table_name = "users";
    stmt5.select_all = true;
    stmt5.having_clause = std::make_unique<BinaryOpExpression>(
        ">",
        std::make_unique<ColumnRefExpression>("users", "bad_having_col"),
        std::make_unique<LiteralExpression>("0", "INTEGER")
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt5), std::runtime_error, "Invalid col in HAVING");

    // 3f. Invalid column inside Aggregate SUM(bad_col)
    SelectStatement stmt6;
    stmt6.table_name = "users";
    stmt6.select_list.push_back(std::make_unique<AggregateExpression>(
        "SUM",
        std::make_unique<ColumnRefExpression>("users", "bad_agg_col"),
        false
    ));
    TEST_ASSERT_THROWS(binder.BindSelect(stmt6), std::runtime_error, "Invalid col in Aggregate function");

    // 3g. BinaryOp with null left child
    SelectStatement stmt7;
    stmt7.table_name = "users";
    stmt7.where_clause = std::make_unique<BinaryOpExpression>(
        "=",
        nullptr,
        std::make_unique<LiteralExpression>("1", "INTEGER")
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt7), std::runtime_error, "BinaryOp with null left child");

    // 3h. BinaryOp with null right child
    SelectStatement stmt8;
    stmt8.table_name = "users";
    stmt8.where_clause = std::make_unique<BinaryOpExpression>(
        "=",
        std::make_unique<LiteralExpression>("1", "INTEGER"),
        nullptr
    );
    TEST_ASSERT_THROWS(binder.BindSelect(stmt8), std::runtime_error, "BinaryOp with null right child");
}

void TestSchemaGetColIdxEdgeCases() {
    std::cout << "=== Stress Test 4: Schema::GetColIdx Edge Cases ===\n";
    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("name", TypeId::VARCHAR);
    schema.AddColumn("age", TypeId::INTEGER);

    TEST_ASSERT(schema.GetColIdx("id") == 0, "GetColIdx('id') == 0");
    TEST_ASSERT(schema.GetColIdx("name") == 1, "GetColIdx('name') == 1");
    TEST_ASSERT(schema.GetColIdx("age") == 2, "GetColIdx('age') == 2");
    TEST_ASSERT(schema.GetColIdx("non_existent") == -1, "GetColIdx('non_existent') == -1");
    TEST_ASSERT(schema.GetColIdx("") == -1, "GetColIdx('') == -1");
    TEST_ASSERT(schema.GetColIdx("ID") == -1, "GetColIdx('ID') is case-sensitive (returns -1)");

    // Test Duplicate Columns in Schema
    Schema dup_schema;
    dup_schema.AddColumn("col1", TypeId::INTEGER);
    dup_schema.AddColumn("col2", TypeId::VARCHAR);
    dup_schema.AddColumn("col1", TypeId::SMALLINT); // duplicate column name 'col1'

    TEST_ASSERT(dup_schema.columns.size() == 3, "Schema contains 3 columns including duplicate");
    TEST_ASSERT(dup_schema.GetColIdx("col1") == 0, "GetColIdx('col1') returns first matching index (0)");
    TEST_ASSERT(dup_schema.GetColIdx("col2") == 1, "GetColIdx('col2') returns index 1");
}

void TestWildcardExpansionEdgeCases() {
    std::cout << "=== Stress Test 5: Wildcard Expansion (SELECT *) Edge Cases ===\n";
    Catalog catalog;

    // 5a. Empty schema table
    Schema empty_schema;
    catalog.CreateTable("empty_table", empty_schema);

    Binder binder(catalog);

    SelectStatement stmt1;
    stmt1.table_name = "empty_table";
    stmt1.select_all = true;

    auto bound1 = binder.BindSelect(stmt1);
    TEST_ASSERT(bound1 != nullptr, "Binding SELECT * on empty table succeeded");
    TEST_ASSERT(bound1->select_list.empty(), "Select list is empty for table with 0 columns");
    TEST_ASSERT(bound1->select_schema.columns.empty(), "Select schema is empty for table with 0 columns");

    // 5b. Wildcard via ColumnRefExpression("*")
    Schema users_schema;
    users_schema.AddColumn("c1", TypeId::INTEGER);
    users_schema.AddColumn("c2", TypeId::VARCHAR);
    catalog.CreateTable("users", users_schema);

    SelectStatement stmt2;
    stmt2.table_name = "users";
    stmt2.select_list.push_back(std::make_unique<ColumnRefExpression>("users", "*"));

    auto bound2 = binder.BindSelect(stmt2);
    TEST_ASSERT(bound2 != nullptr, "Binding SELECT ColumnRef('*') succeeded");
    TEST_ASSERT(bound2->select_list.size() == 2, "Wildcard '*' expanded to 2 columns");
    TEST_ASSERT(bound2->select_schema.columns.size() == 2, "Select schema has 2 columns");

    // 5c. Multiple wildcards and explicit columns: SELECT *, c1, * FROM users
    SelectStatement stmt3;
    stmt3.table_name = "users";
    stmt3.select_list.push_back(std::make_unique<ColumnRefExpression>("users", "*"));
    stmt3.select_list.push_back(std::make_unique<ColumnRefExpression>("users", "c1"));
    stmt3.select_list.push_back(std::make_unique<ColumnRefExpression>("users", "*"));

    auto bound3 = binder.BindSelect(stmt3);
    TEST_ASSERT(bound3 != nullptr, "Binding SELECT *, c1, * succeeded");
    TEST_ASSERT(bound3->select_list.size() == 5, "Select list expanded to 2 + 1 + 2 = 5 items");

    // 5d. Aggregate COUNT(*)
    SelectStatement stmt4;
    stmt4.table_name = "users";
    stmt4.select_list.push_back(std::make_unique<AggregateExpression>("COUNT", nullptr, true));

    auto bound4 = binder.BindSelect(stmt4);
    TEST_ASSERT(bound4 != nullptr, "Binding COUNT(*) succeeded");
    TEST_ASSERT(bound4->select_list.size() == 1, "Count(*) select list size 1");
    TEST_ASSERT(bound4->select_list[0]->GetExprType() == BoundExpressionType::AGGREGATE, "Count(*) is BoundAggregateExpression");
    const auto* agg = static_cast<const BoundAggregateExpression*>(bound4->select_list[0].get());
    TEST_ASSERT(agg->IsStar() == true, "Aggregate IsStar() is true");
    TEST_ASSERT(agg->GetExpr() == nullptr, "Aggregate inner expr is nullptr for star");
}

void TestDuplicateColumnsInQuery() {
    std::cout << "=== Stress Test 6: Duplicate Columns in Table & Joins ===\n";
    Catalog catalog;
    Schema t1_schema;
    t1_schema.AddColumn("id", TypeId::INTEGER);
    t1_schema.AddColumn("val", TypeId::VARCHAR);
    catalog.CreateTable("t1", t1_schema);

    Schema t2_schema;
    t2_schema.AddColumn("id", TypeId::INTEGER);
    t2_schema.AddColumn("score", TypeId::INTEGER);
    catalog.CreateTable("t2", t2_schema);

    Binder binder(catalog);

    // 6a. JOIN between t1 and t2 (both have "id")
    SelectStatement stmt;
    stmt.from_table = std::make_unique<JoinTableRef>(
        std::make_unique<BaseTableRef>("t1"),
        std::make_unique<BaseTableRef>("t2"),
        "INNER",
        std::make_unique<BinaryOpExpression>(
            "=",
            std::make_unique<ColumnRefExpression>("t1", "id"),
            std::make_unique<ColumnRefExpression>("t2", "id")
        )
    );
    stmt.select_all = true;

    auto bound = binder.BindSelect(stmt);
    TEST_ASSERT(bound != nullptr, "Join of t1 and t2 bound successfully");
    TEST_ASSERT(bound->select_schema.columns.size() == 4, "Joined schema has 4 columns (id, val, id, score)");
    TEST_ASSERT(bound->select_schema.columns[0].name == "id", "Col 0 is id");
    TEST_ASSERT(bound->select_schema.columns[2].name == "id", "Col 2 is id");
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   M3 Binder & Catalog Stress Test Suite               \n";
    std::cout << "========================================================\n";

    TestEmptyCatalogConditions();
    TestInvalidTables();
    TestInvalidColumns();
    TestSchemaGetColIdxEdgeCases();
    TestWildcardExpansionEdgeCases();
    TestDuplicateColumnsInQuery();

    std::cout << "========================================================\n";
    std::cout << " Summary: " << g_passed << " PASSED, " << g_failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}
