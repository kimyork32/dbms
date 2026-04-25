#pragma once
#include <string>
#include <vector>
#include <memory>

namespace megatron {

enum class StatementType {
    CREATE,
    INSERT,
    SELECT,
    UNKNOWN
};

/**
 * @brief Nodo base del Árbol de Sintaxis Abstracta (AST)
 */
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual StatementType GetType() const = 0;
};

/**
 * @brief Representa: CREATE TABLE table_name (col1, col2, ...)
 */
class CreateStatement : public ASTNode {
public:
    std::string table_name;
    std::vector<std::string> columns;

    StatementType GetType() const override { return StatementType::CREATE; }
};

/**
 * @brief Representa: INSERT INTO table_name VALUES (val1, val2, ...)
 */
class InsertStatement : public ASTNode {
public:
    std::string table_name;
    std::vector<std::string> values;

    StatementType GetType() const override { return StatementType::INSERT; }
};

/**
 * @brief Estructura simple para representar una condición WHERE (ej: id = 1)
 */
struct Condition {
    std::string column;
    std::string value;
    bool active = false;
};

/**
 * @brief Representa: SELECT * FROM table_name [WHERE col = val]
 */
class SelectStatement : public ASTNode {
public:
    std::string table_name;
    bool select_all = true;
    std::vector<std::string> columns; 
    Condition where_clause;

    StatementType GetType() const override { return StatementType::SELECT; }
};

} // namespace megatron
