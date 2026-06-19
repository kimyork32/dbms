#include "parser/parser.hpp"
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace megatron {

// ================= Lexer Implementation =================

Lexer::Lexer(std::string source) {
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

void Parser::ExpectKeyword(Lexer& lexer, const std::string& expected_keyword) {
    std::string token = lexer.NextToken();
    std::transform(token.begin(), token.end(), token.begin(), ::toupper);
    if (token != expected_keyword) {
        throw std::runtime_error("Expected " + expected_keyword + " but got " + token);
    }
}

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
    } else if (first_token == "UPDATE") {
        return ParseUpdate(lexer);
    } else if (first_token == "DELETE") {
        return ParseDelete(lexer);
    } else {
        throw std::runtime_error("Unknown command: " + first_token);
    }
}

std::unique_ptr<ASTNode> Parser::ParseCreate(Lexer& lexer) {
    auto stmt = std::make_unique<CreateStatement>();
    
    ExpectKeyword(lexer, "TABLE");
    stmt->table_name = lexer.NextToken();

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
    
    ExpectKeyword(lexer, "INTO");
    stmt->table_name = lexer.NextToken();

    ExpectKeyword(lexer, "VALUES");

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

    ExpectKeyword(lexer, "FROM");
    stmt->table_name = lexer.NextToken();

    if (lexer.HasMore()) {
        std::string where_token = lexer.NextToken();
        std::transform(where_token.begin(), where_token.end(), where_token.begin(), ::toupper);
        if (where_token == "WHERE") {
            stmt->where_clause.active = true;
            stmt->where_clause.column = lexer.NextToken();
            if (lexer.NextToken() != "=") throw std::runtime_error("Expected = in WHERE clause");
            stmt->where_clause.value = lexer.NextToken();
            
            if (!stmt->where_clause.value.empty() && stmt->where_clause.value.front() == '\'' && stmt->where_clause.value.back() == '\'') {
                stmt->where_clause.value = stmt->where_clause.value.substr(1, stmt->where_clause.value.size() - 2);
            }
        }
    }

    return stmt;
}

std::unique_ptr<ASTNode> Parser::ParseUpdate(Lexer& lexer) {
    auto stmt = std::make_unique<UpdateStatement>();

    stmt->table_name = lexer.NextToken();

    ExpectKeyword(lexer, "SET");
    stmt->set_column = lexer.NextToken();

    if (lexer.NextToken() != "=") throw std::runtime_error("Expected = in SET clause");

    stmt->set_value = lexer.NextToken();
    if (!stmt->set_value.empty() && stmt->set_value.front() == '\'' && stmt->set_value.back() == '\'') {
        stmt->set_value = stmt->set_value.substr(1, stmt->set_value.size() - 2);
    }

    if (lexer.HasMore()) {
        std::string where_token = lexer.NextToken();
        std::transform(where_token.begin(), where_token.end(), where_token.begin(), ::toupper);
        if (where_token == "WHERE") {
            stmt->where_clause.active = true;
            stmt->where_clause.column = lexer.NextToken();
            if (lexer.NextToken() != "=") throw std::runtime_error("Expected = in WHERE clause");
            stmt->where_clause.value = lexer.NextToken();
            
            if (!stmt->where_clause.value.empty() && stmt->where_clause.value.front() == '\'' && stmt->where_clause.value.back() == '\'') {
                stmt->where_clause.value = stmt->where_clause.value.substr(1, stmt->where_clause.value.size() - 2);
            }
        }
    }

    return stmt;
}

std::unique_ptr<ASTNode> Parser::ParseDelete(Lexer& lexer) {
    auto stmt = std::make_unique<DeleteStatement>();

    ExpectKeyword(lexer, "FROM");
    stmt->table_name = lexer.NextToken();

    if (lexer.HasMore()) {
        std::string where_token = lexer.NextToken();
        std::transform(where_token.begin(), where_token.end(), where_token.begin(), ::toupper);
        if (where_token == "WHERE") {
            stmt->where_clause.active = true;
            stmt->where_clause.column = lexer.NextToken();
            if (lexer.NextToken() != "=") throw std::runtime_error("Expected = in WHERE clause");
            stmt->where_clause.value = lexer.NextToken();
            
            if (!stmt->where_clause.value.empty() && stmt->where_clause.value.front() == '\'' && stmt->where_clause.value.back() == '\'') {
                stmt->where_clause.value = stmt->where_clause.value.substr(1, stmt->where_clause.value.size() - 2);
            }
        }
    }

    return stmt;
}

} // namespace megatron
