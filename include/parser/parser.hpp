#pragma once
#include "ast.hpp"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace megatron {

/**
 * @brief Tokenizador simple para separar palabras de SQL.
 */
class Lexer {
public:
    explicit Lexer(std::string source);
    std::string NextToken();
    bool HasMore() const;
    std::string Peek() const;

private:
    std::vector<std::string> tokens_;
    size_t current_pos_ = 0;
};

/**
 * @brief Parser de SQL manual (Recursive Descent)
 * Encargado de transformar el texto en un AST.
 */
class Parser {
public:
    static std::unique_ptr<ASTNode> Parse(const std::string& query);

private:
    static std::unique_ptr<ASTNode> ParseCreate(Lexer& lexer);
    static std::unique_ptr<ASTNode> ParseInsert(Lexer& lexer);
    static std::unique_ptr<ASTNode> ParseSelect(Lexer& lexer);
};

} // namespace megatron
