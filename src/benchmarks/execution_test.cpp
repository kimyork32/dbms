#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cassert>

#include "catalog/catalog.hpp"
#include "storage/engine/disk_storage_engine.hpp"
#include "execution/plan_node.hpp"
#include "execution/executor.hpp"

using namespace megatron;
using namespace megatron::execution;

// helper to verify condition and print message
static void AssertTrue(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "[FAIL] " << msg << std::endl;
        std::exit(1);
    }
}

// test sequential scan executor pull style
static void TestSeqScan() {
    std::cout << "running TestSeqScan..." << std::endl;

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("name", TypeId::VARCHAR);

    auto plan = std::make_shared<SeqScanPlanNode>(schema, "users");
    SeqScanExecutor scan_exec(plan.get());

    std::vector<Tuple> tuples = {
        {"1", "Alice"},
        {"2", "Bob"},
        {"3", "Charlie"}
    };
    scan_exec.SetTuples(tuples);
    scan_exec.Init();

    Tuple t;
    RID r;
    std::vector<Tuple> result;
    while (scan_exec.Next(&t, &r)) {
        result.push_back(t);
    }

    AssertTrue(result.size() == 3, "seq scan tuple count mismatch");
    AssertTrue(result[0][1] == "Alice", "seq scan row 0 mismatch");
    AssertTrue(result[1][1] == "Bob", "seq scan row 1 mismatch");
    AssertTrue(result[2][1] == "Charlie", "seq scan row 2 mismatch");

    std::cout << "[PASSED] TestSeqScan" << std::endl;
}

// test full volcano pipeline: SeqScan -> HashJoin -> Filter -> Aggregation -> Projection
static void TestFullVolcanoPipeline() {
    std::cout << "running TestFullVolcanoPipeline..." << std::endl;

    // schema setup: users (id, dept_id, name)
    Schema user_schema;
    user_schema.AddColumn("id", TypeId::INTEGER);
    user_schema.AddColumn("dept_id", TypeId::INTEGER);
    user_schema.AddColumn("name", TypeId::VARCHAR);

    // schema setup: departments (dept_id, dept_name, salary)
    Schema dept_schema;
    dept_schema.AddColumn("dept_id", TypeId::INTEGER);
    dept_schema.AddColumn("dept_name", TypeId::VARCHAR);
    dept_schema.AddColumn("salary", TypeId::INTEGER);

    // left scan: users
    auto left_plan = std::make_shared<SeqScanPlanNode>(user_schema, "users");
    auto left_exec = std::make_unique<SeqScanExecutor>(left_plan.get());
    left_exec->SetTuples({
        {"1", "10", "Alice"},
        {"2", "10", "Bob"},
        {"3", "20", "Charlie"},
        {"4", "20", "David"},
        {"5", "30", "Eve"}
    });

    // right scan: departments
    auto right_plan = std::make_shared<SeqScanPlanNode>(dept_schema, "departments");
    auto right_exec = std::make_unique<SeqScanExecutor>(right_plan.get());
    right_exec->SetTuples({
        {"10", "Engineering", "100"},
        {"10", "Engineering", "200"},
        {"20", "Sales", "150"},
        {"20", "Sales", "250"},
        {"30", "Marketing", "80"}
    });

    // 1. HashJoin: join on users.dept_id (idx 1) == departments.dept_id (idx 0)
    // output schema: id, dept_id, name, dept_id, dept_name, salary
    Schema joined_schema;
    joined_schema.AddColumn("id", TypeId::INTEGER);
    joined_schema.AddColumn("dept_id", TypeId::INTEGER);
    joined_schema.AddColumn("name", TypeId::VARCHAR);
    joined_schema.AddColumn("dept_id", TypeId::INTEGER);
    joined_schema.AddColumn("dept_name", TypeId::VARCHAR);
    joined_schema.AddColumn("salary", TypeId::INTEGER);

    auto join_plan = std::make_shared<HashJoinPlanNode>(joined_schema, left_plan, right_plan, 1, 0);
    auto join_exec = std::make_unique<HashJoinExecutor>(join_plan.get(), std::move(left_exec), std::move(right_exec));

    // 2. Filter: salary > 90 (salary is index 5 in joined tuple)
    auto filter_plan = std::make_shared<FilterPlanNode>(joined_schema, join_plan, [](const Tuple& tuple) -> bool {
        if (tuple.size() < 6) return false;
        try {
            return std::stod(tuple[5]) > 90.0;
        } catch (...) {
            return false;
        }
    });
    auto filter_exec = std::make_unique<FilterExecutor>(filter_plan.get(), std::move(join_exec));

    // 3. Aggregation: group by dept_name (index 4 in joined tuple), SUM salary (index 5)
    // output schema: dept_name, sum_salary
    Schema agg_schema;
    agg_schema.AddColumn("dept_name", TypeId::VARCHAR);
    agg_schema.AddColumn("sum_salary", TypeId::VARCHAR);

    auto agg_plan = std::make_shared<AggregationPlanNode>(agg_schema, filter_plan, std::vector<size_t>{4}, 5, AggregateType::SUM);
    auto agg_exec = std::make_unique<AggregationExecutor>(agg_plan.get(), std::move(filter_exec));

    // 4. Projection: select dept_name (idx 0), sum_salary (idx 1)
    auto proj_plan = std::make_shared<ProjectionPlanNode>(agg_schema, agg_plan, std::vector<size_t>{0, 1});
    auto proj_exec = std::make_unique<ProjectionExecutor>(proj_plan.get(), std::move(agg_exec));

    // pull tuples from top of physical execution tree
    proj_exec->Init();
    Tuple tuple;
    RID rid;
    std::vector<Tuple> final_results;
    while (proj_exec->Next(&tuple, &rid)) {
        final_results.push_back(tuple);
    }

    // sort results by group key for deterministic verification
    std::sort(final_results.begin(), final_results.end(), [](const Tuple& a, const Tuple& b) {
        return a[0] < b[0];
    });

    AssertTrue(final_results.size() == 2, "pipeline result row count mismatch");
    // Engineering has (100+200)*2 = 600 sum salary for 2 matching users
    AssertTrue(final_results[0][0] == "Engineering", "row 0 department mismatch");
    AssertTrue(final_results[0][1] == "600", "row 0 sum salary mismatch");

    // Sales has (150+250)*2 = 800 sum salary for 2 matching users
    AssertTrue(final_results[1][0] == "Sales", "row 1 department mismatch");
    AssertTrue(final_results[1][1] == "800", "row 1 sum salary mismatch");

    std::cout << "[PASSED] TestFullVolcanoPipeline" << std::endl;
}

