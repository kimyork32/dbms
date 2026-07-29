#include "parser/parser.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace megatron {

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            g_tests_passed++; \
        } else { \
            g_tests_failed++; \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
        } \
    } while (0)

void TestOperatorPrecedence() {
    std::cout << "--- Running Test Category 1: Operator Precedence ---\n";

    // 1. Multiplicative over Additive: a + b * c - d / e
    {
        auto res = ParseQuery("SELECT a + b * c - d / e FROM t;");
        TEST_ASSERT(res.success, "Parse a + b * c - d / e");
        TEST_ASSERT(res.ast != nullptr, "AST not null for a + b * c - d / e");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        TEST_ASSERT(sel->select_list.size() == 1, "Select list size 1");
        auto root_expr = sel->select_list[0].get();
        TEST_ASSERT(root_expr->GetExprType() == ExpressionType::BINARY_OP, "Root is BinaryOp");
        auto bin1 = static_cast<BinaryOpExpression*>(root_expr);
        TEST_ASSERT(bin1->op_type == "-", "Root op is '-'");

        // Left child of '-' should be '+'
        TEST_ASSERT(bin1->left->GetExprType() == ExpressionType::BINARY_OP, "Left child is BinaryOp");
        auto bin_plus = static_cast<BinaryOpExpression*>(bin1->left.get());
        TEST_ASSERT(bin_plus->op_type == "+", "Left child op is '+'");
        TEST_ASSERT(bin_plus->left->GetExprType() == ExpressionType::COLUMN_REF, "Left of '+' is ColumnRef a");
        TEST_ASSERT(bin_plus->right->GetExprType() == ExpressionType::BINARY_OP, "Right of '+' is BinaryOp *");
        auto bin_mul = static_cast<BinaryOpExpression*>(bin_plus->right.get());
        TEST_ASSERT(bin_mul->op_type == "*", "Right of '+' op is '*'");

        // Right child of '-' should be '/'
        TEST_ASSERT(bin1->right->GetExprType() == ExpressionType::BINARY_OP, "Right child is BinaryOp");
        auto bin_div = static_cast<BinaryOpExpression*>(bin1->right.get());
        TEST_ASSERT(bin_div->op_type == "/", "Right child op is '/'");
    }

    // 2. Parentheses override: (a + b) * (c - d) / e
    {
        auto res = ParseQuery("SELECT (a + b) * (c - d) / e FROM t;");
        TEST_ASSERT(res.success, "Parse (a + b) * (c - d) / e");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        auto root_expr = sel->select_list[0].get();
        TEST_ASSERT(root_expr->GetExprType() == ExpressionType::BINARY_OP, "Root is BinaryOp");
        auto bin_div = static_cast<BinaryOpExpression*>(root_expr);
        TEST_ASSERT(bin_div->op_type == "/", "Root op is '/'");

        // Left of '/' is '*'
        TEST_ASSERT(bin_div->left->GetExprType() == ExpressionType::BINARY_OP, "Left of '/' is '*'");
        auto bin_mul = static_cast<BinaryOpExpression*>(bin_div->left.get());
        TEST_ASSERT(bin_mul->op_type == "*", "Op is '*'");
        TEST_ASSERT(bin_mul->left->GetExprType() == ExpressionType::BINARY_OP, "Left of '*' is '+'");
        auto bin_plus = static_cast<BinaryOpExpression*>(bin_mul->left.get());
        TEST_ASSERT(bin_plus->op_type == "+", "Op is '+'");
    }

    // 3. Arithmetic vs Comparison: a + b * c >= d - e / f
    {
        auto res = ParseQuery("SELECT * FROM t WHERE a + b * c >= d - e / f;");
        TEST_ASSERT(res.success, "Parse a + b * c >= d - e / f");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        TEST_ASSERT(sel->where_clause != nullptr, "Where clause not null");
        auto root = static_cast<BinaryOpExpression*>(sel->where_clause.get());
        TEST_ASSERT(root->op_type == ">=", "Where root op is '>='");
        TEST_ASSERT(root->left->GetExprType() == ExpressionType::BINARY_OP, "Left of '>=' is '+'");
        TEST_ASSERT(root->right->GetExprType() == ExpressionType::BINARY_OP, "Right of '>=' is '-'");
    }

    // 4. Comparison vs Logical AND vs Logical OR: a = 1 AND b = 2 OR c = 3 AND d = 4
    {
        auto res = ParseQuery("SELECT * FROM t WHERE a = 1 AND b = 2 OR c = 3 AND d = 4;");
        TEST_ASSERT(res.success, "Parse logical AND/OR query");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        auto root = static_cast<BinaryOpExpression*>(sel->where_clause.get());
        TEST_ASSERT(root->op_type == "OR", "Root of AND/OR precedence is 'OR'");
        TEST_ASSERT(root->left->GetExprType() == ExpressionType::BINARY_OP, "Left of OR is AND");
        auto left_and = static_cast<BinaryOpExpression*>(root->left.get());
        TEST_ASSERT(left_and->op_type == "AND", "Left op is AND");
        TEST_ASSERT(root->right->GetExprType() == ExpressionType::BINARY_OP, "Right of OR is AND");
        auto right_and = static_cast<BinaryOpExpression*>(root->right.get());
        TEST_ASSERT(right_and->op_type == "AND", "Right op is AND");
    }

    // 5. Parentheses overriding AND/OR: (a = 1 OR b = 2) AND c = 3
    {
        auto res = ParseQuery("SELECT * FROM t WHERE (a = 1 OR b = 2) AND c = 3;");
        TEST_ASSERT(res.success, "Parse (a = 1 OR b = 2) AND c = 3");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        auto root = static_cast<BinaryOpExpression*>(sel->where_clause.get());
        TEST_ASSERT(root->op_type == "AND", "Root of overridden precedence is 'AND'");
        TEST_ASSERT(root->left->GetExprType() == ExpressionType::BINARY_OP, "Left of AND is OR");
        auto left_or = static_cast<BinaryOpExpression*>(root->left.get());
        TEST_ASSERT(left_or->op_type == "OR", "Left op is OR");
    }

    // 6. Test all comparison operators (=, !=, <>, <, >, <=, >=)
    {
        std::vector<std::string> ops = {"=", "!=", "<>", "<", ">", "<=", ">="};
        for (const auto& op : ops) {
            std::string sql = "SELECT * FROM t WHERE x " + op + " 10;";
            auto res = ParseQuery(sql);
            TEST_ASSERT(res.success, ("Parse comparison op: " + op).c_str());
            if (res.success) {
                auto sel = static_cast<SelectStatement*>(res.ast.get());
                auto bin = static_cast<BinaryOpExpression*>(sel->where_clause.get());
                TEST_ASSERT(bin->op_type == op, ("Op matches expected: " + op).c_str());
            }
        }
    }

    // 7. Left-associativity of subtraction and division: a - b - c and a / b / c
    {
        auto res_sub = ParseQuery("SELECT a - b - c FROM t;");
        TEST_ASSERT(res_sub.success, "Parse a - b - c");
        auto sel_sub = static_cast<SelectStatement*>(res_sub.ast.get());
        auto root_sub = static_cast<BinaryOpExpression*>(sel_sub->select_list[0].get());
        TEST_ASSERT(root_sub->op_type == "-", "Root is '-'");
        TEST_ASSERT(root_sub->left->GetExprType() == ExpressionType::BINARY_OP, "Left is BinaryOp for a - b");
        auto left_sub = static_cast<BinaryOpExpression*>(root_sub->left.get());
        TEST_ASSERT(left_sub->op_type == "-", "Left op is '-'");

        auto res_div = ParseQuery("SELECT a / b / c FROM t;");
        TEST_ASSERT(res_div.success, "Parse a / b / c");
        auto sel_div = static_cast<SelectStatement*>(res_div.ast.get());
        auto root_div = static_cast<BinaryOpExpression*>(sel_div->select_list[0].get());
        TEST_ASSERT(root_div->op_type == "/", "Root is '/'");
        TEST_ASSERT(root_div->left->GetExprType() == ExpressionType::BINARY_OP, "Left is BinaryOp for a / b");
    }
}

