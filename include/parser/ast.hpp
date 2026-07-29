#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace megatron {

enum class StatementType {
    CREATE,
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    UNKNOWN
};

/**
 * @brief base node of the abstract syntax tree (ast)
 */
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual StatementType GetType() const = 0;
};

enum class ExpressionType {
    COLUMN_REF,
    LITERAL,
    BINARY_OP,
    AGGREGATE
};

/**
 * @brief base class for all expression nodes in sql queries
 */
class ExpressionNode {
public:
    virtual ~ExpressionNode() = default;
    virtual ExpressionType GetExprType() const = 0;
};

/**
 * @brief represents a column reference, optionally qualified with table name and alias
 */
class ColumnRefExpression : public ExpressionNode {
public:
    std::string table_name;
    std::string column_name;
    std::string alias;

    ColumnRefExpression() = default;
    ColumnRefExpression(std::string table_name, std::string column_name, std::string alias = "")
        : table_name(std::move(table_name)),
          column_name(std::move(column_name)),
          alias(std::move(alias)) {}

    ExpressionType GetExprType() const override { return ExpressionType::COLUMN_REF; }
};

/**
 * @brief represents a literal value (number, string, etc.)
 */
class LiteralExpression : public ExpressionNode {
public:
    std::string value;
    std::string value_type; // e.g. "INTEGER", "FLOAT", "STRING"

    LiteralExpression() = default;
    LiteralExpression(std::string value, std::string value_type)
        : value(std::move(value)), value_type(std::move(value_type)) {}

    ExpressionType GetExprType() const override { return ExpressionType::LITERAL; }
};

/**
 * @brief represents a binary operation (e.g., col = 5, a + b, x AND y)
 */
class BinaryOpExpression : public ExpressionNode {
public:
    std::string op_type;
    std::unique_ptr<ExpressionNode> left;
    std::unique_ptr<ExpressionNode> right;

    BinaryOpExpression() = default;
    BinaryOpExpression(std::string op_type,
                       std::unique_ptr<ExpressionNode> left,
                       std::unique_ptr<ExpressionNode> right)
        : op_type(std::move(op_type)),
          left(std::move(left)),
          right(std::move(right)) {}

    ExpressionType GetExprType() const override { return ExpressionType::BINARY_OP; }
};

/**
 * @brief represents an aggregate expression (e.g., SUM(col), COUNT(*))
 */
class AggregateExpression : public ExpressionNode {
public:
    std::string func_name;
    std::unique_ptr<ExpressionNode> expr;
    bool is_star{false};
    std::string alias;

    AggregateExpression() = default;
    AggregateExpression(std::string func_name,
                        std::unique_ptr<ExpressionNode> expr,
                        bool is_star = false,
                        std::string alias = "")
        : func_name(std::move(func_name)),
          expr(std::move(expr)),
          is_star(is_star),
          alias(std::move(alias)) {}

    ExpressionType GetExprType() const override { return ExpressionType::AGGREGATE; }
};

enum class TableRefType {
    BASE_TABLE,
    JOIN_TABLE
};

/**
 * @brief base class for table references in from/join clauses
 */
class TableRef {
public:
    virtual ~TableRef() = default;
    virtual TableRefType GetRefType() const = 0;
};

/**
 * @brief represents a simple base table reference with optional alias
 */
class BaseTableRef : public TableRef {
public:
    std::string table_name;
    std::string alias;

    BaseTableRef() = default;
    explicit BaseTableRef(std::string table_name, std::string alias = "")
        : table_name(std::move(table_name)), alias(std::move(alias)) {}

    TableRefType GetRefType() const override { return TableRefType::BASE_TABLE; }
};

/**
 * @brief represents a join table reference (e.g. left JOIN right ON cond)
 */
class JoinTableRef : public TableRef {
public:
    std::unique_ptr<TableRef> left;
    std::unique_ptr<TableRef> right;
    std::string join_type; // e.g. "INNER", "LEFT", "RIGHT"
    std::unique_ptr<ExpressionNode> join_condition;

    JoinTableRef() = default;
    JoinTableRef(std::unique_ptr<TableRef> left,
                 std::unique_ptr<TableRef> right,
                 std::string join_type,
                 std::unique_ptr<ExpressionNode> join_condition)
        : left(std::move(left)),
          right(std::move(right)),
          join_type(std::move(join_type)),
          join_condition(std::move(join_condition)) {}

    TableRefType GetRefType() const override { return TableRefType::JOIN_TABLE; }
};

/**
 * @brief represents: CREATE TABLE table_name (col1, col2, ...)
 */
class CreateStatement : public ASTNode {
public:
    std::string table_name;
    std::vector<std::string> columns;

    StatementType GetType() const override { return StatementType::CREATE; }
};

/**
 * @brief represents: INSERT INTO table_name VALUES (val1, val2, ...)
 */
class InsertStatement : public ASTNode {
public:
    std::string table_name;
    std::vector<std::string> values;

    StatementType GetType() const override { return StatementType::INSERT; }
};

/**
 * @brief simple structure to represent a legacy where condition
 */
struct Condition {
    std::string column;
    std::string value;
    bool active = false;
};

/**
 * @brief represents: SELECT [expr_list] FROM [table_ref] [WHERE expr] [GROUP BY expr_list] [HAVING expr]
 */
class SelectStatement : public ASTNode {
public:
    bool select_all{false};
    std::vector<std::unique_ptr<ExpressionNode>> select_list;
    std::unique_ptr<TableRef> from_table;
    std::unique_ptr<ExpressionNode> where_clause;
    std::vector<std::unique_ptr<ExpressionNode>> group_by;
    std::unique_ptr<ExpressionNode> having_clause;

    // helper field for backwards compatibility with legacy execution code
    std::string table_name;

    StatementType GetType() const override { return StatementType::SELECT; }
};

/**
 * @brief represents: UPDATE table_name SET col = val [WHERE col = val]
 */
class UpdateStatement : public ASTNode {
public:
    std::string table_name;
    std::string set_column;
    std::string set_value;
    Condition where_clause;

    StatementType GetType() const override { return StatementType::UPDATE; }
};

/**
 * @brief represents: DELETE FROM table_name [WHERE col = val]
 */
class DeleteStatement : public ASTNode {
public:
    std::string table_name;
    Condition where_clause;

    StatementType GetType() const override { return StatementType::DELETE; }
};

} // namespace megatron