// test state reset on re-init
static void TestStateResetOnReInit() {
    std::cout << "running TestStateResetOnReInit..." << std::endl;

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);

    auto plan = std::make_shared<SeqScanPlanNode>(schema, "nums");
    SeqScanExecutor scan_exec(plan.get());
    scan_exec.SetTuples({{"1"}, {"2"}, {"3"}});

    scan_exec.Init();
    Tuple t;
    int count = 0;
    while (scan_exec.Next(&t)) {
        count++;
    }
    AssertTrue(count == 3, "first scan count mismatch");

    // re-initialize and scan again
    scan_exec.Init();
    count = 0;
    while (scan_exec.Next(&t)) {
        count++;
    }
    AssertTrue(count == 3, "second scan count mismatch after re-init");

    std::cout << "[PASSED] TestStateResetOnReInit" << std::endl;
}

// test global aggregation on empty input stream
static void TestGlobalAggEmptyStream() {
    std::cout << "running TestGlobalAggEmptyStream..." << std::endl;

    Schema schema;
    schema.AddColumn("count", TypeId::VARCHAR);

    auto scan_plan = std::make_shared<SeqScanPlanNode>(schema, "empty_table");
    auto scan_exec = std::make_unique<SeqScanExecutor>(scan_plan.get());
    scan_exec->SetTuples({});

    auto agg_plan = std::make_shared<AggregationPlanNode>(schema, scan_plan, std::vector<size_t>{}, 0, AggregateType::COUNT);
    AggregationExecutor agg_exec(agg_plan.get(), std::move(scan_exec));

    agg_exec.Init();
    Tuple t;
    std::vector<Tuple> results;
    while (agg_exec.Next(&t)) {
        results.push_back(t);
    }
    AssertTrue(results.size() == 1, "global agg empty stream should return 1 row");
    AssertTrue(results[0][0] == "0", "global agg empty stream result should be 0");

    std::cout << "[PASSED] TestGlobalAggEmptyStream" << std::endl;
}

// test sequential scan cursor >= 65536 dynamic RID computation
static void TestSeqScanLargeCursorRID() {
    std::cout << "running TestSeqScanLargeCursorRID..." << std::endl;

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);

    auto scan_plan = std::make_shared<SeqScanPlanNode>(schema, "large_table");
    SeqScanExecutor scan_exec(scan_plan.get());

    std::vector<Tuple> tuples(70000, {"1"});
    scan_exec.SetTuples(tuples);
    scan_exec.Init();

    Tuple t;
    RID r;
    size_t count = 0;
    RID rid_65536;
    while (scan_exec.Next(&t, &r)) {
        if (count == 65536) {
            rid_65536 = r;
        }
        count++;
    }
    AssertTrue(count == 70000, "large scan count mismatch");
    AssertTrue(rid_65536.page_id == 1, "rid page_id for cursor 65536 mismatch");
    AssertTrue(rid_65536.slot_id == 0, "rid slot_id for cursor 65536 mismatch");

    std::cout << "[PASSED] TestSeqScanLargeCursorRID" << std::endl;
}