void TestTableReferences() {
    std::cout << "--- Running Test Category 2: Table References ---\n";

    // 1. Single table without alias
    {
        auto res = ParseQuery("SELECT * FROM users;");
        TEST_ASSERT(res.success, "Parse single table without alias");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        TEST_ASSERT(sel->from_table != nullptr, "from_table not null");
        TEST_ASSERT(sel->from_table->GetRefType() == TableRefType::BASE_TABLE, "from_table is BASE_TABLE");
        auto base = static_cast<BaseTableRef*>(sel->from_table.get());
        TEST_ASSERT(base->table_name == "users", "Base table name is users");
        TEST_ASSERT(base->alias.empty(), "Base table alias is empty");
    }

    // 2. Single table with alias (implicit and AS)
    {
        auto res1 = ParseQuery("SELECT * FROM users u;");
        TEST_ASSERT(res1.success, "Parse single table with implicit alias");
        auto base1 = static_cast<BaseTableRef*>(static_cast<SelectStatement*>(res1.ast.get())->from_table.get());
        TEST_ASSERT(base1->table_name == "users", "Table name is users");
        TEST_ASSERT(base1->alias == "u", "Alias is u");

        auto res2 = ParseQuery("SELECT * FROM users AS u;");
        TEST_ASSERT(res2.success, "Parse single table with AS alias");
        auto base2 = static_cast<BaseTableRef*>(static_cast<SelectStatement*>(res2.ast.get())->from_table.get());
        TEST_ASSERT(base2->table_name == "users", "Table name is users");
        TEST_ASSERT(base2->alias == "u", "Alias is u");
    }

    // 3. JoinTableRef with single join (INNER JOIN / JOIN)
    {
        auto res = ParseQuery("SELECT * FROM users u INNER JOIN orders o ON u.id = o.user_id;");
        TEST_ASSERT(res.success, "Parse INNER JOIN query");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        TEST_ASSERT(sel->from_table->GetRefType() == TableRefType::JOIN_TABLE, "from_table is JOIN_TABLE");
        auto join = static_cast<JoinTableRef*>(sel->from_table.get());
        TEST_ASSERT(join->join_type == "INNER", "Join type is INNER");
        TEST_ASSERT(join->left->GetRefType() == TableRefType::BASE_TABLE, "Left is BASE_TABLE");
        TEST_ASSERT(join->right->GetRefType() == TableRefType::BASE_TABLE, "Right is BASE_TABLE");
        auto left_base = static_cast<BaseTableRef*>(join->left.get());
        auto right_base = static_cast<BaseTableRef*>(join->right.get());
        TEST_ASSERT(left_base->table_name == "users" && left_base->alias == "u", "Left base table correct");
        TEST_ASSERT(right_base->table_name == "orders" && right_base->alias == "o", "Right base table correct");
        TEST_ASSERT(join->join_condition != nullptr, "Join condition not null");
        auto cond = static_cast<BinaryOpExpression*>(join->join_condition.get());
        TEST_ASSERT(cond->op_type == "=", "Join condition op is '='");
    }

    // 4. Multiple chained joins: t1 JOIN t2 ON cond1 JOIN t3 ON cond2
    {
        auto res = ParseQuery("SELECT * FROM t1 JOIN t2 ON t1.id = t2.id JOIN t3 ON t2.id = t3.id;");
        TEST_ASSERT(res.success, "Parse multiple chained joins");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        TEST_ASSERT(sel->from_table->GetRefType() == TableRefType::JOIN_TABLE, "from_table is JOIN_TABLE");
        auto root_join = static_cast<JoinTableRef*>(sel->from_table.get());
        // Left should be JoinTableRef (t1 JOIN t2)
        TEST_ASSERT(root_join->left->GetRefType() == TableRefType::JOIN_TABLE, "Left of second join is JoinTableRef");
        TEST_ASSERT(root_join->right->GetRefType() == TableRefType::BASE_TABLE, "Right of second join is t3");
        auto inner_join = static_cast<JoinTableRef*>(root_join->left.get());
        auto t3_base = static_cast<BaseTableRef*>(root_join->right.get());
        TEST_ASSERT(t3_base->table_name == "t3", "Right table is t3");
        TEST_ASSERT(inner_join->left->GetRefType() == TableRefType::BASE_TABLE, "Inner left is t1");
        TEST_ASSERT(inner_join->right->GetRefType() == TableRefType::BASE_TABLE, "Inner right is t2");
    }

    // 5. Complex JOIN ON condition with AND
    {
        auto res = ParseQuery("SELECT * FROM a JOIN b ON a.id = b.id AND a.status = b.status;");
        TEST_ASSERT(res.success, "Parse complex JOIN ON condition");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        auto join = static_cast<JoinTableRef*>(sel->from_table.get());
        TEST_ASSERT(join->join_condition->GetExprType() == ExpressionType::BINARY_OP, "Join condition is BinaryOp");
        auto cond_bin = static_cast<BinaryOpExpression*>(join->join_condition.get());
        TEST_ASSERT(cond_bin->op_type == "AND", "Join condition root op is 'AND'");
    }
}

