#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.hpp"
#include "execution/plan_node.hpp"
#include "binder/bound_statement.hpp"

namespace megatron {
namespace optimizer {

class Optimizer {
public:
    explicit Optimizer(const Catalog& catalog) : catalog_(catalog) {}

    /**
     * @brief Translates a BoundSelectStatement into a physical plan tree with BufferHints.
     */
    std::shared_ptr<execution::AbstractPlanNode> Optimize(const binder::BoundSelectStatement& stmt);

    /**
     * @brief Injects plan-aware BufferHints into physical plan tree.
     */
    void InjectBufferHints(std::shared_ptr<execution::AbstractPlanNode>& plan);

private:
    // Optimization Phases
    std::shared_ptr<execution::AbstractPlanNode> OptimizeTableRef(const binder::BoundTableRef& table_ref);
    
    std::shared_ptr<execution::AbstractPlanNode> OptimizeIndexScan(
        std::shared_ptr<execution::AbstractPlanNode> plan,
        const binder::BoundExpression* where_clause);

    std::shared_ptr<execution::AbstractPlanNode> OptimizeWhere(
        std::shared_ptr<execution::AbstractPlanNode> plan,
        const binder::BoundExpression* where_clause);

    std::shared_ptr<execution::AbstractPlanNode> OptimizeAggregation(
        std::shared_ptr<execution::AbstractPlanNode> plan,
        const binder::BoundSelectStatement& stmt);

    std::shared_ptr<execution::AbstractPlanNode> OptimizeProjection(
        std::shared_ptr<execution::AbstractPlanNode> plan,
        const binder::BoundSelectStatement& stmt);

    void SetSubtreeHint(std::shared_ptr<execution::AbstractPlanNode>& node, BufferHint hint);
    bool IsCatalogTable(const std::string& table_name) const;

    const Catalog& catalog_;
};

} // namespace optimizer

using optimizer::Optimizer;

} // namespace megatron
