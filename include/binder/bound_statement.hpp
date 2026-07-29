#pragma once

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include "catalog/catalog.hpp"
#include "storage/record/schema.hpp"
#include "storage/record/type_id.hpp"

namespace megatron {
namespace binder {

enum class BoundExpressionType {
    COLUMN_REF,
    LITERAL,
    BINARY_OP,
    AGGREGATE
};

class BoundExpression {
public:
    virtual ~BoundExpression() = default;
    virtual BoundExpressionType GetExprType() const = 0;
    virtual TypeId GetReturnType() const = 0;
};

class BoundColumnRefExpression : public BoundExpression {
public:
    BoundColumnRefExpression(std::string col_name, size_t col_idx, TypeId return_type, std::string table_name = "")
        : col_name_(std::move(col_name)), col_idx_(col_idx), return_type_(return_type), table_name_(std::move(table_name)) {}

    BoundExpressionType GetExprType() const override { return BoundExpressionType::COLUMN_REF; }
    TypeId GetReturnType() const override { return return_type_; }

    const std::string& GetColName() const { return col_name_; }
    size_t GetColIdx() const { return col_idx_; }
    const std::string& GetTableName() const { return table_name_; }

private:
    std::string col_name_;
    size_t col_idx_;
    TypeId return_type_;
    std::string table_name_;
};

class BoundLiteralExpression : public BoundExpression {
public:
    BoundLiteralExpression(std::string value, TypeId return_type)
        : value_(std::move(value)), return_type_(return_type) {}

    BoundExpressionType GetExprType() const override { return BoundExpressionType::LITERAL; }
    TypeId GetReturnType() const override { return return_type_; }

    const std::string& GetValue() const { return value_; }

private:
    std::string value_;
    TypeId return_type_;
};

class BoundBinaryOpExpression : public BoundExpression {
public:
    BoundBinaryOpExpression(std::string op_type, std::unique_ptr<BoundExpression> left, std::unique_ptr<BoundExpression> right, TypeId return_type = TypeId::INTEGER)
        : op_type_(std::move(op_type)), left_(std::move(left)), right_(std::move(right)), return_type_(return_type) {}

    BoundExpressionType GetExprType() const override { return BoundExpressionType::BINARY_OP; }
    TypeId GetReturnType() const override { return return_type_; }

    const std::string& GetOpType() const { return op_type_; }
    const BoundExpression* GetLeft() const { return left_.get(); }
    const BoundExpression* GetRight() const { return right_.get(); }

private:
    std::string op_type_;
    std::unique_ptr<BoundExpression> left_;
    std::unique_ptr<BoundExpression> right_;
    TypeId return_type_;
};

class BoundAggregateExpression : public BoundExpression {
public:
    BoundAggregateExpression(std::string func_name, std::unique_ptr<BoundExpression> expr, bool is_star, TypeId return_type)
        : func_name_(std::move(func_name)), expr_(std::move(expr)), is_star_(is_star), return_type_(return_type) {}

    BoundExpressionType GetExprType() const override { return BoundExpressionType::AGGREGATE; }
    TypeId GetReturnType() const override { return return_type_; }

    const std::string& GetFuncName() const { return func_name_; }
    const BoundExpression* GetExpr() const { return expr_.get(); }
    bool IsStar() const { return is_star_; }

private:
    std::string func_name_;
    std::unique_ptr<BoundExpression> expr_;
    bool is_star_;
    TypeId return_type_;
};

enum class BoundTableRefType {
    BASE_TABLE,
    JOIN_TABLE
};

class BoundTableRef {
public:
    virtual ~BoundTableRef() = default;
    virtual BoundTableRefType GetRefType() const = 0;
    virtual const Schema& GetSchema() const = 0;
};

class BoundBaseTableRef : public BoundTableRef {
public:
    BoundBaseTableRef(std::string table_name, const TableMetadata* meta, std::string alias = "")
        : table_name_(std::move(table_name)), meta_(meta), alias_(std::move(alias)) {}

    BoundTableRefType GetRefType() const override { return BoundTableRefType::BASE_TABLE; }
    const Schema& GetSchema() const override { return meta_->schema; }
    const std::string& GetTableName() const { return table_name_; }
    const TableMetadata* GetTableMeta() const { return meta_; }
    const std::string& GetAlias() const { return alias_; }

private:
    std::string table_name_;
    const TableMetadata* meta_{nullptr};
    std::string alias_;
};

class BoundJoinTableRef : public BoundTableRef {
public:
    BoundJoinTableRef(std::unique_ptr<BoundTableRef> left, std::unique_ptr<BoundTableRef> right, std::string join_type, std::unique_ptr<BoundExpression> condition, Schema schema)
        : left_(std::move(left)), right_(std::move(right)), join_type_(std::move(join_type)), condition_(std::move(condition)), schema_(std::move(schema)) {}

    BoundTableRefType GetRefType() const override { return BoundTableRefType::JOIN_TABLE; }
    const Schema& GetSchema() const override { return schema_; }
    const BoundTableRef* GetLeft() const { return left_.get(); }
    const BoundTableRef* GetRight() const { return right_.get(); }
    const std::string& GetJoinType() const { return join_type_; }
    const BoundExpression* GetCondition() const { return condition_.get(); }

private:
    std::unique_ptr<BoundTableRef> left_;
    std::unique_ptr<BoundTableRef> right_;
    std::string join_type_;
    std::unique_ptr<BoundExpression> condition_;
    Schema schema_;
};

enum class BoundStatementType {
    SELECT
};

class BoundStatement {
public:
    virtual ~BoundStatement() = default;
    virtual BoundStatementType GetType() const = 0;
};

class BoundSelectStatement : public BoundStatement {
public:
    BoundStatementType GetType() const override { return BoundStatementType::SELECT; }

    std::unique_ptr<BoundTableRef> from_table;
    std::vector<std::unique_ptr<BoundExpression>> select_list;
    std::unique_ptr<BoundExpression> where_clause;
    std::vector<std::unique_ptr<BoundExpression>> group_by;
    std::unique_ptr<BoundExpression> having_clause;
    Schema select_schema;
};

} // namespace binder
} // namespace megatron
