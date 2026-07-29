#include "optimizer/optimizer.hpp"
#include <iostream>
#include <algorithm>

namespace megatron {
namespace optimizer {

std::shared_ptr<execution::AbstractPlanNode> Optimizer::Optimize(const binder::BoundSelectStatement& stmt) {
    if (!stmt.from_table) {
        return nullptr;
    }

    // Phase 1: From-clause / TableRef optimization
    auto plan = OptimizeTableRef(*stmt.from_table);

    // Phase 2: Index Selection Rule (Transforms SeqScan -> IndexScan if applicable)
    if (stmt.where_clause) {
        plan = OptimizeIndexScan(plan, stmt.where_clause.get());
    }

    // Phase 3: WHERE Clause Filter Placement
    if (stmt.where_clause) {
        plan = OptimizeWhere(plan, stmt.where_clause.get());
    }

    // Phase 4: Aggregation Plan Construction
    plan = OptimizeAggregation(plan, stmt);

    // Phase 5: Projection Plan Construction
    plan = OptimizeProjection(plan, stmt);

    // Phase 6: Plan-Aware BufferHint Injection Pass
    InjectBufferHints(plan);

    return plan;
}

std::shared_ptr<execution::AbstractPlanNode> Optimizer::OptimizeTableRef(const binder::BoundTableRef& table_ref) {
    if (table_ref.GetRefType() == binder::BoundTableRefType::BASE_TABLE) {
        const auto& base_ref = static_cast<const binder::BoundBaseTableRef&>(table_ref);
        std::string table_name = base_ref.GetTableName();
        const TableMetadata* meta = catalog_.GetTable(table_name);
        return std::make_shared<execution::SeqScanPlanNode>(
            base_ref.GetSchema(), table_name, meta, BufferHint::DEFAULT
        );
    } else if (table_ref.GetRefType() == binder::BoundTableRefType::JOIN_TABLE) {
        const auto& join_ref = static_cast<const binder::BoundJoinTableRef&>(table_ref);
        auto left_plan = OptimizeTableRef(*join_ref.GetLeft());
        auto right_plan = OptimizeTableRef(*join_ref.GetRight());

        size_t left_key_idx = 0;
        size_t right_key_idx = 0;
        bool is_hash_join_candidate = false;

        if (join_ref.GetCondition() && join_ref.GetCondition()->GetExprType() == binder::BoundExpressionType::BINARY_OP) {
            const auto* bin_op = static_cast<const binder::BoundBinaryOpExpression*>(join_ref.GetCondition());
            if (bin_op->GetOpType() == "=") {
                if (bin_op->GetLeft()->GetExprType() == binder::BoundExpressionType::COLUMN_REF &&
                    bin_op->GetRight()->GetExprType() == binder::BoundExpressionType::COLUMN_REF) {
                    const auto* left_col = static_cast<const binder::BoundColumnRefExpression*>(bin_op->GetLeft());
                    const auto* right_col = static_cast<const binder::BoundColumnRefExpression*>(bin_op->GetRight());

                    size_t left_schema_size = left_plan->GetOutputSchema().columns.size();
                    if (left_col->GetColIdx() < left_schema_size) {
                        left_key_idx = left_col->GetColIdx();
                        right_key_idx = right_col->GetColIdx() >= left_schema_size ? right_col->GetColIdx() - left_schema_size : right_col->GetColIdx();
                    } else {
                        right_key_idx = left_col->GetColIdx() - left_schema_size;
                        left_key_idx = right_col->GetColIdx();
                    }
                    is_hash_join_candidate = true;
                }
            }
        }

        if (is_hash_join_candidate) {
            return std::make_shared<execution::HashJoinPlanNode>(
                join_ref.GetSchema(), left_plan, right_plan, left_key_idx, right_key_idx, BufferHint::DEFAULT
            );
        } else {
            return std::make_shared<execution::NestedLoopJoinPlanNode>(
                join_ref.GetSchema(), left_plan, right_plan, nullptr, BufferHint::DEFAULT
            );
        }
    }

    return nullptr;
}

std::shared_ptr<execution::AbstractPlanNode> Optimizer::OptimizeIndexScan(
    std::shared_ptr<execution::AbstractPlanNode> plan,
    const binder::BoundExpression* where_clause) {
    if (!plan || !where_clause) return plan;

    if (plan->GetType() == execution::PlanType::SeqScan &&
        where_clause->GetExprType() == binder::BoundExpressionType::BINARY_OP) {
        const auto* bin_op = static_cast<const binder::BoundBinaryOpExpression*>(where_clause);
        if (bin_op->GetOpType() == "=") {
            std::string col_name;
            std::string search_key;
            bool found_pattern = false;

            if (bin_op->GetLeft()->GetExprType() == binder::BoundExpressionType::COLUMN_REF &&
                bin_op->GetRight()->GetExprType() == binder::BoundExpressionType::LITERAL) {
                const auto* col_ref = static_cast<const binder::BoundColumnRefExpression*>(bin_op->GetLeft());
                const auto* lit = static_cast<const binder::BoundLiteralExpression*>(bin_op->GetRight());
                col_name = col_ref->GetColName();
                search_key = lit->GetValue();
                found_pattern = true;
            } else if (bin_op->GetRight()->GetExprType() == binder::BoundExpressionType::COLUMN_REF &&
                       bin_op->GetLeft()->GetExprType() == binder::BoundExpressionType::LITERAL) {
                const auto* col_ref = static_cast<const binder::BoundColumnRefExpression*>(bin_op->GetRight());
                const auto* lit = static_cast<const binder::BoundLiteralExpression*>(bin_op->GetLeft());
                col_name = col_ref->GetColName();
                search_key = lit->GetValue();
                found_pattern = true;
            }

            if (found_pattern) {
                const auto* seq_node = static_cast<const execution::SeqScanPlanNode*>(plan.get());
                std::string table_name = seq_node->GetTableName();
                std::string index_name = "idx_" + table_name + "_" + col_name;
                return std::make_shared<execution::IndexScanPlanNode>(
                    seq_node->GetOutputSchema(), table_name, index_name, search_key, seq_node->GetTableMeta(), BufferHint::DEFAULT
                );
            }
        }
    }

    return plan;
}

std::shared_ptr<execution::AbstractPlanNode> Optimizer::OptimizeWhere(
    std::shared_ptr<execution::AbstractPlanNode> plan,
    const binder::BoundExpression* where_clause) {
    if (!plan || !where_clause) return plan;

    auto eval_fn = [](auto& self, const binder::BoundExpression* expr, const Tuple& tuple) -> std::string {
        if (!expr) return "";
        switch (expr->GetExprType()) {
            case binder::BoundExpressionType::COLUMN_REF: {
                const auto* col_ref = static_cast<const binder::BoundColumnRefExpression*>(expr);
                size_t idx = col_ref->GetColIdx();
                if (idx < tuple.size()) {
                    return tuple[idx];
                }
                return "";
            }
            case binder::BoundExpressionType::LITERAL: {
                const auto* lit = static_cast<const binder::BoundLiteralExpression*>(expr);
                return lit->GetValue();
            }
            case binder::BoundExpressionType::BINARY_OP: {
                const auto* bin = static_cast<const binder::BoundBinaryOpExpression*>(expr);
                std::string l_val = self(self, bin->GetLeft(), tuple);
                std::string r_val = self(self, bin->GetRight(), tuple);
                const std::string& op = bin->GetOpType();

                if (op == "AND") {
                    bool l_b = (l_val != "0" && !l_val.empty());
                    bool r_b = (r_val != "0" && !r_val.empty());
                    return (l_b && r_b) ? "1" : "0";
                }
                if (op == "OR") {
                    bool l_b = (l_val != "0" && !l_val.empty());
                    bool r_b = (r_val != "0" && !r_val.empty());
                    return (l_b || r_b) ? "1" : "0";
                }

                try {
                    long long l_num = std::stoll(l_val);
                    long long r_num = std::stoll(r_val);
                    if (op == "=" || op == "==") return (l_num == r_num) ? "1" : "0";
                    if (op == "!=") return (l_num != r_num) ? "1" : "0";
                    if (op == "<") return (l_num < r_num) ? "1" : "0";
                    if (op == ">") return (l_num > r_num) ? "1" : "0";
                    if (op == "<=") return (l_num <= r_num) ? "1" : "0";
                    if (op == ">=") return (l_num >= r_num) ? "1" : "0";
                } catch (...) {
                    if (op == "=" || op == "==") return (l_val == r_val) ? "1" : "0";
                    if (op == "!=") return (l_val != r_val) ? "1" : "0";
                    if (op == "<") return (l_val < r_val) ? "1" : "0";
                    if (op == ">") return (l_val > r_val) ? "1" : "0";
                    if (op == "<=") return (l_val <= r_val) ? "1" : "0";
                    if (op == ">=") return (l_val >= r_val) ? "1" : "0";
                }
                return "0";
            }
            case binder::BoundExpressionType::AGGREGATE:
                return "";
        }
        return "";
    };

    execution::FilterPlanNode::PredicateFn pred = [where_clause, eval_fn](const Tuple& tuple) {
        std::string res = eval_fn(eval_fn, where_clause, tuple);
        return res != "0" && !res.empty();
    };

    return std::make_shared<execution::FilterPlanNode>(
        plan->GetOutputSchema(), plan, pred, BufferHint::DEFAULT
    );
}

std::shared_ptr<execution::AbstractPlanNode> Optimizer::OptimizeAggregation(
    std::shared_ptr<execution::AbstractPlanNode> plan,
    const binder::BoundSelectStatement& stmt) {
    bool has_agg = false;
    for (const auto& expr : stmt.select_list) {
        if (expr->GetExprType() == binder::BoundExpressionType::AGGREGATE) {
            has_agg = true;
            break;
        }
    }

    if (stmt.group_by.empty() && !has_agg) {
        return plan;
    }

    std::vector<size_t> group_by_indices;
    for (const auto& g_expr : stmt.group_by) {
        if (g_expr->GetExprType() == binder::BoundExpressionType::COLUMN_REF) {
            const auto* col_ref = static_cast<const binder::BoundColumnRefExpression*>(g_expr.get());
            group_by_indices.push_back(col_ref->GetColIdx());
        }
    }

    size_t agg_col_idx = 0;
    execution::AggregateType agg_type = execution::AggregateType::COUNT;

    for (const auto& expr : stmt.select_list) {
        if (expr->GetExprType() == binder::BoundExpressionType::AGGREGATE) {
            const auto* agg = static_cast<const binder::BoundAggregateExpression*>(expr.get());
            std::string func = agg->GetFuncName();
            if (func == "SUM") agg_type = execution::AggregateType::SUM;
            else if (func == "COUNT") agg_type = execution::AggregateType::COUNT;
            else if (func == "MAX") agg_type = execution::AggregateType::MAX;
            else if (func == "MIN") agg_type = execution::AggregateType::MIN;
            else if (func == "AVG") agg_type = execution::AggregateType::AVG;

            if (agg->GetExpr() && agg->GetExpr()->GetExprType() == binder::BoundExpressionType::COLUMN_REF) {
                const auto* child_col = static_cast<const binder::BoundColumnRefExpression*>(agg->GetExpr());
                agg_col_idx = child_col->GetColIdx();
            }
            break;
        }
    }

    return std::make_shared<execution::AggregationPlanNode>(
        stmt.select_schema, plan, group_by_indices, agg_col_idx, agg_type, BufferHint::DEFAULT
    );
}

std::shared_ptr<execution::AbstractPlanNode> Optimizer::OptimizeProjection(
    std::shared_ptr<execution::AbstractPlanNode> plan,
    const binder::BoundSelectStatement& stmt) {
    if (!plan) return nullptr;

    std::vector<size_t> select_indices;

    for (const auto& expr : stmt.select_list) {
        if (expr->GetExprType() == binder::BoundExpressionType::COLUMN_REF) {
            const auto* col_ref = static_cast<const binder::BoundColumnRefExpression*>(expr.get());
            select_indices.push_back(col_ref->GetColIdx());
        } else {
            select_indices.push_back(select_indices.size());
        }
    }

    return std::make_shared<execution::ProjectionPlanNode>(
        stmt.select_schema, plan, select_indices, BufferHint::DEFAULT
    );
}

void Optimizer::InjectBufferHints(std::shared_ptr<execution::AbstractPlanNode>& plan) {
    if (!plan) return;

    // Post-order traversal: process child nodes first
    for (const auto& child : plan->GetChildren()) {
        auto non_const_child = std::const_pointer_cast<execution::AbstractPlanNode>(child);
        InjectBufferHints(non_const_child);
    }

    // Apply rule matching based on physical plan node type
    switch (plan->GetType()) {
        case execution::PlanType::IndexScan: {
            plan->SetBufferHint(BufferHint::KEEP_HOT);
            break;
        }

        case execution::PlanType::SeqScan: {
            auto scan_node = std::dynamic_pointer_cast<execution::SeqScanPlanNode>(plan);
            if (scan_node) {
                if (plan->GetBufferHint() != BufferHint::DISCARD_QUICKLY) {
                    if (IsCatalogTable(scan_node->GetTableName())) {
                        plan->SetBufferHint(BufferHint::KEEP_HOT);
                    } else {
                        plan->SetBufferHint(BufferHint::DEFAULT);
                    }
                }
            }
            break;
        }

        case execution::PlanType::HashJoin: {
            auto join_node = std::dynamic_pointer_cast<execution::HashJoinPlanNode>(plan);
            if (join_node) {
                // Build side (left child) gets DISCARD_QUICKLY
                auto left_child = std::const_pointer_cast<execution::AbstractPlanNode>(
                    plan->GetChildren()[0]
                );
                if (left_child) {
                    SetSubtreeHint(left_child, BufferHint::DISCARD_QUICKLY);
                }
                plan->SetBufferHint(BufferHint::DEFAULT);
            }
            break;
        }

        case execution::PlanType::NestedLoopJoin: {
            if (plan->GetChildren().size() >= 2) {
                auto outer_child = std::const_pointer_cast<execution::AbstractPlanNode>(plan->GetChildren()[0]);
                auto inner_child = std::const_pointer_cast<execution::AbstractPlanNode>(plan->GetChildren()[1]);
                if (outer_child) SetSubtreeHint(outer_child, BufferHint::DISCARD_QUICKLY);
                if (inner_child) SetSubtreeHint(inner_child, BufferHint::KEEP_HOT);
            }
            plan->SetBufferHint(BufferHint::DEFAULT);
            break;
        }

        case execution::PlanType::Projection:
        case execution::PlanType::Filter:
        case execution::PlanType::Aggregation:
        default: {
            if (plan->GetBufferHint() != BufferHint::DISCARD_QUICKLY &&
                plan->GetBufferHint() != BufferHint::KEEP_HOT) {
                plan->SetBufferHint(BufferHint::DEFAULT);
            }
            break;
        }
    }
}

void Optimizer::SetSubtreeHint(std::shared_ptr<execution::AbstractPlanNode>& node, BufferHint hint) {
    if (!node) return;
    node->SetBufferHint(hint);
    for (const auto& child : node->GetChildren()) {
        auto non_const_child = std::const_pointer_cast<execution::AbstractPlanNode>(child);
        SetSubtreeHint(non_const_child, hint);
    }
}

bool Optimizer::IsCatalogTable(const std::string& table_name) const {
    return table_name.rfind("__", 0) == 0 || table_name == "catalog" || table_name == "system_tables";
}

} // namespace optimizer
} // namespace megatron