void TestRobustnessAndBoundaryConditions() {
    std::cout << "--- Running Test Category 3: Robustness & Boundary Conditions ---\n";

    // 1. Empty SQL string
    {
        auto res = ParseQuery("");
        TEST_ASSERT(!res.success, "Empty string returns success=false");
        TEST_ASSERT(res.ast == nullptr, "Empty string AST is null");
        TEST_ASSERT(!res.error_message.empty(), "Empty string has error message");
    }

    // 2. Whitespace-only string
    {
        auto res = ParseQuery("   \t\n  ");
        TEST_ASSERT(!res.success, "Whitespace string returns success=false");
        TEST_ASSERT(res.ast == nullptr, "Whitespace string AST is null");
        TEST_ASSERT(!res.error_message.empty(), "Whitespace string has error message");
    }

    // 3. Unexpected characters / Invalid tokens
    {
        auto res = ParseQuery("SELECT @ FROM t;");
        TEST_ASSERT(!res.success, "Unexpected char @ returns success=false");
        TEST_ASSERT(res.ast == nullptr, "Unexpected char AST is null");

        auto res2 = ParseQuery("SELECT #$% FROM t;");
        TEST_ASSERT(!res2.success, "Unexpected char #$% returns success=false");
    }

    // 4. Malformed syntax
    {
        std::vector<std::string> bad_sqls = {
            "SELECT FROM employees;",
            "SELECT * FROM;",
            "SELECT * FROM t WHERE;",
            "SELECT * FROM t JOIN ON a = b;",
            "SELECT (1 + 2 FROM t;",
            "SELECT 1 + * 2 FROM t;",
            "SELECT SUM() FROM t;",
            "SELECT SUM(*) FROM t;",
            "CREATE TABLE ;",
            "INSERT INTO VALUES (1, 2);"
        };
        for (const auto& sql : bad_sqls) {
            auto res = ParseQuery(sql);
            TEST_ASSERT(!res.success, ("Malformed query handled cleanly: " + sql).c_str());
            TEST_ASSERT(res.ast == nullptr, "Malformed AST is null");
        }
    }

    // 5. Escaped string literals and numbers
    {
        auto res = ParseQuery("SELECT 'hello \\'world\\'', 123, 456.78 FROM t;");
        TEST_ASSERT(res.success, "Parse escaped string and float numbers");
        auto sel = static_cast<SelectStatement*>(res.ast.get());
        TEST_ASSERT(sel->select_list.size() == 3, "Select list has 3 items");
        auto lit1 = static_cast<LiteralExpression*>(sel->select_list[0].get());
        TEST_ASSERT(lit1->value == "hello 'world'", "Escaped quote parsed correctly");
        TEST_ASSERT(lit1->value_type == "STRING", "Literal type is STRING");

        auto lit2 = static_cast<LiteralExpression*>(sel->select_list[1].get());
        TEST_ASSERT(lit2->value == "123" && lit2->value_type == "INTEGER", "Integer literal parsed");

        auto lit3 = static_cast<LiteralExpression*>(sel->select_list[2].get());
        TEST_ASSERT(lit3->value == "456.78" && lit3->value_type == "FLOAT", "Float literal parsed");
    }

    // 6. Keywords case-insensitivity
    {
        auto res = ParseQuery("select ID, NAME from USERS where ID = 1 group by ID having count(*) > 0;");
        TEST_ASSERT(res.success, "Lowercase keywords parsed successfully");
        TEST_ASSERT(res.ast->GetType() == StatementType::SELECT, "Statement is SELECT");
    }
}

} // namespace megatron

int main() {
    std::cout << "==================================================\n";
    std::cout << "   DBMS M1 Lexer & Parser Challenger Test Suite   \n";
    std::cout << "==================================================\n";

    megatron::TestOperatorPrecedence();
    megatron::TestTableReferences();
    megatron::TestRobustnessAndBoundaryConditions();

    std::cout << "==================================================\n";
    std::cout << " Summary: " << megatron::g_tests_passed << " PASSED, "
              << megatron::g_tests_failed << " FAILED\n";
    std::cout << "==================================================\n";

    return (megatron::g_tests_failed == 0) ? 0 : 1;
}
