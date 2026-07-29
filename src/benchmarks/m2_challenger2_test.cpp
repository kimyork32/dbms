#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <cstdio>
#include <unistd.h>

#include "catalog/catalog.hpp"
#include "storage/engine/disk_storage_engine.hpp"
#include "storage/index/b_plus_tree.hpp"
#include "storage/page/buffer_pool_manager.hpp"
#include "storage/record/tuple_builder.hpp"
#include "execution/plan_node.hpp"
#include "execution/executor.hpp"

using namespace megatron;
using namespace megatron::execution;

static void AssertTrue(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "[FAIL] " << msg << std::endl;
        std::exit(1);
    }
}

// 1. Verify IndexScanPlanNode creation, getters, schema, and hint assignment
static void TestIndexScanPlanNode() {
    std::cout << "running TestIndexScanPlanNode..." << std::endl;

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("email", TypeId::VARCHAR);

    TableMetadata meta;
    meta.table_name = "customers";
    meta.schema = schema;

    // Test default hint constructor
    auto plan1 = std::make_shared<IndexScanPlanNode>(schema, "customers", "idx_cust_id", "101", &meta);
    AssertTrue(plan1->GetType() == PlanType::IndexScan, "PlanType should be IndexScan");
    AssertTrue(plan1->GetTableName() == "customers", "GetTableName mismatch");
    AssertTrue(plan1->GetIndexName() == "idx_cust_id", "GetIndexName mismatch");
    AssertTrue(plan1->GetSearchKey() == "101", "GetSearchKey mismatch");
    AssertTrue(plan1->GetTableMeta() == &meta, "GetTableMeta mismatch");
    AssertTrue(plan1->GetOutputSchema().columns.size() == 2, "Output schema column count mismatch");
    AssertTrue(plan1->GetChildren().empty(), "IndexScanPlanNode children should be empty");
    AssertTrue(plan1->GetChildAt(0) == nullptr, "GetChildAt(0) should be nullptr");
    AssertTrue(plan1->GetBufferHint() == BufferHint::DEFAULT, "Default BufferHint should be DEFAULT");

    // Test custom hints (KEEP_HOT, DISCARD_QUICKLY)
    auto plan2 = std::make_shared<IndexScanPlanNode>(schema, "customers", "idx_cust_id", "202", &meta, BufferHint::KEEP_HOT);
    AssertTrue(plan2->GetBufferHint() == BufferHint::KEEP_HOT, "BufferHint should be KEEP_HOT");

    auto plan3 = std::make_shared<IndexScanPlanNode>(schema, "customers", "idx_cust_id", "303", &meta, BufferHint::DISCARD_QUICKLY);
    AssertTrue(plan3->GetBufferHint() == BufferHint::DISCARD_QUICKLY, "BufferHint should be DISCARD_QUICKLY");

    // Test SetBufferHint mutation
    plan3->SetBufferHint(BufferHint::KEEP_HOT);
    AssertTrue(plan3->GetBufferHint() == BufferHint::KEEP_HOT, "SetBufferHint failed to update hint");

    std::cout << "[PASSED] TestIndexScanPlanNode" << std::endl;
}

// 2. Verify IndexScanExecutor initialization, hint extraction, RID iteration, and state reset
static void TestIndexScanExecutorBasics() {
    std::cout << "running TestIndexScanExecutorBasics..." << std::endl;

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("val", TypeId::VARCHAR);

    auto plan = std::make_shared<IndexScanPlanNode>(schema, "items", "idx_items_id", "42", nullptr, BufferHint::DISCARD_QUICKLY);
    IndexScanExecutor exec(plan.get());

    // Hint propagation check
    AssertTrue(exec.GetBufferHint() == BufferHint::DISCARD_QUICKLY, "Executor failed to extract hint from plan");
    AssertTrue(exec.GetTableName() == "items", "Executor table_name mismatch");
    AssertTrue(exec.GetIndexName() == "idx_items_id", "Executor index_name mismatch");
    AssertTrue(exec.GetSearchKey() == "42", "Executor search_key mismatch");

    // Manual RID and tuple setup
    std::vector<RID> rids = {RID(1, 10), RID(1, 11), RID(2, 5)};
    std::vector<Tuple> tuples = {{"42", "alpha"}, {"42", "beta"}, {"42", "gamma"}};

    exec.SetRIDs(rids);
    exec.SetTuples(tuples);
    exec.Init();

    Tuple t;
    RID r;
    
    AssertTrue(exec.Next(&t, &r), "First Next should return true");
    AssertTrue(r == RID(1, 10), "First RID mismatch");
    AssertTrue(t[1] == "alpha", "First tuple mismatch");

    AssertTrue(exec.Next(&t, &r), "Second Next should return true");
    AssertTrue(r == RID(1, 11), "Second RID mismatch");
    AssertTrue(t[1] == "beta", "Second tuple mismatch");

    AssertTrue(exec.Next(&t, &r), "Third Next should return true");
    AssertTrue(r == RID(2, 5), "Third RID mismatch");
    AssertTrue(t[1] == "gamma", "Third tuple mismatch");

    AssertTrue(!exec.Next(&t, &r), "Fourth Next should return false (exhausted)");

    // Test state reset on re-Init
    exec.Init();
    AssertTrue(exec.Next(&t, &r), "Next after re-Init should return true");
    AssertTrue(r == RID(1, 10), "RID after re-Init mismatch");

    // Test null pointers passed to Next
    AssertTrue(exec.Next(nullptr, nullptr), "Next with null pointers should succeed");

    std::cout << "[PASSED] TestIndexScanExecutorBasics" << std::endl;
}

