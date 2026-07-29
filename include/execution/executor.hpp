#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cstdint>

#include "catalog/catalog.hpp"
#include "storage/engine/storage_engine.hpp"
#include "execution/plan_node.hpp"

namespace megatron {
namespace execution {

/**
 * @brief base interface for volcano model executors
 */
class AbstractExecutor {
public:
    explicit AbstractExecutor(const AbstractPlanNode* plan_node = nullptr) : plan_node_(plan_node) {}
    virtual ~AbstractExecutor() = default;

    /**
     * @brief initializes state before pulling tuples
     */
    virtual void Init() = 0;

    /**
     * @brief pulls next tuple from stream according to volcano iterator model
     * @param tuple pointer to output tuple
     * @param rid optional pointer to output record ID
     * @return true if tuple retrieved, false if stream exhausted
     */
    virtual bool Next(Tuple* tuple, RID* rid = nullptr) = 0;

    /**
     * @brief gets output schema of executor stream
     */
    virtual const Schema& GetOutputSchema() const = 0;

    /**
     * @brief gets physical plan node corresponding to executor
     */
    const AbstractPlanNode* GetPlanNode() const {
        return plan_node_;
    }

    /**
     * @brief gets buffer pool retention hint from physical plan node
     */
    BufferHint GetBufferHint() const {
        return plan_node_ != nullptr ? plan_node_->GetBufferHint() : BufferHint::DEFAULT;
    }

protected:
    const AbstractPlanNode* plan_node_{nullptr};
};

/**
 * @brief sequential scan executor
 */
class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(std::string table_name, const TableMetadata* meta = nullptr, std::vector<Tuple> tuples = {}, StorageEngineInterface* storage = nullptr, GlobalBufferPoolManager* bpm = nullptr);
    explicit SeqScanExecutor(const SeqScanPlanNode* plan, const TableMetadata* meta = nullptr, std::vector<Tuple> tuples = {}, StorageEngineInterface* storage = nullptr, GlobalBufferPoolManager* bpm = nullptr);

    void SetTuples(std::vector<Tuple> tuples, std::vector<RID> rids = {});
    void SetStorageEngine(StorageEngineInterface* storage) { storage_ = storage; }
    void SetBufferPoolManager(GlobalBufferPoolManager* bpm) { bpm_ = bpm; }
    void Init() override;
    bool Next(Tuple* tuple, RID* rid = nullptr) override;
    const Schema& GetOutputSchema() const override;
    const std::string& GetTableName() const;

private:
    std::string table_name_;
    const TableMetadata* meta_{nullptr};
    StorageEngineInterface* storage_{nullptr};
    GlobalBufferPoolManager* bpm_{nullptr};
    std::vector<Tuple> tuples_;
    std::vector<RID> rids_;
    Schema schema_;
    size_t cursor_ = 0;
};

/**
 * @brief index scan executor
 */
class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(std::string table_name,
                      std::string index_name,
                      std::string search_key,
                      const TableMetadata* meta = nullptr,
                      GlobalBufferPoolManager* bpm = nullptr);
    explicit IndexScanExecutor(const IndexScanPlanNode* plan,
                               const TableMetadata* meta = nullptr,
                               GlobalBufferPoolManager* bpm = nullptr);

    void SetTuples(std::vector<Tuple> tuples, std::vector<RID> rids = {});
    void SetRIDs(std::vector<RID> rids);
    void SetBufferPoolManager(GlobalBufferPoolManager* bpm) { bpm_ = bpm; }

    void Init() override;
    bool Next(Tuple* tuple, RID* rid = nullptr) override;
    const Schema& GetOutputSchema() const override;

    const std::string& GetTableName() const;
    const std::string& GetIndexName() const;
    const std::string& GetSearchKey() const;

private:
    std::string table_name_;
    std::string index_name_;
    std::string search_key_;
    const TableMetadata* meta_{nullptr};
    GlobalBufferPoolManager* bpm_{nullptr};
    Schema schema_;
    std::vector<RID> matching_rids_;
    size_t cursor_{0};
    std::vector<Tuple> tuples_;
    std::vector<RID> rids_;
};

/**
 * @brief hash join executor
 */
class HashJoinExecutor : public AbstractExecutor {
public:
    HashJoinExecutor(std::unique_ptr<AbstractExecutor> left_child,
                     std::unique_ptr<AbstractExecutor> right_child,
                     size_t left_key_idx,
                     size_t right_key_idx);
    HashJoinExecutor(const HashJoinPlanNode* plan,
                     std::unique_ptr<AbstractExecutor> left_child,
                     std::unique_ptr<AbstractExecutor> right_child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid = nullptr) override;
    const Schema& GetOutputSchema() const override;
    AbstractExecutor* GetLeftChild() const;
    AbstractExecutor* GetRightChild() const;

private:
    void BuildOutputSchema();

    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    size_t left_key_idx_{0};
    size_t right_key_idx_{0};
    std::unordered_map<std::string, std::vector<std::pair<Tuple, RID>>> hash_table_;
    std::vector<std::pair<Tuple, RID>> matched_results_;
    size_t match_cursor_ = 0;
    Schema output_schema_;
};

/**
 * @brief aggregation executor
 */
class AggregationExecutor : public AbstractExecutor {
public:
    AggregationExecutor(std::unique_ptr<AbstractExecutor> child,
                        std::vector<size_t> group_by_indices,
                        size_t agg_col_idx,
                        AggregateType agg_type);
    AggregationExecutor(const AggregationPlanNode* plan,
                        std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid = nullptr) override;
    const Schema& GetOutputSchema() const override;
    AbstractExecutor* GetChild() const;

private:
    void BuildOutputSchema();

    std::unique_ptr<AbstractExecutor> child_;
    std::vector<size_t> group_by_indices_;
    size_t agg_col_idx_{0};
    AggregateType agg_type_{AggregateType::COUNT};
    std::vector<Tuple> results_;
    size_t cursor_ = 0;
    Schema output_schema_;
};

/**
 * @brief filter executor
 */
class FilterExecutor : public AbstractExecutor {
public:
    using PredicateFn = std::function<bool(const Tuple&)>;

    FilterExecutor(std::unique_ptr<AbstractExecutor> child, PredicateFn predicate);
    FilterExecutor(const FilterPlanNode* plan, std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid = nullptr) override;
    const Schema& GetOutputSchema() const override;
    AbstractExecutor* GetChild() const;

private:
    std::unique_ptr<AbstractExecutor> child_;
    PredicateFn predicate_;
};

/**
 * @brief projection executor
 */
class ProjectionExecutor : public AbstractExecutor {
public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> child,
                       std::vector<size_t> select_indices,
                       Schema output_schema = {});
    ProjectionExecutor(const ProjectionPlanNode* plan,
                       std::unique_ptr<AbstractExecutor> child);

    void Init() override;
    bool Next(Tuple* tuple, RID* rid = nullptr) override;
    const Schema& GetOutputSchema() const override;
    AbstractExecutor* GetChild() const;

private:
    void BuildOutputSchema();

    std::unique_ptr<AbstractExecutor> child_;
    std::vector<size_t> select_indices_;
    Schema output_schema_;
};

} // namespace execution
} // namespace megatron
