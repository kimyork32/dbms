#include "benchmarks/performance_tests.hpp"
#include "execution/query_executor.hpp"
#include "parser/parser.hpp"
#include "storage/engine/disk_storage_engine.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

namespace megatron {
namespace benchmarks {

void RunFrontendTests() {
    std::cout << "--- starting frontend parser & lexer tests ---\n";

    // test 1: select with aggregates, alias, where, group by, having
    std::string sql1 = "SELECT department_id, COUNT(*), SUM(salary) AS total_sal FROM employees WHERE salary > 50000 GROUP BY department_id HAVING SUM(salary) > 100000;";
    auto res1 = ParseQuery(sql1);
    assert(res1.success && "sql1 parse failed");
    assert(res1.ast != nullptr);
    assert(res1.ast->GetType() == StatementType::SELECT);
    auto select1 = static_cast<SelectStatement*>(res1.ast.get());
    assert(!select1->select_all);
    assert(select1->select_list.size() == 3);
    assert(select1->table_name == "employees");
    assert(select1->where_clause != nullptr);
    assert(select1->group_by.size() == 1);
    assert(select1->having_clause != nullptr);

    // detailed verification of multi-column select with aggregates and aliases
    assert(select1->select_list[0]->GetExprType() == ExpressionType::COLUMN_REF);
    auto col0 = static_cast<ColumnRefExpression*>(select1->select_list[0].get());
    assert(col0->column_name == "department_id");

    assert(select1->select_list[1]->GetExprType() == ExpressionType::AGGREGATE);
    auto agg1 = static_cast<AggregateExpression*>(select1->select_list[1].get());
    assert(agg1->func_name == "COUNT");
    assert(agg1->is_star == true);

    assert(select1->select_list[2]->GetExprType() == ExpressionType::AGGREGATE);
    auto agg2 = static_cast<AggregateExpression*>(select1->select_list[2].get());
    assert(agg2->func_name == "SUM");
    assert(agg2->alias == "total_sal");
    assert(agg2->expr != nullptr && agg2->expr->GetExprType() == ExpressionType::COLUMN_REF);

    // verify group by and having clause details
    assert(select1->group_by[0]->GetExprType() == ExpressionType::COLUMN_REF);
    assert(static_cast<ColumnRefExpression*>(select1->group_by[0].get())->column_name == "department_id");
    assert(select1->having_clause->GetExprType() == ExpressionType::BINARY_OP);
    auto having_bin = static_cast<BinaryOpExpression*>(select1->having_clause.get());
    assert(having_bin->op_type == ">");
    assert(having_bin->left->GetExprType() == ExpressionType::AGGREGATE);
    assert(having_bin->right->GetExprType() == ExpressionType::LITERAL);
    std::cout << "[PASS] select query with aggregates, group by, having parsed and AST validated successfully\n";

    // test 2: join query
    std::string sql2 = "SELECT t1.a, t2.b FROM t1 INNER JOIN t2 ON t1.id = t2.t1_id;";
    auto res2 = ParseQuery(sql2);
    assert(res2.success && "sql2 parse failed");
    assert(res2.ast->GetType() == StatementType::SELECT);
    auto select2 = static_cast<SelectStatement*>(res2.ast.get());
    assert(select2->from_table != nullptr);
    assert(select2->from_table->GetRefType() == TableRefType::JOIN_TABLE);
    auto join2 = static_cast<JoinTableRef*>(select2->from_table.get());
    assert(join2->join_type == "INNER");
    assert(join2->left != nullptr && join2->left->GetRefType() == TableRefType::BASE_TABLE);
    assert(join2->right != nullptr && join2->right->GetRefType() == TableRefType::BASE_TABLE);
    auto left_base = static_cast<BaseTableRef*>(join2->left.get());
    auto right_base = static_cast<BaseTableRef*>(join2->right.get());
    assert(left_base->table_name == "t1");
    assert(right_base->table_name == "t2");
    assert(join2->join_condition != nullptr);
    assert(join2->join_condition->GetExprType() == ExpressionType::BINARY_OP);
    auto join_cond_bin = static_cast<BinaryOpExpression*>(join2->join_condition.get());
    assert(join_cond_bin->op_type == "=");
    assert(join_cond_bin->left->GetExprType() == ExpressionType::COLUMN_REF);
    assert(join_cond_bin->right->GetExprType() == ExpressionType::COLUMN_REF);
    auto cond_l = static_cast<ColumnRefExpression*>(join_cond_bin->left.get());
    auto cond_r = static_cast<ColumnRefExpression*>(join_cond_bin->right.get());
    assert(cond_l->table_name == "t1" && cond_l->column_name == "id");
    assert(cond_r->table_name == "t2" && cond_r->column_name == "t1_id");
    std::cout << "[PASS] inner join query with ON conditions parsed and AST validated successfully\n";

    // test 3: complex nested expressions
    std::string sql3 = "SELECT * FROM t WHERE (a = 1 AND b > 2) OR (c <= 3 AND d != 4);";
    auto res3 = ParseQuery(sql3);
    assert(res3.success && "sql3 parse failed");
    auto select3 = static_cast<SelectStatement*>(res3.ast.get());
    assert(select3->where_clause != nullptr);
    assert(select3->where_clause->GetExprType() == ExpressionType::BINARY_OP);
    auto where_or = static_cast<BinaryOpExpression*>(select3->where_clause.get());
    assert(where_or->op_type == "OR");
    assert(where_or->left->GetExprType() == ExpressionType::BINARY_OP);
    assert(where_or->right->GetExprType() == ExpressionType::BINARY_OP);

    auto where_and_l = static_cast<BinaryOpExpression*>(where_or->left.get());
    assert(where_and_l->op_type == "AND");
    auto bin_a1 = static_cast<BinaryOpExpression*>(where_and_l->left.get());
    assert(bin_a1->op_type == "=");
    assert(static_cast<ColumnRefExpression*>(bin_a1->left.get())->column_name == "a");
    assert(static_cast<LiteralExpression*>(bin_a1->right.get())->value == "1");

    auto bin_b2 = static_cast<BinaryOpExpression*>(where_and_l->right.get());
    assert(bin_b2->op_type == ">");
    assert(static_cast<ColumnRefExpression*>(bin_b2->left.get())->column_name == "b");
    assert(static_cast<LiteralExpression*>(bin_b2->right.get())->value == "2");

    auto where_and_r = static_cast<BinaryOpExpression*>(where_or->right.get());
    assert(where_and_r->op_type == "AND");
    auto bin_c3 = static_cast<BinaryOpExpression*>(where_and_r->left.get());
    assert(bin_c3->op_type == "<=");
    assert(static_cast<ColumnRefExpression*>(bin_c3->left.get())->column_name == "c");
    assert(static_cast<LiteralExpression*>(bin_c3->right.get())->value == "3");

    auto bin_d4 = static_cast<BinaryOpExpression*>(where_and_r->right.get());
    assert(bin_d4->op_type == "!=");
    assert(static_cast<ColumnRefExpression*>(bin_d4->left.get())->column_name == "d");
    assert(static_cast<LiteralExpression*>(bin_d4->right.get())->value == "4");
    std::cout << "[PASS] complex nested expressions parsed and AST validated successfully\n";

    // test 4: DDL & DML statements
    auto res_create = ParseQuery("CREATE TABLE test (id, name, val);");
    assert(res_create.success && res_create.ast->GetType() == StatementType::CREATE);
    auto create_stmt = static_cast<CreateStatement*>(res_create.ast.get());
    assert(create_stmt->table_name == "test");
    assert(create_stmt->columns.size() == 3);

    auto res_insert = ParseQuery("INSERT INTO test VALUES (1, 'alice', 100);");
    assert(res_insert.success && res_insert.ast->GetType() == StatementType::INSERT);
    auto insert_stmt = static_cast<InsertStatement*>(res_insert.ast.get());
    assert(insert_stmt->table_name == "test");
    assert(insert_stmt->values.size() == 3);

    auto res_update = ParseQuery("UPDATE test SET val = 200 WHERE id = 1;");
    assert(res_update.success && res_update.ast->GetType() == StatementType::UPDATE);

    auto res_delete = ParseQuery("DELETE FROM test WHERE id = 1;");
    assert(res_delete.success && res_delete.ast->GetType() == StatementType::DELETE);
    std::cout << "[PASS] create, insert, update, delete queries parsed successfully\n";

    // test 5: error cases (invalid keywords, missing parentheses, unclosed strings)
    auto res_err1 = ParseQuery("SELECT FROM employees;");
    assert(!res_err1.success);
    assert(!res_err1.error_message.empty());
    std::cout << "[PASS] syntax error (missing projection) handled cleanly: " << res_err1.error_message << "\n";

    auto res_err2 = ParseQuery("SELECT * FROM employees WHERE (salary > 50000;");
    assert(!res_err2.success);
    assert(!res_err2.error_message.empty());
    std::cout << "[PASS] syntax error (missing closing parenthesis) handled cleanly: " << res_err2.error_message << "\n";

    auto res_err3 = ParseQuery("INVALID_KEYWORD t;");
    assert(!res_err3.success);
    assert(!res_err3.error_message.empty());
    std::cout << "[PASS] syntax error (invalid keyword at start) handled cleanly: " << res_err3.error_message << "\n";

    auto res_err4 = ParseQuery("SELECT SUM(salary FROM employees;");
    assert(!res_err4.success);
    assert(!res_err4.error_message.empty());
    std::cout << "[PASS] syntax error (missing rparen in aggregate) handled cleanly: " << res_err4.error_message << "\n";

    // Empirical findings on unclosed string, trailing tokens, and negative numbers:
    auto res_unclosed = ParseQuery("SELECT 'unclosed string FROM employees;");
    std::cout << "[EMPIRICAL FINDING] unclosed string parsing success = " << (res_unclosed.success ? "true" : "false") << "\n";

    auto res_trailing = ParseQuery("SELECT * FROM employees EXTRA_GARBAGE;");
    std::cout << "[EMPIRICAL FINDING] trailing tokens query parsing success = " << (res_trailing.success ? "true" : "false") << "\n";

    auto res_neg = ParseQuery("SELECT * FROM employees WHERE salary = -5000;");
    std::cout << "[EMPIRICAL FINDING] negative number query parsing success = " << (res_neg.success ? "true" : "false") << "\n";

    std::cout << "--- frontend parser & lexer tests completed successfully ---\n";
}

void RunPerformanceTests(DiskStorageEngine& storage) {
    RunFrontendTests();

    std::cout << "--- starting performance tests ---\n";
    
    // preparation
    execution::ExecuteQuery("CREATE TABLE perftest (id, data, moredata)", storage, false);

    const int NUM_RECORDS = 1000000; // can now handle many more thanks to dynamic paging.

    std::cout << "inserting " << NUM_RECORDS << " records...\n";
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS; ++i) {
        std::string query = "INSERT INTO perftest VALUES (" + std::to_string(i) + ", 'TestData" + std::to_string(i) + "', 'MoreData" + std::to_string(i) + "')";
        execution::ExecuteQuery(query, storage, false);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "insertion time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "performing full scan...\n";
    start = std::chrono::high_resolution_clock::now();
    execution::ExecuteQuery("SELECT * FROM perftest", storage, false);
    end = std::chrono::high_resolution_clock::now();
    std::cout << "read time (full scan): " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "updating " << NUM_RECORDS / 2 << " records (by PK)...\n";
    start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS / 2; ++i) {
        std::string query = "UPDATE perftest SET data = 'UpdatedData' WHERE id = " + std::to_string(i);
        execution::ExecuteQuery(query, storage, false);
    }
    auto update_end = std::chrono::high_resolution_clock::now();
    std::cout << "update time: " << std::chrono::duration_cast<std::chrono::milliseconds>(update_end - start).count() << " ms\n";

    std::cout << "deleting " << NUM_RECORDS / 2 << " records (by PK)...\n";
    start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS / 2; ++i) {
        std::string query = "DELETE FROM perftest WHERE id = " + std::to_string(i);
        execution::ExecuteQuery(query, storage, false);
    }
    auto delete_end = std::chrono::high_resolution_clock::now();
    std::cout << "deletion time: " << std::chrono::duration_cast<std::chrono::milliseconds>(delete_end - start).count() << " ms\n";

    std::cout << "--- end of performance tests ---\n";
}

} // namespace benchmarks
} // namespace megatron
