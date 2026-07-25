#include "execution/executor.hpp"
#include <stdexcept>
#include <cmath>

namespace megatron {
namespace execution {

// ================= SeqScanExecutor =================

SeqScanExecutor::SeqScanExecutor(std::string table_name, const TableMetadata* meta, std::vector<Tuple> tuples)
    : AbstractExecutor(nullptr), table_name_(std::move(table_name)), tuples_(std::move(tuples)) {
    if (meta) {
        schema_ = meta->schema;
    }
}

SeqScanExecutor::SeqScanExecutor(const SeqScanPlanNode* plan, const TableMetadata* meta, std::vector<Tuple> tuples)
    : AbstractExecutor(plan), tuples_(std::move(tuples)) {
    if (plan) {
        table_name_ = plan->GetTableName();
        schema_ = plan->GetOutputSchema();
    } else if (meta) {
        table_name_ = meta->table_name;
        schema_ = meta->schema;
    }
}

void SeqScanExecutor::SetTuples(std::vector<Tuple> tuples, std::vector<RID> rids) {
    tuples_ = std::move(tuples);
    rids_ = std::move(rids);
}

void SeqScanExecutor::Init() {
    cursor_ = 0;
}

bool SeqScanExecutor::Next(Tuple* tuple, RID* rid) {
    if (cursor_ >= tuples_.size()) {
        return false;
    }
    if (tuple != nullptr) {
        *tuple = tuples_[cursor_];
    }
    if (rid != nullptr) {
        if (cursor_ < rids_.size()) {
            *rid = rids_[cursor_];
        } else {
            uint32_t page_id = static_cast<uint32_t>(cursor_ >> 16);
            uint16_t slot_id = static_cast<uint16_t>(cursor_ & 0xFFFF);
            *rid = RID(page_id, slot_id);
        }
    }
    cursor_++;
    return true;
}

const Schema& SeqScanExecutor::GetOutputSchema() const {
    return schema_;
}

const std::string& SeqScanExecutor::GetTableName() const {
    return table_name_;
}

// ================= HashJoinExecutor =================

HashJoinExecutor::HashJoinExecutor(std::unique_ptr<AbstractExecutor> left_child,
                                   std::unique_ptr<AbstractExecutor> right_child,
                                   size_t left_key_idx,
                                   size_t right_key_idx)
    : AbstractExecutor(nullptr),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)),
      left_key_idx_(left_key_idx),
      right_key_idx_(right_key_idx) {
    BuildOutputSchema();
}