// 3. Verify IndexScanExecutor B+ tree lookup integration
static void TestIndexScanBPlusTreeIntegration() {
    std::cout << "running TestIndexScanBPlusTreeIntegration..." << std::endl;

    const char* idx_file = "test_m2_bplus.db";
    unlink(idx_file); // Remove if left over

    // Create B+ tree index file and insert key 777 pointing to packed RID (page 12, slot 34)
    {
        BPlusTreeDisk tree(idx_file);
        int64_t packed_rid = (static_cast<int64_t>(12) << 16) | 34;
        tree.Insert(777, packed_rid);
        tree.Insert(888, (static_cast<int64_t>(5) << 16) | 99);
    }

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);

    auto plan = std::make_shared<IndexScanPlanNode>(schema, "test_table", idx_file, "777", nullptr, BufferHint::KEEP_HOT);
    IndexScanExecutor exec(plan.get());

    exec.Init();

    Tuple t;
    RID r;
    AssertTrue(exec.Next(&t, &r), "Next should find key from B+ tree index lookup");
    AssertTrue(r.page_id == 12 && r.slot_id == 34, "B+ tree index lookup returned wrong RID");
    AssertTrue(!exec.Next(&t, &r), "Subsequent Next should be false");

    // Search second key 888
    auto plan888 = std::make_shared<IndexScanPlanNode>(schema, "test_table", idx_file, "888", nullptr, BufferHint::DISCARD_QUICKLY);
    IndexScanExecutor exec888(plan888.get());
    exec888.Init();
    AssertTrue(exec888.Next(&t, &r), "Next should find key 888 from B+ tree index lookup");
    AssertTrue(r.page_id == 5 && r.slot_id == 99, "B+ tree index lookup returned wrong RID for 888");

    // Test non-existent key search
    auto missing_plan = std::make_shared<IndexScanPlanNode>(schema, "test_table", idx_file, "99999", nullptr);
    IndexScanExecutor missing_exec(missing_plan.get());
    missing_exec.Init();
    AssertTrue(!missing_exec.Next(&t, &r), "Missing key search should return false");

    // Test invalid string search key (non-numeric)
    auto invalid_key_plan = std::make_shared<IndexScanPlanNode>(schema, "test_table", idx_file, "not_a_number", nullptr);
    IndexScanExecutor invalid_exec(invalid_key_plan.get());
    invalid_exec.Init(); // Should not throw exception
    AssertTrue(!invalid_exec.Next(&t, &r), "Invalid key search should return false gracefully");

    unlink(idx_file);
    std::cout << "[PASSED] TestIndexScanBPlusTreeIntegration" << std::endl;
}

