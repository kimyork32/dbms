#pragma once

#include "ast.hpp"
#include "lexer.hpp"
#include <memory>
#include <string>
#include <vector>

namespace megatron {

/**
 * @brief result structure for parse query api
 */
struct ParseResult {
    bool success{false};
    std::string error_message;
    std::unique_ptr<ASTNode> ast;
};

/**
 * @brief top-level function to parse sql string into parse result
 * @param sql sql query string
 * @return ParseResult containing success flag, error message, and AST root pointer
 */
ParseResult ParseQuery(const std::string& sql);

/**
 * @brief manual sql parser (recursive descent) responsible for transforming tokens into an ast
 */
class Parser {
public:
    explicit Parser(Lexer lexer);

    /**
     * @brief static entry point to parse a query string into ast
     */
    static std::unique_ptr<ASTNode> Parse(const std::string& query);

    /**
     * @brief parses a complete sql statement
     */
    std::unique_ptr<ASTNode> ParseStatement();

private:
    std::unique_ptr<ASTNode> ParseCreate();
    std::unique_ptr<ASTNode> ParseInsert();
    std::unique_ptr<ASTNode> ParseSelect();
    std::unique_ptr<ASTNode> ParseUpdate();
    std::unique_ptr<ASTNode> ParseDelete();

    // clause parsing helpers
    std::unique_ptr<TableRef> ParseTableRef();
    std::unique_ptr<TableRef> ParseBaseTableRef();
    std::unique_ptr<ExpressionNode> ParseExpression();
    std::unique_ptr<ExpressionNode> ParseLogicalOr();
    std::unique_ptr<ExpressionNode> ParseLogicalAnd();
    std::unique_ptr<ExpressionNode> ParseComparison();
    std::unique_ptr<ExpressionNode> ParseAdditive();
    std::unique_ptr<ExpressionNode> ParseMultiplicative();
    std::unique_ptr<ExpressionNode> ParsePrimary();

    // utility helper methods
    Token Expect(TokenType expected_type, const std::string& error_msg);
    Token ExpectKeyword(TokenType expected_type, const std::string& expected_str);
    bool Match(TokenType type);

    Lexer lexer_;
};

} // namespace megatron
