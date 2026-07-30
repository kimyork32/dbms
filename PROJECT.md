# Project: Operator-Aware Buffer Management for DBMS

## Architecture
- **Buffer Pool Management**: `GlobalBufferPoolManager` handles page fetch, pin/unpin, 4-tier clock-sweep frame eviction based on `BufferHint` (`DEFAULT`, `KEEP_HOT`, `DISCARD_QUICKLY`), and metrics collection (`page_hits_`, `page_misses_`, `disk_writes_`).
- **Optimizer**: `Optimizer::InjectBufferHints` analyzes physical plan nodes and injects buffer access hints down plan subtrees before execution.
- **Executors**: Volcano iterator model (`AbstractExecutor`) with `Init()` and `Next()`. `NestedLoopJoinExecutor` performs outer/inner loop iteration, with inner child rescan resetting inner executor. `SeqScanExecutor` clears cached tuples on `Init()` when `bpm_ != nullptr` to enforce hint-based page re-fetching.
- **Benchmarks**: Synthetic workload generator and execution harness running Nested Loop Join, Hash Join, and Concurrent SeqScan + Join queries across 10%, 25%, 50%, and 80% buffer pool capacities, printing ASCII comparative performance tables.

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | GlobalBufferPoolManager Metrics (R3) | Instrument `page_hits_`, `page_misses_`, `disk_writes_`, `GetMissRatio()`, `GetDiskIOCount()`, `ResetMetrics()`, and parameterize `DiskStorageEngine(pool_size)`. | M1 | ORIGINAL_REQUEST.md / docs |
| 2 | Optimizer Hint Injection Rules Fix (R2) | Fix `HashJoin` build side hint (`KEEP_HOT`), standalone `SeqScan` hint (`DISCARD_QUICKLY`), and verify `NestedLoopJoin` hints. | M2 | ORIGINAL_REQUEST.md / docs |
| 3 | NestedLoopJoinExecutor & Rescan Fix (R1 & R4) | Implement `NestedLoopJoinExecutor` in `executor.hpp`/`executor.cpp` and fix `SeqScanExecutor::Init()` to clear `tuples_` on `bpm_ != nullptr`. | M3 | ORIGINAL_REQUEST.md / docs |
| 4 | Operator-Aware Benchmark Workloads (R5) | Build synthetic table generator and benchmark harness testing NLJ, HashJoin, and Concurrent workloads across 10%, 25%, 50%, 80% pool sizes, printing comparison tables. | M4 | ORIGINAL_REQUEST.md / docs |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Buffer Pool Manager Metrics & Parameterized Storage Engine | `include/storage/page/buffer_pool_manager.hpp`, `src/storage/page/buffer_pool_manager.cpp`, `include/storage/engine/disk_storage_engine.hpp`, `src/storage/engine/disk_storage_engine.cpp` | none | DONE |
| M2 | Optimizer Hint Injection Rules Fix | `src/optimizer/optimizer.cpp` | M1 | DONE |
| M3 | NestedLoopJoinExecutor & SeqScan Rescan Fix | `include/execution/executor.hpp`, `src/execution/executor.cpp` | M1, M2 | DONE |
| M4 | Benchmark Workloads & Synthetic Data Generator | `src/benchmarks/operator_aware_benchmark.cpp`, `CMakeLists.txt` | M1, M2, M3 | DONE |

## Interface Contracts
### GlobalBufferPoolManager Interface
- `void ResetMetrics()`: Resets `page_hits_`, `page_misses_`, `disk_writes_` to 0.
- `size_t GetPageHits() const`: Returns total page hit count.
- `size_t GetPageMisses() const`: Returns total page miss count.
- `size_t GetDiskWrites() const`: Returns total disk write count.
- `size_t GetDiskIOCount() const`: Returns `page_misses_ + disk_writes_`.
- `double GetMissRatio() const`: Returns `page_misses_ / (page_hits_ + page_misses_)` (0.0 if total fetches == 0).

### Optimizer Buffer Hint Rules
- `PlanType::HashJoin`: `left_child` (build side) -> `BufferHint::KEEP_HOT`, `right_child` (probe side) -> `BufferHint::DISCARD_QUICKLY`.
- `PlanType::SeqScan`: Standalone catalog table -> `BufferHint::KEEP_HOT`, standalone normal table -> `BufferHint::DISCARD_QUICKLY`.
- `PlanType::NestedLoopJoin`: `left_child` (outer loop) -> `BufferHint::DISCARD_QUICKLY`, `right_child` (inner loop) -> `BufferHint::KEEP_HOT`.

### NestedLoopJoinExecutor Interface
- Header: `include/execution/executor.hpp`
- Class: `class NestedLoopJoinExecutor : public AbstractExecutor`
- Constructor: `NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan, std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child)`
- Methods: `void Init() override`, `bool Next(Tuple *tuple, RID *rid) override`, `const Schema *GetOutputSchema() const override`.

## Code Layout
- `include/storage/page/buffer_pool_manager.hpp` & `src/storage/page/buffer_pool_manager.cpp`
- `include/storage/engine/disk_storage_engine.hpp` & `src/storage/engine/disk_storage_engine.cpp`
- `src/optimizer/optimizer.cpp`
- `include/execution/executor.hpp` & `src/execution/executor.cpp`
- `src/benchmarks/operator_aware_benchmark.cpp`
- `CMakeLists.txt`
