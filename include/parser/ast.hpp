#pragma once
#include <string>
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
 * @brief simple structure to represent a where condition (e.g., id = 1)
 */
struct Condition {
    std::string column;
    std::string value;
    bool active = false;
};

/**
 * @brief represents: SELECT * FROM table_name [WHERE col = val]
 */
class SelectStatement : public ASTNode {
public:
    std::string table_name;
    bool select_all = true;
    std::vector<std::string> columns; 
    Condition where_clause;

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
