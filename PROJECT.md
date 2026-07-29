# Project: Plan-Aware DBMS

## Architecture
The Plan-Aware DBMS introduces query-plan-driven buffer management hints (`BufferHint`) into a relational DBMS engine.
Query execution flow:
```
[ SQL Query String ]
        │
        ▼ Parser
  [ AST (SelectStatement) ]
        │
        ▼ Binder (Validates against Catalog tables & schemas)
  [ BoundSelectStatement ]
        │
        ▼ Optimizer (Injects BufferHint: DISCARD_QUICKLY, KEEP_HOT, DEFAULT)
  [ Physical Plan Tree (AbstractPlanNode) ]
        │
        ▼ Volcano Executor (AbstractExecutor)
  [ Access Methods (DiskStorageEngine / B+ Tree / Hash Index) ]
        │
        ▼ Propagates BufferHint to FetchPage / NewPage
  [ GlobalBufferPoolManager ] (Enforces 4-tier frame eviction: DISCARD_QUICKLY first, KEEP_HOT protected)
```

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | BufferHint Enum | Define `BufferHint` enum (`DEFAULT`, `DISCARD_QUICKLY`, `KEEP_HOT`) | M1 | survey |
| 2 | AbstractPlanNode Hint Support | Add `BufferHint` member and getters/setters to `AbstractPlanNode` | M1 | survey |
| 3 | Hint-Aware GlobalBufferPoolManager | Update `FetchPage`/`NewPage` signatures and implement 4-tier frame eviction (`DISCARD_QUICKLY` first, `KEEP_HOT` protected) | M1 | survey |
| 4 | AbstractExecutor Hint Extraction | Add `GetBufferHint()` helper to `AbstractExecutor` | M2 | survey |
| 5 | IndexScanPlanNode & IndexScanExecutor | Implement missing `IndexScanPlanNode` and `IndexScanExecutor` | M2 | survey |
| 6 | Access Method Hint Propagation | Propagate `BufferHint` through `SeqScan`, `B+ Tree`, `Hash Index`, and `IndexScan` access methods to `GlobalBufferPoolManager` | M2 | survey |
| 7 | Binder Implementation | Implement `Binder` for catalog table/column AST validation and schema resolution | M3 | survey |
| 8 | Optimizer & Smart Hint Injection | Implement `Optimizer` translating bound AST into physical plan nodes with intelligent `BufferHint` injection | M3 | survey |
| 9 | Automated C++ Test Suite | Implement `src/test_plan_aware.cpp` verifying replacement behavior, hint propagation, and optimizer injection | M4 | survey |
| 10 | CMake Integration | Update `CMakeLists.txt` to build `megatron_plan_aware_test` executable | M4 | survey |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | M1: BufferHint & Buffer Manager | Add `BufferHint` to `AbstractPlanNode`, update `GlobalBufferPoolManager` `FetchPage`/`NewPage` signatures and 4-tier hint-aware replacement logic | none | DONE |
| 2 | M2: Executor & Access Method Propagation | Add `GetBufferHint()` to `AbstractExecutor`, implement `IndexScanPlanNode`/`Executor`, propagate hints in `SeqScan`, `B+Tree`, `Hash`, and `IndexScan` access methods | M1 | DONE |
| 3 | M3: Binder & Optimizer | Implement `Binder` for AST vs Catalog validation, implement `Optimizer` for physical plan translation with smart hint injection | M1, M2 | DONE |
| 4 | M4: Integration Test Suite & CMake | Create `src/test_plan_aware.cpp` verifying hint replacement priorities, hint propagation, and optimizer injection; update `CMakeLists.txt` | M1, M2, M3 | IN_PROGRESS |

## Interface Contracts
### AbstractPlanNode ↔ GlobalBufferPoolManager
- `enum class BufferHint { DEFAULT = 0, KEEP_HOT, DISCARD_QUICKLY };`
- `SlottedPage* FetchPage(const std::string& table_name, uint32_t page_id, BufferHint hint = BufferHint::DEFAULT);`
- `SlottedPage* NewPage(const std::string& table_name, uint32_t* page_id, BufferHint hint = BufferHint::DEFAULT);`

### Binder ↔ Catalog
- `const TableMetadata* Catalog::GetTable(const std::string& table_name) const;`
- `int Schema::GetColIdx(const std::string& name) const;`

### Optimizer ↔ Physical Plan
- `std::shared_ptr<AbstractPlanNode> Optimizer::Optimize(const BoundSelectStatement& stmt);`

## Code Layout
- `include/storage/page/buffer_pool_manager.hpp` & `src/storage/page/buffer_pool_manager.cpp`
- `include/execution/plan_node.hpp` & `src/execution/plan_node.cpp`
- `include/execution/executor.hpp` & `src/execution/executor.cpp`
- `include/storage/engine/disk_storage_engine.hpp` & `src/storage/engine/disk_storage_engine.cpp`
- `include/storage/index/b_plus_tree.hpp` & `src/storage/index/b_plus_tree.cpp`
- `include/binder/binder.hpp`, `include/binder/bound_statement.hpp` & `src/binder/binder.cpp`
- `include/optimizer/optimizer.hpp` & `src/optimizer/optimizer.cpp`
- `src/test_plan_aware.cpp` & `CMakeLists.txt`