// 4. Verify GlobalBufferPoolManager + Multi-Column Tuple Deserialization + Hint Propagation
static void TestIndexScanBufferPoolDeserialization() {
    std::cout << "running TestIndexScanBufferPoolDeserialization..." << std::endl;

    const std::string table_name = "m2_products";
    unlink((table_name + ".db").c_str());

    GlobalBufferPoolManager bpm(5);

    uint32_t page_id = 0;
    SlottedPage* page = bpm.NewPage(table_name, &page_id, BufferHint::DEFAULT);
    AssertTrue(page != nullptr, "NewPage failed");

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);
    schema.AddColumn("name", TypeId::VARCHAR);

    // Build raw tuple 0: id=500, name="Widget"
    TupleBuilder builder(&schema);
    builder.SetInt("id", 500);
    builder.SetVarchar("name", "Widget");
    uint16_t slot_id0 = page->GetHeader()->num_slots;
    AssertTrue(page->InsertTuple(builder.GetData(), builder.GetSize()), "InsertTuple 0 failed");

    // Build raw tuple 1: id=501, name="Gadget"
    TupleBuilder builder2(&schema);
    builder2.SetInt("id", 501);
    builder2.SetVarchar("name", "Gadget");
    uint16_t slot_id1 = page->GetHeader()->num_slots;
    AssertTrue(page->InsertTuple(builder2.GetData(), builder2.GetSize()), "InsertTuple 1 failed");

    bpm.UnpinPage(table_name, page_id, true);

    TableMetadata meta;
    meta.table_name = table_name;
    meta.schema = schema;

    // Create plan node with BufferHint::KEEP_HOT
    auto plan = std::make_shared<IndexScanPlanNode>(schema, table_name, "idx_prod", "500", &meta, BufferHint::KEEP_HOT);
    IndexScanExecutor exec(plan.get(), &meta, &bpm);

    // Pass matching RIDs
    exec.SetRIDs({RID(page_id, slot_id0), RID(page_id, slot_id1)});
    exec.Init();

    Tuple tuple_out;
    RID rid_out;

    // First tuple
    AssertTrue(exec.Next(&tuple_out, &rid_out), "First Next should succeed fetching from BPM");
    AssertTrue(rid_out.page_id == page_id && rid_out.slot_id == slot_id0, "RID 0 out mismatch");
    AssertTrue(tuple_out.size() == 2, "Deserialized tuple 0 field count mismatch");
    AssertTrue(tuple_out[0] == "500", "Deserialized INTEGER column 0 mismatch");
    AssertTrue(tuple_out[1] == "Widget", "Deserialized VARCHAR column 0 mismatch");

    // Second tuple
    AssertTrue(exec.Next(&tuple_out, &rid_out), "Second Next should succeed fetching from BPM");
    AssertTrue(rid_out.page_id == page_id && rid_out.slot_id == slot_id1, "RID 1 out mismatch");
    AssertTrue(tuple_out.size() == 2, "Deserialized tuple 1 field count mismatch");
    AssertTrue(tuple_out[0] == "501", "Deserialized INTEGER column 1 mismatch");
    AssertTrue(tuple_out[1] == "Gadget", "Deserialized VARCHAR column 1 mismatch");

    // Exhausted
    AssertTrue(!exec.Next(&tuple_out, &rid_out), "Third Next should return false");

    bpm.FlushAllPages();
    unlink((table_name + ".db").c_str());

    std::cout << "[PASSED] TestIndexScanBufferPoolDeserialization" << std::endl;
}

// 5. Verify Edge Cases: non-existent page file exception handling
static void TestIndexScanEdgeCases() {
    std::cout << "running TestIndexScanEdgeCases..." << std::endl;

    const std::string table_name = "m2_edge_table";
    unlink((table_name + ".bd").c_str());

    GlobalBufferPoolManager bpm(5);

    Schema schema;
    schema.AddColumn("id", TypeId::INTEGER);

    TableMetadata meta;
    meta.table_name = table_name;
    meta.schema = schema;

    auto plan = std::make_shared<IndexScanPlanNode>(schema, table_name, "idx", "1", &meta);
    IndexScanExecutor exec(plan.get(), &meta, &bpm);

    // Query non-existent page (page_id 999) without table creation
    exec.SetRIDs({RID(999, 0)});
    exec.Init();

    Tuple t;
    RID r;
    bool caught_exception = false;
    try {
        exec.Next(&t, &r);
    } catch (const std::runtime_error& e) {
        caught_exception = true;
    }
    AssertTrue(caught_exception, "Fetching non-existent page should throw runtime_error");

    std::cout << "[PASSED] TestIndexScanEdgeCases" << std::endl;
}

int main() {
    std::cout << "=== starting Milestone 2 Challenger 2 Empirical Test Suite ===" << std::endl;
    TestIndexScanPlanNode();
    TestIndexScanExecutorBasics();
    TestIndexScanBPlusTreeIntegration();
    TestIndexScanBufferPoolDeserialization();
    TestIndexScanEdgeCases();
    std::cout << "=== ALL Milestone 2 Challenger 2 Tests PASSED ===" << std::endl;
    return 0;
}
