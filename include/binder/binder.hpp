#pragma once

#include <memory>
#include <string>
#include <stdexcept>

#include "catalog/catalog.hpp"
#include "parser/ast.hpp"
#include "binder/bound_statement.hpp"

namespace megatron {
namespace binder {

class Binder {
public:
    explicit Binder(const Catalog& catalog) : catalog_(catalog) {}

    std::unique_ptr<BoundSelectStatement> BindSelect(const SelectStatement& stmt);
    std::unique_ptr<BoundTableRef> BindTableRef(const TableRef& table_ref);
    std::unique_ptr<BoundExpression> BindExpression(const ExpressionNode& expr, const Schema& current_schema, const std::string& current_table_name = "");

private:
    const Catalog& catalog_;
};

} // namespace binder

using binder::Binder;
using binder::BoundSelectStatement;

} // namespace megatron
