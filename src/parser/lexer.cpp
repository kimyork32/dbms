#include "parser/lexer.hpp"
#include <cctype>
#include <unordered_map>
#include <algorithm>

namespace megatron {

Lexer::Lexer(std::string source) : source_(std::move(source)), current_pos_(0) {
    Tokenize();
}

void Lexer::Tokenize() {
    tokens_.clear();
    size_t i = 0;
    size_t len = source_.length();

    static const std::unordered_map<std::string, TokenType> keywords = {
        {"SELECT", TokenType::KEYWORD_SELECT},
        {"FROM", TokenType::KEYWORD_FROM},
        {"JOIN", TokenType::KEYWORD_JOIN},
        {"INNER", TokenType::KEYWORD_INNER},
        {"ON", TokenType::KEYWORD_ON},
        {"WHERE", TokenType::KEYWORD_WHERE},
        {"GROUP", TokenType::KEYWORD_GROUP},
        {"BY", TokenType::KEYWORD_BY},
        {"HAVING", TokenType::KEYWORD_HAVING},
        {"SUM", TokenType::KEYWORD_SUM},
        {"COUNT", TokenType::KEYWORD_COUNT},
        {"AVG", TokenType::KEYWORD_AVG},
        {"MIN", TokenType::KEYWORD_MIN},
        {"MAX", TokenType::KEYWORD_MAX},
        {"AS", TokenType::KEYWORD_AS},
        {"AND", TokenType::KEYWORD_AND},
        {"OR", TokenType::KEYWORD_OR},
        {"CREATE", TokenType::KEYWORD_CREATE},
        {"TABLE", TokenType::KEYWORD_TABLE},
        {"INSERT", TokenType::KEYWORD_INSERT},
        {"INTO", TokenType::KEYWORD_INTO},
        {"VALUES", TokenType::KEYWORD_VALUES},
        {"UPDATE", TokenType::KEYWORD_UPDATE},
        {"SET", TokenType::KEYWORD_SET},
        {"DELETE", TokenType::KEYWORD_DELETE}
    };

    while (i < len) {
        // skip whitespace
        if (std::isspace(static_cast<unsigned char>(source_[i]))) {
            i++;
            continue;
        }

        size_t start_pos = i;
        char c = source_[i];

        // strings ('string' or "string")
        if (c == '\'' || c == '"') {
            char quote = c;
            i++;
            std::string str_val;
            while (i < len && source_[i] != quote) {
                if (source_[i] == '\\' && i + 1 < len) {
                    i++;
                    str_val += source_[i];
                } else {
                    str_val += source_[i];
                }
                i++;
            }
            if (i < len && source_[i] == quote) {
                i++;
            }
            tokens_.emplace_back(TokenType::STRING, str_val, start_pos);
            continue;
        }

        // numbers
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num_val;
            bool has_dot = false;
            while (i < len && (std::isdigit(static_cast<unsigned char>(source_[i])) || source_[i] == '.')) {
                if (source_[i] == '.') {
                    if (has_dot) break;
                    has_dot = true;
                }
                num_val += source_[i];
                i++;
            }
            tokens_.emplace_back(TokenType::NUMBER, num_val, start_pos);
            continue;
        }

        // identifiers and keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string ident;
            while (i < len && (std::isalnum(static_cast<unsigned char>(source_[i])) || source_[i] == '_')) {
                ident += source_[i];
                i++;
            }
            std::string upper_ident = ident;
            std::transform(upper_ident.begin(), upper_ident.end(), upper_ident.begin(), ::toupper);

            auto kw_it = keywords.find(upper_ident);
            if (kw_it != keywords.end()) {
                tokens_.emplace_back(kw_it->second, upper_ident, start_pos);
            } else {
                tokens_.emplace_back(TokenType::IDENTIFIER, ident, start_pos);
            }
            continue;
        }

        // multi-char operators
        if (c == '!' && i + 1 < len && source_[i + 1] == '=') {
            tokens_.emplace_back(TokenType::NOT_EQUAL, "!=", start_pos);
            i += 2;
            continue;
        }
        if (c == '<' && i + 1 < len && source_[i + 1] == '>') {
            tokens_.emplace_back(TokenType::NOT_EQUAL, "<>", start_pos);
            i += 2;
            continue;
        }
        if (c == '<' && i + 1 < len && source_[i + 1] == '=') {
            tokens_.emplace_back(TokenType::LESS_EQUAL, "<=", start_pos);
            i += 2;
            continue;
        }
        if (c == '>' && i + 1 < len && source_[i + 1] == '=') {
            tokens_.emplace_back(TokenType::GREATER_EQUAL, ">=", start_pos);
            i += 2;
            continue;
        }

        // single-char operators and punctuation
        switch (c) {
            case '=':
                tokens_.emplace_back(TokenType::EQUAL, "=", start_pos);
                break;
            case '<':
                tokens_.emplace_back(TokenType::LESS_THAN, "<", start_pos);
                break;
            case '>':
                tokens_.emplace_back(TokenType::GREATER_THAN, ">", start_pos);
                break;
            case '+':
                tokens_.emplace_back(TokenType::PLUS, "+", start_pos);
                break;
            case '-':
                tokens_.emplace_back(TokenType::MINUS, "-", start_pos);
                break;
            case '*':
                tokens_.emplace_back(TokenType::ASTERISK, "*", start_pos);
                break;
            case '/':
                tokens_.emplace_back(TokenType::SLASH, "/", start_pos);
                break;
            case '.':
                tokens_.emplace_back(TokenType::DOT, ".", start_pos);
                break;
            case ',':
                tokens_.emplace_back(TokenType::COMMA, ",", start_pos);
                break;
            case '(':
                tokens_.emplace_back(TokenType::LPAREN, "(", start_pos);
                break;
            case ')':
                tokens_.emplace_back(TokenType::RPAREN, ")", start_pos);
                break;
            case ';':
                tokens_.emplace_back(TokenType::SEMICOLON, ";", start_pos);
                break;
            default:
                tokens_.emplace_back(TokenType::INVALID, std::string(1, c), start_pos);
                break;
        }
        i++;
    }

    tokens_.emplace_back(TokenType::END, "", len);
    current_pos_ = 0;
}

Token Lexer::NextToken() {
    if (current_pos_ < tokens_.size()) {
        return tokens_[current_pos_++];
    }
    return Token(TokenType::END, "", source_.length());
}

std::string Lexer::NextTokenText() {
    return NextToken().text;
}

Token Lexer::Peek() const {
    if (current_pos_ < tokens_.size()) {
        return tokens_[current_pos_];
    }
    return Token(TokenType::END, "", source_.length());
}

std::string Lexer::PeekText() const {
    return Peek().text;
}

bool Lexer::HasMore() const {
    return current_pos_ < tokens_.size() && tokens_[current_pos_].type != TokenType::END;
}

} // namespace megatron