// test IndexScanPlanNode and IndexScanExecutor functionality
static void TestIndexScanPlanAndExecutor() {
    std::cout << "running TestIndexScanPlanAndExecutor..." << std::endl;

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("name", TypeId::VARCHAR);

    auto idx_plan = std::make_shared<IndexScanPlanNode>(schema, "users", "idx_users_id", "42", nullptr, BufferHint::KEEP_HOT);
    AssertTrue(idx_plan->GetType() == PlanType::IndexScan, "IndexScanPlanNode type mismatch");
    AssertTrue(idx_plan->GetTableName() == "users", "table_name mismatch");
    AssertTrue(idx_plan->GetIndexName() == "idx_users_id", "index_name mismatch");
    AssertTrue(idx_plan->GetSearchKey() == "42", "search_key mismatch");
    AssertTrue(idx_plan->GetBufferHint() == BufferHint::KEEP_HOT, "IndexScanPlanNode BufferHint mismatch");

    IndexScanExecutor idx_exec(idx_plan.get());
    AssertTrue(idx_exec.GetBufferHint() == BufferHint::KEEP_HOT, "IndexScanExecutor GetBufferHint mismatch");
    AssertTrue(idx_exec.GetTableName() == "users", "IndexScanExecutor table_name mismatch");
    AssertTrue(idx_exec.GetIndexName() == "idx_users_id", "IndexScanExecutor index_name mismatch");
    AssertTrue(idx_exec.GetSearchKey() == "42", "IndexScanExecutor search_key mismatch");

    idx_exec.SetTuples({{"42", "Douglas"}}, {RID(5, 12)});
    idx_exec.Init();

    Tuple t;
    RID r;
    AssertTrue(idx_exec.Next(&t, &r), "IndexScanExecutor Next returned false");
    AssertTrue(t[0] == "42" && t[1] == "Douglas", "IndexScanExecutor tuple mismatch");
    AssertTrue(r.page_id == 5 && r.slot_id == 12, "IndexScanExecutor RID mismatch");
    AssertTrue(!idx_exec.Next(&t, &r), "IndexScanExecutor returned extra tuple");

    std::cout << "[PASSED] TestIndexScanPlanAndExecutor" << std::endl;
}

// test BufferHint propagation through executors
static void TestBufferHintPropagationInExecutors() {
    std::cout << "running TestBufferHintPropagationInExecutors..." << std::endl;

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);

    auto left_plan = std::make_shared<SeqScanPlanNode>(schema, "t1", nullptr, BufferHint::KEEP_HOT);
    auto right_plan = std::make_shared<IndexScanPlanNode>(schema, "t2", "idx_t2", "10", nullptr, BufferHint::DISCARD_QUICKLY);

    auto left_exec = std::make_unique<SeqScanExecutor>(left_plan.get());
    auto right_exec = std::make_unique<IndexScanExecutor>(right_plan.get());

    AssertTrue(left_exec->GetBufferHint() == BufferHint::KEEP_HOT, "SeqScanExecutor hint propagation failed");
    AssertTrue(right_exec->GetBufferHint() == BufferHint::DISCARD_QUICKLY, "IndexScanExecutor hint propagation failed");

    auto join_plan = std::make_shared<HashJoinPlanNode>(schema, left_plan, right_plan, 0, 0, BufferHint::DEFAULT);
    HashJoinExecutor join_exec(join_plan.get(), std::move(left_exec), std::move(right_exec));

    AssertTrue(join_exec.GetLeftChild()->GetBufferHint() == BufferHint::KEEP_HOT, "HashJoin left child hint failed");
    AssertTrue(join_exec.GetRightChild()->GetBufferHint() == BufferHint::DISCARD_QUICKLY, "HashJoin right child hint failed");

    std::cout << "[PASSED] TestBufferHintPropagationInExecutors" << std::endl;
}

int main() {
    std::cout << "=== starting direct physical plan volcano unit tests ===" << std::endl;
    TestSeqScan();
    TestFullVolcanoPipeline();
    TestStateResetOnReInit();
    TestGlobalAggEmptyStream();
    TestSeqScanLargeCursorRID();
    TestIndexScanPlanAndExecutor();
    TestBufferHintPropagationInExecutors();
    std::cout << "=== all physical plan unit tests PASSED successfully ===" << std::endl;
    return 0;
}
