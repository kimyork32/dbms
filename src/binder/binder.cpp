#include "binder/binder.hpp"
#include <iostream>

namespace megatron {
namespace binder {

std::unique_ptr<BoundSelectStatement> Binder::BindSelect(const SelectStatement& stmt) {
    auto bound_stmt = std::make_unique<BoundSelectStatement>();

    const TableRef* from_ref = stmt.from_table.get();
    std::unique_ptr<BaseTableRef> temp_base_ref;

    if (from_ref == nullptr) {
        if (!stmt.table_name.empty()) {
            temp_base_ref = std::make_unique<BaseTableRef>(stmt.table_name);
            from_ref = temp_base_ref.get();
        } else {
            throw std::runtime_error("No FROM table specified in select statement");
        }
    }

    bound_stmt->from_table = BindTableRef(*from_ref);
    const Schema& current_schema = bound_stmt->from_table->GetSchema();

    std::string current_table_name;
    if (bound_stmt->from_table->GetRefType() == BoundTableRefType::BASE_TABLE) {
        const auto* base_table = static_cast<const BoundBaseTableRef*>(bound_stmt->from_table.get());
        current_table_name = base_table->GetTableName();
    }

    // Bind Select List
    if (stmt.select_all || stmt.select_list.empty()) {
        for (size_t i = 0; i < current_schema.columns.size(); ++i) {
            const auto& col = current_schema.columns[i];
            bound_stmt->select_list.push_back(
                std::make_unique<BoundColumnRefExpression>(col.name, i, col.type, current_table_name)
            );
            bound_stmt->select_schema.AddColumn(col.name, col.type);
        }
    } else {
        for (const auto& expr : stmt.select_list) {
            if (expr->GetExprType() == ExpressionType::COLUMN_REF) {
                const auto* col_ref = static_cast<const ColumnRefExpression*>(expr.get());
                if (col_ref->column_name == "*") {
                    for (size_t i = 0; i < current_schema.columns.size(); ++i) {
                        const auto& col = current_schema.columns[i];
                        bound_stmt->select_list.push_back(
                            std::make_unique<BoundColumnRefExpression>(col.name, i, col.type, current_table_name)
                        );
                        bound_stmt->select_schema.AddColumn(col.name, col.type);
                    }
                    continue;
                }
            }

            auto bound_expr = BindExpression(*expr, current_schema, current_table_name);
            std::string out_col_name;
            if (bound_expr->GetExprType() == BoundExpressionType::COLUMN_REF) {
                out_col_name = static_cast<const BoundColumnRefExpression*>(bound_expr.get())->GetColName();
            } else if (bound_expr->GetExprType() == BoundExpressionType::AGGREGATE) {
                out_col_name = static_cast<const BoundAggregateExpression*>(bound_expr.get())->GetFuncName();
            } else {
                out_col_name = "col_" + std::to_string(bound_stmt->select_list.size());
            }

            bound_stmt->select_schema.AddColumn(out_col_name, bound_expr->GetReturnType());
            bound_stmt->select_list.push_back(std::move(bound_expr));
        }
    }

    // Bind WHERE clause
    if (stmt.where_clause) {
        bound_stmt->where_clause = BindExpression(*stmt.where_clause, current_schema, current_table_name);
    }

    // Bind GROUP BY
    for (const auto& g_expr : stmt.group_by) {
        bound_stmt->group_by.push_back(BindExpression(*g_expr, current_schema, current_table_name));
    }

    // Bind HAVING
    if (stmt.having_clause) {
        bound_stmt->having_clause = BindExpression(*stmt.having_clause, current_schema, current_table_name);
    }

    return bound_stmt;
}

std::unique_ptr<BoundTableRef> Binder::BindTableRef(const TableRef& table_ref) {
    if (table_ref.GetRefType() == TableRefType::BASE_TABLE) {
        const auto& base_ref = static_cast<const BaseTableRef&>(table_ref);
        const TableMetadata* meta = catalog_.GetTable(base_ref.table_name);
        if (meta == nullptr) {
            throw std::runtime_error("Table not found: " + base_ref.table_name);
        }
        return std::make_unique<BoundBaseTableRef>(base_ref.table_name, meta, base_ref.alias);
    } else if (table_ref.GetRefType() == TableRefType::JOIN_TABLE) {
        const auto& join_ref = static_cast<const JoinTableRef&>(table_ref);
        if (!join_ref.left || !join_ref.right) {
            throw std::runtime_error("Join statement missing left or right table");
        }

        auto bound_left = BindTableRef(*join_ref.left);
        auto bound_right = BindTableRef(*join_ref.right);

        Schema combined_schema;
        for (const auto& col : bound_left->GetSchema().columns) {
            combined_schema.AddColumn(col.name, col.type);
        }
        for (const auto& col : bound_right->GetSchema().columns) {
            combined_schema.AddColumn(col.name, col.type);
        }

        std::unique_ptr<BoundExpression> bound_cond = nullptr;
        if (join_ref.join_condition) {
            bound_cond = BindExpression(*join_ref.join_condition, combined_schema);
        }

        return std::make_unique<BoundJoinTableRef>(
            std::move(bound_left),
            std::move(bound_right),
            join_ref.join_type,
            std::move(bound_cond),
            combined_schema
        );
    }

    throw std::runtime_error("Unsupported TableRef type");
}

std::unique_ptr<BoundExpression> Binder::BindExpression(const ExpressionNode& expr, const Schema& current_schema, const std::string& current_table_name) {
    switch (expr.GetExprType()) {
        case ExpressionType::COLUMN_REF: {
            const auto& col_ref = static_cast<const ColumnRefExpression&>(expr);
            int idx = current_schema.GetColIdx(col_ref.column_name);
            if (idx == -1) {
                throw std::runtime_error("Column not found: " + col_ref.column_name);
            }
            TypeId type = current_schema.columns[static_cast<size_t>(idx)].type;
            std::string tbl = col_ref.table_name.empty() ? current_table_name : col_ref.table_name;
            return std::make_unique<BoundColumnRefExpression>(col_ref.column_name, static_cast<size_t>(idx), type, tbl);
        }

        case ExpressionType::LITERAL: {
            const auto& lit = static_cast<const LiteralExpression&>(expr);
            TypeId type = TypeId::INTEGER;
            if (lit.value_type == "VARCHAR" || lit.value_type == "STRING") {
                type = TypeId::VARCHAR;
            } else if (lit.value_type == "SMALLINT") {
                type = TypeId::SMALLINT;
            } else if (lit.value_type == "INTEGER" || lit.value_type == "INT") {
                type = TypeId::INTEGER;
            }
            return std::make_unique<BoundLiteralExpression>(lit.value, type);
        }

        case ExpressionType::BINARY_OP: {
            const auto& bin = static_cast<const BinaryOpExpression&>(expr);
            if (!bin.left || !bin.right) {
                throw std::runtime_error("Binary operator missing left or right expression");
            }
            auto left = BindExpression(*bin.left, current_schema, current_table_name);
            auto right = BindExpression(*bin.right, current_schema, current_table_name);
            return std::make_unique<BoundBinaryOpExpression>(bin.op_type, std::move(left), std::move(right), TypeId::INTEGER);
        }

        case ExpressionType::AGGREGATE: {
            const auto& agg = static_cast<const AggregateExpression&>(expr);
            std::unique_ptr<BoundExpression> child = nullptr;
            TypeId ret_type = TypeId::INTEGER;
            if (!agg.is_star && agg.expr != nullptr) {
                child = BindExpression(*agg.expr, current_schema, current_table_name);
                ret_type = child->GetReturnType();
            }
            if (agg.func_name == "COUNT") {
                ret_type = TypeId::INTEGER;
            }
            return std::make_unique<BoundAggregateExpression>(agg.func_name, std::move(child), agg.is_star, ret_type);
        }
    }

    throw std::runtime_error("Unknown ExpressionNode type");
}

} // namespace binder
} // namespace megatron