HashJoinExecutor::HashJoinExecutor(const HashJoinPlanNode* plan,
                                   std::unique_ptr<AbstractExecutor> left_child,
                                   std::unique_ptr<AbstractExecutor> right_child)
    : AbstractExecutor(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {
    if (plan) {
        left_key_idx_ = plan->GetLeftKeyIdx();
        right_key_idx_ = plan->GetRightKeyIdx();
        output_schema_ = plan->GetOutputSchema();
    } else {
        left_key_idx_ = 0;
        right_key_idx_ = 0;
        BuildOutputSchema();
    }
}

void HashJoinExecutor::BuildOutputSchema() {
    output_schema_.columns.clear();
    if (left_child_) {
        for (const auto& col : left_child_->GetOutputSchema().columns) {
            output_schema_.columns.push_back(col);
        }
    }
    if (right_child_) {
        for (const auto& col : right_child_->GetOutputSchema().columns) {
            output_schema_.columns.push_back(col);
        }
    }
}

void HashJoinExecutor::Init() {
    if (left_child_) {
        left_child_->Init();
    }
    if (right_child_) {
        right_child_->Init();
    }
    hash_table_.clear();
    matched_results_.clear();
    match_cursor_ = 0;

    if (!left_child_ || !right_child_) {
        return;
    }

    Tuple left_tuple;
    RID left_rid;
    while (left_child_->Next(&left_tuple, &left_rid)) {
        if (left_key_idx_ < left_tuple.size()) {
            hash_table_[left_tuple[left_key_idx_]].push_back({left_tuple, left_rid});
        }
    }

    Tuple right_tuple;
    RID right_rid;
    while (right_child_->Next(&right_tuple, &right_rid)) {
        if (right_key_idx_ < right_tuple.size()) {
            const std::string& key = right_tuple[right_key_idx_];
            auto it = hash_table_.find(key);
            if (it != hash_table_.end()) {
                for (const auto& pair : it->second) {
                    Tuple joined = pair.first;
                    joined.insert(joined.end(), right_tuple.begin(), right_tuple.end());
                    matched_results_.push_back({std::move(joined), pair.second});
                }
            }
        }
    }
}

bool HashJoinExecutor::Next(Tuple* tuple, RID* rid) {
    if (match_cursor_ >= matched_results_.size()) {
        return false;
    }
    if (tuple != nullptr) {
        *tuple = matched_results_[match_cursor_].first;
    }
    if (rid != nullptr) {
        *rid = matched_results_[match_cursor_].second;
    }
    match_cursor_++;
    return true;
}

const Schema& HashJoinExecutor::GetOutputSchema() const {
    return output_schema_;
}

AbstractExecutor* HashJoinExecutor::GetLeftChild() const {
    return left_child_.get();
}

AbstractExecutor* HashJoinExecutor::GetRightChild() const {
    return right_child_.get();
}

// ================= AggregationExecutor =================

AggregationExecutor::AggregationExecutor(std::unique_ptr<AbstractExecutor> child,
                                         std::vector<size_t> group_by_indices,
                                         size_t agg_col_idx,
                                         AggregateType agg_type)
    : AbstractExecutor(nullptr),
      child_(std::move(child)),
      group_by_indices_(std::move(group_by_indices)),
      agg_col_idx_(agg_col_idx),
      agg_type_(agg_type) {
    BuildOutputSchema();
}

AggregationExecutor::AggregationExecutor(const AggregationPlanNode* plan,
                                         std::unique_ptr<AbstractExecutor> child)
    : AbstractExecutor(plan),
      child_(std::move(child)) {
    if (plan) {
        group_by_indices_ = plan->GetGroupByIndices();
        agg_col_idx_ = plan->GetAggColIdx();
        agg_type_ = plan->GetAggType();
        output_schema_ = plan->GetOutputSchema();
    } else {
        agg_col_idx_ = 0;
        agg_type_ = AggregateType::COUNT;
        BuildOutputSchema();
    }
}

void AggregationExecutor::BuildOutputSchema() {
    output_schema_.columns.clear();
    if (child_) {
        for (size_t idx : group_by_indices_) {
            if (idx < child_->GetOutputSchema().columns.size()) {
                output_schema_.columns.push_back(child_->GetOutputSchema().columns[idx]);
            }
        }
        Column agg_col;
        agg_col.name = "agg_result";
        agg_col.type = TypeId::VARCHAR;
        output_schema_.columns.push_back(agg_col);
    }
}

void AggregationExecutor::Init() {
    if (!child_) {
        return;
    }
    child_->Init();
    results_.clear();
    cursor_ = 0;

    std::unordered_map<std::string, double> agg_results;
    std::unordered_map<std::string, int> count_results;
    std::unordered_map<std::string, Tuple> group_keys;

    Tuple tuple;
    RID rid;
    while (child_->Next(&tuple, &rid)) {
        std::string group_key_str;
        Tuple group_val;
        for (size_t idx : group_by_indices_) {
            if (idx < tuple.size()) {
                group_key_str += tuple[idx] + "|";
                group_val.push_back(tuple[idx]);
            }
        }
        if (group_keys.find(group_key_str) == group_keys.end()) {
            group_keys[group_key_str] = group_val;
        }

        double val = 0.0;
        if (agg_col_idx_ < tuple.size()) {
            try {
                val = std::stod(tuple[agg_col_idx_]);
            } catch (...) {
                val = 0.0;
            }
        }

        if (agg_results.find(group_key_str) == agg_results.end()) {
            agg_results[group_key_str] = (agg_type_ == AggregateType::MIN || agg_type_ == AggregateType::MAX) ? val : 0.0;
            count_results[group_key_str] = 0;
        }

        count_results[group_key_str]++;
        if (agg_type_ == AggregateType::SUM || agg_type_ == AggregateType::AVG) {
            agg_results[group_key_str] += val;
        } else if (agg_type_ == AggregateType::COUNT) {
            agg_results[group_key_str] = count_results[group_key_str];
        } else if (agg_type_ == AggregateType::MAX) {
            if (val > agg_results[group_key_str]) {
                agg_results[group_key_str] = val;
            }
        } else if (agg_type_ == AggregateType::MIN) {
            if (val < agg_results[group_key_str]) {
                agg_results[group_key_str] = val;
            }
        }
    }

    for (const auto& pair : group_keys) {
        const std::string& gk = pair.first;
        Tuple row = pair.second;
        double res_val = agg_results[gk];
        std::string formatted_res;
        if (agg_type_ == AggregateType::COUNT) {
            formatted_res = std::to_string(count_results[gk]);
        } else if (agg_type_ == AggregateType::AVG) {
            if (count_results[gk] > 0) {
                res_val /= count_results[gk];
            }
            formatted_res = std::to_string(res_val);
        } else {
            if (std::abs(res_val - std::round(res_val)) < 1e-9) {
                formatted_res = std::to_string(static_cast<long long>(std::round(res_val)));
            } else {
                formatted_res = std::to_string(res_val);
            }
        }
        row.push_back(formatted_res);
        results_.push_back(row);
    }

    if (results_.empty() && group_by_indices_.empty()) {
        results_.push_back({"0"});
    }
}

bool AggregationExecutor::Next(Tuple* tuple, RID* rid) {
    if (cursor_ >= results_.size()) {
        return false;
    }
    if (tuple != nullptr) {
        *tuple = results_[cursor_];
    }
    if (rid != nullptr) {
        *rid = RID(0, static_cast<uint16_t>(cursor_));
    }
    cursor_++;
    return true;
}

const Schema& AggregationExecutor::GetOutputSchema() const {
    return output_schema_;
}

AbstractExecutor* AggregationExecutor::GetChild() const {
    return child_.get();
}

// ================= FilterExecutor =================

FilterExecutor::FilterExecutor(std::unique_ptr<AbstractExecutor> child, PredicateFn predicate)
    : AbstractExecutor(nullptr), child_(std::move(child)), predicate_(std::move(predicate)) {}

FilterExecutor::FilterExecutor(const FilterPlanNode* plan, std::unique_ptr<AbstractExecutor> child)
    : AbstractExecutor(plan), child_(std::move(child)) {
    if (plan) {
        predicate_ = [plan](const Tuple& tuple) -> bool {
            return plan->Evaluate(tuple);
        };
    }
}

void FilterExecutor::Init() {
    if (child_) {
        child_->Init();
    }
}

bool FilterExecutor::Next(Tuple* tuple, RID* rid) {
    if (!child_) {
        return false;
    }
    Tuple temp_tuple;
    RID temp_rid;
    while (child_->Next(&temp_tuple, &temp_rid)) {
        if (!predicate_ || predicate_(temp_tuple)) {
            if (tuple != nullptr) {
                *tuple = std::move(temp_tuple);
            }
            if (rid != nullptr) {
                *rid = temp_rid;
            }
            return true;
        }
    }
    return false;
}

const Schema& FilterExecutor::GetOutputSchema() const {
    if (child_) {
        return child_->GetOutputSchema();
    }
    static Schema empty_schema;
    return empty_schema;
}

AbstractExecutor* FilterExecutor::GetChild() const {
    return child_.get();
}

// ================= ProjectionExecutor =================

ProjectionExecutor::ProjectionExecutor(std::unique_ptr<AbstractExecutor> child,
                                       std::vector<size_t> select_indices,
                                       Schema output_schema)
    : AbstractExecutor(nullptr),
      child_(std::move(child)),
      select_indices_(std::move(select_indices)),
      output_schema_(std::move(output_schema)) {
    if (output_schema_.columns.empty()) {
        BuildOutputSchema();
    }
}

ProjectionExecutor::ProjectionExecutor(const ProjectionPlanNode* plan,
                                       std::unique_ptr<AbstractExecutor> child)
    : AbstractExecutor(plan), child_(std::move(child)) {
    if (plan) {
        select_indices_ = plan->GetSelectIndices();
        output_schema_ = plan->GetOutputSchema();
    } else {
        BuildOutputSchema();
    }
}

void ProjectionExecutor::BuildOutputSchema() {
    output_schema_.columns.clear();
    if (child_) {
        const auto& child_schema = child_->GetOutputSchema();
        for (size_t idx : select_indices_) {
            if (idx < child_schema.columns.size()) {
                output_schema_.columns.push_back(child_schema.columns[idx]);
            }
        }
    }
}

void ProjectionExecutor::Init() {
    if (child_) {
        child_->Init();
    }
}

bool ProjectionExecutor::Next(Tuple* tuple, RID* rid) {
    if (!child_) {
        return false;
    }
    Tuple child_tuple;
    if (!child_->Next(&child_tuple, rid)) {
        return false;
    }
    if (tuple != nullptr) {
        tuple->clear();
        tuple->reserve(select_indices_.size());
        for (size_t idx : select_indices_) {
            if (idx < child_tuple.size()) {
                tuple->push_back(child_tuple[idx]);
            } else {
                tuple->push_back("");
            }
        }
    }
    return true;
}

const Schema& ProjectionExecutor::GetOutputSchema() const {
    return output_schema_;
}

AbstractExecutor* ProjectionExecutor::GetChild() const {
    return child_.get();
}

} // namespace execution
} // namespace megatron
