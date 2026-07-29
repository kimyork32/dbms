# Megatron DBMS

Megatron DBMS is a relational database management system (RDBMS) designed for educational and research purposes. It is built using a modular architecture that clearly separates the responsibilities of query processing, execution, and disk storage, all implemented in modern C++.

---

## System Architecture

The architecture of Megatron follows the classic model of relational database systems, divided into the following main layers:

1. **Parser:** Takes a raw SQL query and generates an Abstract Syntax Tree (AST).
2. **Binder (Semantic Analyzer):** Validates the AST against the database Catalog, ensuring that the referenced tables and columns exist and the data types are compatible.
3. **Optimizer:** Takes the validated AST and generates a physical execution plan (`PlanNode`). During this phase, the optimizer also injects memory *hints* (`BufferHint`) based on heuristic rules (for example, keeping hash tables in RAM or quickly discarding pages after a massive sequential scan).
4. **Execution Engine:** Follows the Volcano (iterator) model. Each plan node is processed by an `Executor` that "pulls" tuples using the `Next()` method.
5. **Storage Engine:** Manages data persistence on disk. It includes the handling of pages (Slotted Pages), a global buffer manager (Buffer Pool Manager), and access and indexing structures.

---

## Directory Structure

The source code is logically organized, separating headers (`include/`) and implementations (`src/`).

```text
megatron_db/
├── CMakeLists.txt          # CMake build configuration
├── docs/                   # Documentation and project tasks
├── include/                # Header files (.hpp)
│   ├── binder/             # Semantic analyzer (Binder) logic and definitions
│   ├── catalog/            # System catalog (schemas and metadata)
│   ├── execution/          # Classes for plan nodes (PlanNode) and executors
│   ├── optimizer/          # Heuristic query optimizer
│   ├── parser/             # Lexer, Parser, and AST definitions
│   └── storage/            # Storage submodules:
│       ├── engine/         # Disk storage engine interface and core
│       ├── index/          # Data access methods (B+ Trees, Hash Tables)
│       ├── page/           # Disk page handling (SlottedPage) and Buffer Pool
│       └── record/         # Tuple definitions, data types, and schemas
└── src/                    # Source code (.cpp)
    ├── benchmarks/         # Unit, stress, and performance test suites (Tests)
    ├── binder/             # Binder implementation
    ├── catalog/            # Catalog implementation
    ├── execution/          # Executors implementation (SeqScan, HashJoin, etc.)
    ├── optimizer/          # Optimization rules implementation
    ├── parser/             # Parser implementation
    ├── storage/            # Deep storage implementation (Buffer Pool, I/O)
    └── main.cpp            # Application entry point
```

### Key Components Detail

- **`storage/page/buffer_pool_manager`:** Manages which disk pages are cached in RAM. It implements replacement algorithms and is also **Plan-Aware**; meaning it uses semantic hints from the optimizer (`BufferHint`) to decide when to keep a page in memory (`KEEP_HOT`) or when to discard it quickly (`DISCARD_QUICKLY`).
- **`execution/executor`:** Implements basic relational operators like `SeqScanExecutor`, `IndexScanExecutor`, `HashJoinExecutor`, `AggregationExecutor`, and `FilterExecutor`.
- **`storage/index/b_plus_tree`:** Persistent B+ tree structure on disk, responsible for index scanning to speed up lookups.
- **`benchmarks/`:** All modular tests are performed here (Modules M1, M2, M3), validating everything from buffer pool concurrency to end-to-end tests involving the Binder, Optimizer, and Executors.

---

## Build and Execution

Megatron DBMS uses CMake for its build system.

### Requirements
- CMake (>= 3.10)
- C++ compiler with C++17 support (GCC, Clang, or MSVC)

### Build

From the root of the project, run:

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

### Running Tests

To ensure the integrity and correct operation of the different modules:

```bash
cd build
ctest --output-on-failure
```

You can also run specific test executables directly from `build/`:
```bash
./megatron_m3_test
./megatron_binder_stress_test
```
