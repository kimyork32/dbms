#include "parser/parser.hpp"
#include <sstream>
#include <algorithm>
#include <iostream>

namespace megatron {

// ================= Lexer Implementation =================

Lexer::Lexer(std::string source) {
    // Normalización simple para manejar puntuación.
    std::string normalized;
    for (char c : source) {
        if (c == '(' || c == ')' || c == ',' || c == ';' || c == '=') {
            normalized += ' '; normalized += c; normalized += ' ';
        } else {
            normalized += c;
        }
    }

    std::stringstream ss(normalized);
    std::string token;
    while (ss >> token) {
        // Convertir a mayúsculas para palabras clave (opcional para simplicidad).
        tokens_.push_back(token);
    }
}

std::string Lexer::NextToken() {
    if (!HasMore()) return "";
    return tokens_[current_pos_++];
}

bool Lexer::HasMore() const {
    return current_pos_ < tokens_.size();
}

std::string Lexer::Peek() const {
    if (!HasMore()) return "";
    return tokens_[current_pos_];
}

// ================= Parser Implementation =================

std::unique_ptr<ASTNode> Parser::Parse(const std::string& query) {
    Lexer lexer(query);
    if (!lexer.HasMore()) return nullptr;

    std::string first_token = lexer.NextToken();
    std::transform(first_token.begin(), first_token.end(), first_token.begin(), ::toupper);

    if (first_token == "CREATE") {
        return ParseCreate(lexer);
    } else if (first_token == "INSERT") {
        return ParseInsert(lexer);
    } else if (first_token == "SELECT") {
        return ParseSelect(lexer);
    } else {
        throw std::runtime_error("Unknown command: " + first_token);
    }
}

std::unique_ptr<ASTNode> Parser::ParseCreate(Lexer& lexer) {
    auto stmt = std::make_unique<CreateStatement>();
    
    // Esperar "TABLE"
    std::string table_token = lexer.NextToken();
    std::transform(table_token.begin(), table_token.end(), table_token.begin(), ::toupper);
    if (table_token != "TABLE") throw std::runtime_error("Expected TABLE after CREATE");

    stmt->table_name = lexer.NextToken();

    // Esperar "("
    if (lexer.NextToken() != "(") throw std::runtime_error("Expected ( after table name");

    while (lexer.HasMore()) {
        std::string col = lexer.NextToken();
        if (col == ")") break;
        if (col != ",") {
            stmt->columns.push_back(col);
        }
    }

    return stmt;
}

std::unique_ptr<ASTNode> Parser::ParseInsert(Lexer& lexer) {
    auto stmt = std::make_unique<InsertStatement>();
    
    // Esperar "INTO"
    std::string into_token = lexer.NextToken();
    std::transform(into_token.begin(), into_token.end(), into_token.begin(), ::toupper);
    if (into_token != "INTO") throw std::runtime_error("Expected INTO after INSERT");

    stmt->table_name = lexer.NextToken();

    // Esperar "VALUES"
    std::string values_token = lexer.NextToken();
    std::transform(values_token.begin(), values_token.end(), values_token.begin(), ::toupper);
    if (values_token != "VALUES") throw std::runtime_error("Expected VALUES after table name");

    // Esperar "("
    if (lexer.NextToken() != "(") throw std::runtime_error("Expected ( after VALUES");

    while (lexer.HasMore()) {
        std::string val = lexer.NextToken();
        if (val == ")") break;
        if (val != ",") {
            stmt->values.push_back(val);
        }
    }

    return stmt;
}

std::unique_ptr<ASTNode> Parser::ParseSelect(Lexer& lexer) {
    auto stmt = std::make_unique<SelectStatement>();
    
    std::string col_token = lexer.NextToken();
    if (col_token != "*") {
        stmt->select_all = false;
        // Parsear lista de columnas... (omitido para brevedad en este ejemplo)
    }

    // Esperar "FROM"
    std::string from_token = lexer.NextToken();
    std::transform(from_token.begin(), from_token.end(), from_token.begin(), ::toupper);
    if (from_token != "FROM") throw std::runtime_error("Expected FROM after SELECT list");

    stmt->table_name = lexer.NextToken();

    // Opcional: WHERE
    if (lexer.HasMore()) {
        std::string where_token = lexer.NextToken();
        std::transform(where_token.begin(), where_token.end(), where_token.begin(), ::toupper);
        if (where_token == "WHERE") {
            stmt->where_clause.active = true;
            stmt->where_clause.column = lexer.NextToken();
            if (lexer.NextToken() != "=") throw std::runtime_error("Expected = in WHERE clause");
            stmt->where_clause.value = lexer.NextToken();
            
            // Limpieza simple de comillas si existen (ej: 'Alice')
            if (stmt->where_clause.value.front() == '\'' && stmt->where_clause.value.back() == '\'') {
                stmt->where_clause.value = stmt->where_clause.value.substr(1, stmt->where_clause.value.size() - 2);
            }
        }
    }

    return stmt;
}

} // namespace megatron
