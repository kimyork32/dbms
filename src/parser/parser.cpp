#include "parser/parser.hpp"
#include <stdexcept>
#include <utility>

namespace megatron {

ParseResult ParseQuery(const std::string& sql) {
    ParseResult result;
    try {
        Lexer lexer(sql);
        Parser parser(lexer);
        result.ast = parser.ParseStatement();
        result.success = (result.ast != nullptr);
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
        result.ast = nullptr;
    }
    return result;
}

Parser::Parser(Lexer lexer) : lexer_(std::move(lexer)) {}

std::unique_ptr<ASTNode> Parser::Parse(const std::string& query) {
    ParseResult res = ParseQuery(query);
    if (!res.success) {
        throw std::runtime_error(res.error_message);
    }
    return std::move(res.ast);
}

Token Parser::Expect(TokenType expected_type, const std::string& error_msg) {
    Token tok = lexer_.NextToken();
    if (tok.type != expected_type) {
        throw std::runtime_error(error_msg + " (got '" + tok.text + "')");
    }
    return tok;
}

Token Parser::ExpectKeyword(TokenType expected_type, const std::string& expected_str) {
    Token tok = lexer_.NextToken();
    if (tok.type != expected_type) {
        throw std::runtime_error("Expected keyword " + expected_str + " but got '" + tok.text + "'");
    }
    return tok;
}

bool Parser::Match(TokenType type) {
    if (lexer_.Peek().type == type) {
        lexer_.NextToken();
        return true;
    }
    return false;
}

std::unique_ptr<ASTNode> Parser::ParseStatement() {
    Token tok = lexer_.Peek();
    if (tok.type == TokenType::KEYWORD_SELECT) {
        return ParseSelect();
    } else if (tok.type == TokenType::KEYWORD_CREATE) {
        return ParseCreate();
    } else if (tok.type == TokenType::KEYWORD_INSERT) {
        return ParseInsert();
    } else if (tok.type == TokenType::KEYWORD_UPDATE) {
        return ParseUpdate();
    } else if (tok.type == TokenType::KEYWORD_DELETE) {
        return ParseDelete();
    } else {
        throw std::runtime_error("Unexpected token at start of statement: " + tok.text);
    }
}

std::unique_ptr<ASTNode> Parser::ParseSelect() {
    ExpectKeyword(TokenType::KEYWORD_SELECT, "SELECT");
    auto stmt = std::make_unique<SelectStatement>();

    if (Match(TokenType::ASTERISK)) {
        stmt->select_all = true;
    } else {
        stmt->select_all = false;
        while (lexer_.HasMore()) {
            auto expr = ParseExpression();
            std::string alias;
            if (Match(TokenType::KEYWORD_AS)) {
                alias = Expect(TokenType::IDENTIFIER, "Expected alias identifier after AS").text;
            } else if (lexer_.Peek().type == TokenType::IDENTIFIER) {
                alias = lexer_.NextTokenText();
            }

            if (!alias.empty()) {
                if (expr->GetExprType() == ExpressionType::COLUMN_REF) {
                    static_cast<ColumnRefExpression*>(expr.get())->alias = alias;
                } else if (expr->GetExprType() == ExpressionType::AGGREGATE) {
                    static_cast<AggregateExpression*>(expr.get())->alias = alias;
                }
            }

            stmt->select_list.push_back(std::move(expr));
            if (!Match(TokenType::COMMA)) {
                break;
            }
        }
    }

    if (Match(TokenType::KEYWORD_FROM)) {
        stmt->from_table = ParseTableRef();
        if (stmt->from_table && stmt->from_table->GetRefType() == TableRefType::BASE_TABLE) {
            stmt->table_name = static_cast<BaseTableRef*>(stmt->from_table.get())->table_name;
        }
    }

    if (Match(TokenType::KEYWORD_WHERE)) {
        stmt->where_clause = ParseExpression();
    }

    if (Match(TokenType::KEYWORD_GROUP)) {
        ExpectKeyword(TokenType::KEYWORD_BY, "BY");
        while (lexer_.HasMore()) {
            stmt->group_by.push_back(ParseExpression());
            if (!Match(TokenType::COMMA)) {
                break;
            }
        }
    }

    if (Match(TokenType::KEYWORD_HAVING)) {
        stmt->having_clause = ParseExpression();
    }

    Match(TokenType::SEMICOLON);
    return stmt;
}

std::unique_ptr<TableRef> Parser::ParseBaseTableRef() {
    std::string tbl_name = Expect(TokenType::IDENTIFIER, "Expected table name").text;
    std::string alias;
    if (Match(TokenType::KEYWORD_AS)) {
        alias = Expect(TokenType::IDENTIFIER, "Expected alias after AS").text;
    } else if (lexer_.Peek().type == TokenType::IDENTIFIER) {
        alias = lexer_.NextTokenText();
    }
    return std::make_unique<BaseTableRef>(tbl_name, alias);
}

std::unique_ptr<TableRef> Parser::ParseTableRef() {
    auto left = ParseBaseTableRef();

    while (lexer_.Peek().type == TokenType::KEYWORD_JOIN || lexer_.Peek().type == TokenType::KEYWORD_INNER) {
        std::string join_type = "INNER";
        if (Match(TokenType::KEYWORD_INNER)) {
            ExpectKeyword(TokenType::KEYWORD_JOIN, "JOIN");
        } else {
            ExpectKeyword(TokenType::KEYWORD_JOIN, "JOIN");
        }
        auto right = ParseBaseTableRef();
        ExpectKeyword(TokenType::KEYWORD_ON, "ON");
        auto cond = ParseExpression();
        left = std::make_unique<JoinTableRef>(std::move(left), std::move(right), join_type, std::move(cond));
    }
    return left;
}

std::unique_ptr<ExpressionNode> Parser::ParseExpression() {
    return ParseLogicalOr();
}

std::unique_ptr<ExpressionNode> Parser::ParseLogicalOr() {
    auto left = ParseLogicalAnd();
    while (Match(TokenType::KEYWORD_OR)) {
        auto right = ParseLogicalAnd();
        left = std::make_unique<BinaryOpExpression>("OR", std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExpressionNode> Parser::ParseLogicalAnd() {
    auto left = ParseComparison();
    while (Match(TokenType::KEYWORD_AND)) {
        auto right = ParseComparison();
        left = std::make_unique<BinaryOpExpression>("AND", std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExpressionNode> Parser::ParseComparison() {
    auto left = ParseAdditive();
    TokenType peek_t = lexer_.Peek().type;
    if (peek_t == TokenType::EQUAL || peek_t == TokenType::NOT_EQUAL ||
        peek_t == TokenType::GREATER_THAN || peek_t == TokenType::LESS_THAN ||
        peek_t == TokenType::GREATER_EQUAL || peek_t == TokenType::LESS_EQUAL) {
        Token op_tok = lexer_.NextToken();
        auto right = ParseAdditive();
        left = std::make_unique<BinaryOpExpression>(op_tok.text, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExpressionNode> Parser::ParseAdditive() {
    auto left = ParseMultiplicative();
    while (lexer_.Peek().type == TokenType::PLUS || lexer_.Peek().type == TokenType::MINUS) {
        Token op_tok = lexer_.NextToken();
        auto right = ParseMultiplicative();
        left = std::make_unique<BinaryOpExpression>(op_tok.text, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExpressionNode> Parser::ParseMultiplicative() {
    auto left = ParsePrimary();
    while (lexer_.Peek().type == TokenType::ASTERISK || lexer_.Peek().type == TokenType::SLASH) {
        Token op_tok = lexer_.NextToken();
        auto right = ParsePrimary();
        left = std::make_unique<BinaryOpExpression>(op_tok.text, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ExpressionNode> Parser::ParsePrimary() {
    Token tok = lexer_.Peek();
    if (tok.type == TokenType::LPAREN) {
        lexer_.NextToken();
        auto expr = ParseExpression();
        Expect(TokenType::RPAREN, "Expected closing parenthesis");
        return expr;
    }

    if (tok.type == TokenType::KEYWORD_SUM || tok.type == TokenType::KEYWORD_COUNT ||
        tok.type == TokenType::KEYWORD_AVG || tok.type == TokenType::KEYWORD_MIN ||
        tok.type == TokenType::KEYWORD_MAX) {
        Token func_tok = lexer_.NextToken();
        Expect(TokenType::LPAREN, "Expected ( after aggregate function");
        bool is_star = false;
        std::unique_ptr<ExpressionNode> sub_expr = nullptr;
        if (func_tok.type == TokenType::KEYWORD_COUNT && Match(TokenType::ASTERISK)) {
            is_star = true;
        } else {
            sub_expr = ParseExpression();
        }
        Expect(TokenType::RPAREN, "Expected ) after aggregate expression");
        return std::make_unique<AggregateExpression>(func_tok.text, std::move(sub_expr), is_star);
    }

    if (tok.type == TokenType::NUMBER) {
        lexer_.NextToken();
        std::string vtype = (tok.text.find('.') != std::string::npos) ? "FLOAT" : "INTEGER";
        return std::make_unique<LiteralExpression>(tok.text, vtype);
    }

    if (tok.type == TokenType::STRING) {
        lexer_.NextToken();
        return std::make_unique<LiteralExpression>(tok.text, "STRING");
    }

    if (tok.type == TokenType::IDENTIFIER) {
        lexer_.NextToken();
        std::string first_name = tok.text;
        if (Match(TokenType::DOT)) {
            std::string col_name = Expect(TokenType::IDENTIFIER, "Expected column name after dot").text;
            return std::make_unique<ColumnRefExpression>(first_name, col_name);
        }
        return std::make_unique<ColumnRefExpression>("", first_name);
    }

    throw std::runtime_error("Unexpected token in expression: " + tok.text);
}

std::unique_ptr<ASTNode> Parser::ParseCreate() {
    ExpectKeyword(TokenType::KEYWORD_CREATE, "CREATE");
    ExpectKeyword(TokenType::KEYWORD_TABLE, "TABLE");
    auto stmt = std::make_unique<CreateStatement>();
    stmt->table_name = Expect(TokenType::IDENTIFIER, "Expected table name").text;
    Expect(TokenType::LPAREN, "Expected ( after table name");
    while (lexer_.Peek().type != TokenType::RPAREN && lexer_.HasMore()) {
        Token col_tok = Expect(TokenType::IDENTIFIER, "Expected column name");
        stmt->columns.push_back(col_tok.text);
        if (!Match(TokenType::COMMA)) {
            break;
        }
    }
    Expect(TokenType::RPAREN, "Expected ) after column list");
    Match(TokenType::SEMICOLON);
    return stmt;
}

std::unique_ptr<ASTNode> Parser::ParseInsert() {
    ExpectKeyword(TokenType::KEYWORD_INSERT, "INSERT");
    ExpectKeyword(TokenType::KEYWORD_INTO, "INTO");
    auto stmt = std::make_unique<InsertStatement>();
    stmt->table_name = Expect(TokenType::IDENTIFIER, "Expected table name").text;
    ExpectKeyword(TokenType::KEYWORD_VALUES, "VALUES");
    Expect(TokenType::LPAREN, "Expected ( after VALUES");
    while (lexer_.Peek().type != TokenType::RPAREN && lexer_.HasMore()) {
        Token val_tok = lexer_.NextToken();
        stmt->values.push_back(val_tok.text);
        if (!Match(TokenType::COMMA)) {
            break;
        }
    }
    Expect(TokenType::RPAREN, "Expected ) after values list");
    Match(TokenType::SEMICOLON);
    return stmt;
}

std::unique_ptr<ASTNode> Parser::ParseUpdate() {
    ExpectKeyword(TokenType::KEYWORD_UPDATE, "UPDATE");
    auto stmt = std::make_unique<UpdateStatement>();
    stmt->table_name = Expect(TokenType::IDENTIFIER, "Expected table name").text;
    ExpectKeyword(TokenType::KEYWORD_SET, "SET");
    stmt->set_column = Expect(TokenType::IDENTIFIER, "Expected column name in SET").text;
    Expect(TokenType::EQUAL, "Expected = in SET clause");
    Token val_tok = lexer_.NextToken();
    stmt->set_value = val_tok.text;
    if (Match(TokenType::KEYWORD_WHERE)) {
        stmt->where_clause.active = true;
        stmt->where_clause.column = Expect(TokenType::IDENTIFIER, "Expected column name in WHERE").text;
        Expect(TokenType::EQUAL, "Expected = in WHERE clause");
        stmt->where_clause.value = lexer_.NextToken().text;
    }
    Match(TokenType::SEMICOLON);
    return stmt;
}

std::unique_ptr<ASTNode> Parser::ParseDelete() {
    ExpectKeyword(TokenType::KEYWORD_DELETE, "DELETE");
    ExpectKeyword(TokenType::KEYWORD_FROM, "FROM");
    auto stmt = std::make_unique<DeleteStatement>();
    stmt->table_name = Expect(TokenType::IDENTIFIER, "Expected table name").text;
    if (Match(TokenType::KEYWORD_WHERE)) {
        stmt->where_clause.active = true;
        stmt->where_clause.column = Expect(TokenType::IDENTIFIER, "Expected column name in WHERE").text;
        Expect(TokenType::EQUAL, "Expected = in WHERE clause");
        stmt->where_clause.value = lexer_.NextToken().text;
    }
    Match(TokenType::SEMICOLON);
    return stmt;
}

} // namespace megatron
