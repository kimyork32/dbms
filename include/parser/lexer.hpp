#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace megatron {

/**
 * @brief supported token types in sql queries
 */
enum class TokenType {
    // keywords
    KEYWORD_SELECT,
    KEYWORD_FROM,
    KEYWORD_JOIN,
    KEYWORD_INNER,
    KEYWORD_ON,
    KEYWORD_WHERE,
    KEYWORD_GROUP,
    KEYWORD_BY,
    KEYWORD_HAVING,
    KEYWORD_SUM,
    KEYWORD_COUNT,
    KEYWORD_AVG,
    KEYWORD_MIN,
    KEYWORD_MAX,
    KEYWORD_AS,
    KEYWORD_AND,
    KEYWORD_OR,
    KEYWORD_CREATE,
    KEYWORD_TABLE,
    KEYWORD_INSERT,
    KEYWORD_INTO,
    KEYWORD_VALUES,
    KEYWORD_UPDATE,
    KEYWORD_SET,
    KEYWORD_DELETE,

    // literals and identifiers
    IDENTIFIER,
    NUMBER,
    STRING,

    // operators
    EQUAL,          // =
    NOT_EQUAL,      // != or <>
    GREATER_THAN,   // >
    LESS_THAN,      // <
    GREATER_EQUAL,  // >=
    LESS_EQUAL,     // <=
    PLUS,           // +
    MINUS,          // -
    ASTERISK,       // *
    SLASH,          // /

    // punctuation
    DOT,            // .
    COMMA,          // ,
    LPAREN,         // (
    RPAREN,         // )
    SEMICOLON,      // ;

    // special
    END,
    INVALID
};

/**
 * @brief represents a single sql token
 */
struct Token {
    TokenType type{TokenType::END};
    std::string text;
    size_t position{0};

    Token() = default;
    Token(TokenType t, std::string txt, size_t pos = 0)
        : type(t), text(std::move(txt)), position(pos) {}
};

/**
 * @brief sql lexer for tokenizing sql source strings
 */
class Lexer {
public:
    explicit Lexer(std::string source);

    /**
     * @brief returns the next token and advances position
     * @return next token
     */
    Token NextToken();

    /**
     * @brief returns the next token text and advances position
     * @return token text string
     */
    std::string NextTokenText();

    /**
     * @brief returns the next token without advancing position
     * @return next token
     */
    Token Peek() const;

    /**
     * @brief returns the next token text without advancing position
     * @return token text string
     */
    std::string PeekText() const;

    /**
     * @brief checks if there are more tokens remaining
     * @return true if more non-end tokens exist
     */
    bool HasMore() const;

    /**
     * @brief returns current token index position
     * @return token index
     */
    size_t GetPosition() const { return current_pos_; }

private:
    void Tokenize();

    std::string source_;
    std::vector<Token> tokens_;
    size_t current_pos_{0};
};

} // namespace megatron
