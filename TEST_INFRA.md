# E2E Test Infra: Plan-Aware DBMS

## Test Philosophy
- Requirement-driven, opaque-box and white-box integration testing for plan-aware buffer management.
- Test runner: `megatron_plan_aware_test` target in `CMakeLists.txt` executing `src/test_plan_aware.cpp`.

## Feature Inventory
| # | Feature | Source | Tier 1 | Tier 2 | Tier 3 | Tier 4 |
|---|---------|--------|:------:|:------:|:------:|:------:|
| 1 | BufferHint Enum & Frame Metadata | R1 | 5 | 5 | ✓ | ✓ |
| 2 | Hint-Aware Buffer Eviction | R1 | 5 | 5 | ✓ | ✓ |
| 3 | Executor & Access Method Hint Propagation | R2 | 5 | 5 | ✓ | ✓ |
| 4 | Binder AST-Catalog Validation | R3 | 5 | 5 | ✓ | ✓ |
| 5 | Optimizer Smart Hint Injection | R3 | 5 | 5 | ✓ | ✓ |

## Test Scenarios & Categories
- **Tier 1 (Feature Coverage)**: Basic functionality of `BufferHint`, `FetchPage`/`NewPage` signature default arguments, `AbstractPlanNode::GetBufferHint()`, and standard `Binder`/`Optimizer` node creation.
- **Tier 2 (Boundary & Corner Cases)**: Buffer pool size = 3, filling buffer pool with `DISCARD_QUICKLY`, `DEFAULT`, and `KEEP_HOT` frames; verifying that `DISCARD_QUICKLY` frame is evicted first upon new page request; verifying `KEEP_HOT` page protection when non-KEEP_HOT unpinned pages are available; verifying all-pinned frame fallback.
- **Tier 3 (Cross-Feature Combinations)**: End-to-end SQL query string -> `Parser::ParseQuery` -> `Binder::BindSelect` -> `Optimizer::Optimize` -> physical plan node tree -> `AbstractExecutor` -> `GlobalBufferPoolManager` calls with correct hint enforcement.
- **Tier 4 (Real-World Application Scenarios)**: Multi-table HashJoin query execution where build-side pages use `DISCARD_QUICKLY` and index lookup pages use `KEEP_HOT`, verifying query execution correctness and buffer page statistics under limited memory frames.

## Pass / Fail Criteria
- Executable exit code 0.
- All test assertions in `src/test_plan_aware.cpp` pass without memory leaks or undefined behavior (verified via AddressSanitizer `-fsanitize=address`).
