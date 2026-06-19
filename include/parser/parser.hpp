#pragma once
#include "ast.hpp"
#include <string>
#include <vector>
#include <memory>

namespace megatron {

/**
 * @brief simple tokenizer to separate sql words
 */
class Lexer {
public:
    explicit Lexer(std::string source);

    /**
     * @brief returns the next token and advances the position
     * @return next token string
     */
    std::string NextToken();

    /**
     * @brief checks if there are more tokens
     * @return true if more tokens exist
     */
    bool HasMore() const;

    /**
     * @brief returns the next token without advancing the position
     * @return next token string
     */
    std::string Peek() const;

private:
    std::vector<std::string> tokens_;
    size_t current_pos_ = 0;
};

/**
 * @brief manual sql parser (recursive descent) responsible for transforming text into an ast
 */
class Parser {
public:
    /**
     * @brief parses an sql query string into an ast
     * @param query the sql query string
     * @return unique pointer to the root ast node
     */
    static std::unique_ptr<ASTNode> Parse(const std::string& query);

private:
    static std::unique_ptr<ASTNode> ParseCreate(Lexer& lexer);
    static std::unique_ptr<ASTNode> ParseInsert(Lexer& lexer);
    static std::unique_ptr<ASTNode> ParseSelect(Lexer& lexer);
    static std::unique_ptr<ASTNode> ParseUpdate(Lexer& lexer);
    static std::unique_ptr<ASTNode> ParseDelete(Lexer& lexer);

    /**
     * @brief expects a specific sql keyword, throws if not found
     * @param lexer the active lexer
     * @param expected_keyword the keyword to expect (must be uppercase)
     */
    static void ExpectKeyword(Lexer& lexer, const std::string& expected_keyword);
};

} // namespace megatron
