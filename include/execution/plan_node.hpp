#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

#include "catalog/catalog.hpp"
#include "storage/record/schema.hpp"
#include "storage/engine/storage_engine.hpp"

namespace megatron {
namespace execution {

/**
 * @brief physical plan node type enumeration
 */
enum class PlanType {
    SeqScan,
    IndexScan,
    HashJoin,
    NestedLoopJoin,
    Aggregation,
    Filter,
    Projection
};

/**
 * @brief aggregation type enumeration
 */
enum class AggregateType {
    SUM,
    COUNT,
    MAX,
    MIN,
    AVG
};

/**
 * @brief record identifier structure for page/slot location
 */
struct RID {
    uint32_t page_id{0};
    uint16_t slot_id{0};

    RID() = default;
    RID(uint32_t page, uint16_t slot) : page_id(page), slot_id(slot) {}

    /**
     * @brief packs page_id and slot_id into 64-bit integer
     */
    int64_t Get() const {
        return (static_cast<int64_t>(page_id) << 16) | slot_id;
    }

    bool operator==(const RID& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }

    bool operator!=(const RID& other) const {
        return !(*this == other);
    }
};

/**
 * @brief base abstract class for physical execution plan nodes
 */
class AbstractPlanNode {
public:
    AbstractPlanNode(Schema output_schema, std::vector<std::shared_ptr<const AbstractPlanNode>> children)
        : output_schema_(std::move(output_schema)), children_(std::move(children)) {}

    virtual ~AbstractPlanNode() = default;

    /**
     * @brief gets the type of the physical plan node
     */
    virtual PlanType GetType() const = 0;

    /**
     * @brief gets the output schema of this plan node
     */
    const Schema& GetOutputSchema() const {
        return output_schema_;
    }

    /**
     * @brief gets child plan nodes
     */
    const std::vector<std::shared_ptr<const AbstractPlanNode>>& GetChildren() const {
        return children_;
    }

    /**
     * @brief gets child plan node at index
     */
    const AbstractPlanNode* GetChildAt(size_t index) const {
        if (index < children_.size()) {
            return children_[index].get();
        }
        return nullptr;
    }

protected:
    Schema output_schema_;
    std::vector<std::shared_ptr<const AbstractPlanNode>> children_;
};

/**
 * @brief physical sequential scan plan node
 */
class SeqScanPlanNode : public AbstractPlanNode {
public:
    SeqScanPlanNode(Schema output_schema, std::string table_name, const TableMetadata* table_meta = nullptr)
        : AbstractPlanNode(std::move(output_schema), {}),
          table_name_(std::move(table_name)),
          table_meta_(table_meta) {}

    PlanType GetType() const override {
        return PlanType::SeqScan;
    }

    const std::string& GetTableName() const {
        return table_name_;
    }

    const TableMetadata* GetTableMeta() const {
        return table_meta_;
    }

private:
    std::string table_name_;
    const TableMetadata* table_meta_{nullptr};
};

/**
 * @brief physical hash join plan node
 */
class HashJoinPlanNode : public AbstractPlanNode {
public:
    HashJoinPlanNode(Schema output_schema,
                     std::shared_ptr<const AbstractPlanNode> left_child,
                     std::shared_ptr<const AbstractPlanNode> right_child,
                     size_t left_key_idx,
                     size_t right_key_idx)
        : AbstractPlanNode(std::move(output_schema), {std::move(left_child), std::move(right_child)}),
          left_key_idx_(left_key_idx),
          right_key_idx_(right_key_idx) {}

    PlanType GetType() const override {
        return PlanType::HashJoin;
    }

    const AbstractPlanNode* GetLeftChild() const {
        return GetChildAt(0);
    }

    const AbstractPlanNode* GetRightChild() const {
        return GetChildAt(1);
    }

    size_t GetLeftKeyIdx() const {
        return left_key_idx_;
    }

    size_t GetRightKeyIdx() const {
        return right_key_idx_;
    }

private:
    size_t left_key_idx_;
    size_t right_key_idx_;
};

/**
 * @brief physical aggregation plan node
 */
class AggregationPlanNode : public AbstractPlanNode {
public:
    AggregationPlanNode(Schema output_schema,
                        std::shared_ptr<const AbstractPlanNode> child,
                        std::vector<size_t> group_by_indices,
                        size_t agg_col_idx,
                        AggregateType agg_type)
        : AbstractPlanNode(std::move(output_schema), {std::move(child)}),
          group_by_indices_(std::move(group_by_indices)),
          agg_col_idx_(agg_col_idx),
          agg_type_(agg_type) {}

    PlanType GetType() const override {
        return PlanType::Aggregation;
    }

    const AbstractPlanNode* GetChild() const {
        return GetChildAt(0);
    }

    const std::vector<size_t>& GetGroupByIndices() const {
        return group_by_indices_;
    }

    size_t GetAggColIdx() const {
        return agg_col_idx_;
    }

    AggregateType GetAggType() const {
        return agg_type_;
    }

private:
    std::vector<size_t> group_by_indices_;
    size_t agg_col_idx_;
    AggregateType agg_type_;
};

/**
 * @brief physical filter plan node
 */
class FilterPlanNode : public AbstractPlanNode {
public:
    using PredicateFn = std::function<bool(const Tuple&)>;

    FilterPlanNode(Schema output_schema,
                   std::shared_ptr<const AbstractPlanNode> child,
                   PredicateFn predicate)
        : AbstractPlanNode(std::move(output_schema), {std::move(child)}),
          predicate_(std::move(predicate)) {}

    PlanType GetType() const override {
        return PlanType::Filter;
    }

    const AbstractPlanNode* GetChild() const {
        return GetChildAt(0);
    }

    bool Evaluate(const Tuple& tuple) const {
        if (predicate_) {
            return predicate_(tuple);
        }
        return true;
    }

    const PredicateFn& GetPredicate() const {
        return predicate_;
    }

private:
    PredicateFn predicate_;
};

/**
 * @brief physical projection plan node
 */
class ProjectionPlanNode : public AbstractPlanNode {
public:
    ProjectionPlanNode(Schema output_schema,
                       std::shared_ptr<const AbstractPlanNode> child,
                       std::vector<size_t> select_indices)
        : AbstractPlanNode(std::move(output_schema), {std::move(child)}),
          select_indices_(std::move(select_indices)) {}

    PlanType GetType() const override {
        return PlanType::Projection;
    }

    const AbstractPlanNode* GetChild() const {
        return GetChildAt(0);
    }

    const std::vector<size_t>& GetSelectIndices() const {
        return select_indices_;
    }

private:
    std::vector<size_t> select_indices_;
};

} // namespace execution
} // namespace megatron
