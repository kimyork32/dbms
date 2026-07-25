# Project: DBMS Query Execution Pipeline

## Architecture
The DBMS Query Execution Pipeline is structured into four distinct, loosely coupled components:
1. **R1 Frontend (Lexer & Parser)**: Converts SQL query strings into structured Abstract Syntax Trees (`ASTNode`). Independent of storage and execution engine.
2. **R2 Backend (Volcano Execution Engine)**: Standard Volcano iterator execution model (`AbstractExecutor` base class). Processes tuple streams pull-style via `Init()` and `Next(Tuple*, RID*)`. Can be driven directly by C++ tests via physical plan nodes or `ExecutorFactory`.
3. **R3 Middle-end (Catalog, Binder & Optimizer)**: Resolves raw AST nodes against database schemas in `Catalog`, performs semantic validation and column binding in `Binder`, generates logical plan trees, and optimizes them into physical plan trees (`AbstractPlanNode`).
4. **R4 Orchestration & Integration**: Provides the top-level pipeline interface (`ExecuteQuery`), orchestrating SQL text parsing -> semantic binding -> optimization -> physical executor construction -> Volcano iterator execution. Also updates CMake configuration and verifies benchmark suite.

## Code Layout
- `include/parser/lexer.hpp`, `include/parser/ast.hpp`, `include/parser/parser.hpp`
- `src/parser/lexer.cpp`, `src/parser/parser.cpp`
- `include/execution/executor.hpp`, `include/execution/executor_factory.hpp`
- `src/execution/executor.cpp`, `src/execution/executor_factory.cpp`
- `include/catalog/catalog.hpp`, `src/catalog/catalog.cpp`
- `include/binder/binder.hpp`, `include/binder/table_ref.hpp`, `include/binder/statement.hpp`
- `src/binder/binder.cpp`
- `include/optimizer/optimizer.hpp`, `include/optimizer/plan_node.hpp`
- `src/optimizer/optimizer.cpp`, `src/optimizer/plan_node.cpp`
- `include/execution/query_executor.hpp`, `src/execution/query_executor.cpp`

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | TokenType & Lexer | Full TokenType enum, keyword/literal lexing, token stream generation | M1 | Survey |
| 2 | AST Hierarchy | Polymorphic ASTNode, SelectStatement, ExpressionNode (ColumnRef, Literal, BinaryOp, Aggregate) | M1 | Survey |
| 3 | SQL Parser | Parse Select, Join, Where (Filters), GroupBy, Having, OrderBy, Expressions into AST | M1 | Survey |
| 4 | Base Executor & Scan | AbstractExecutor, SeqScanExecutor connected to DiskStorageEngine | M2 | Survey |
| 5 | Join & Aggregation Iterators | HashJoinExecutor with hash table builder, AggregationExecutor (SUM, COUNT, MAX, MIN, AVG) | M2 | Survey |
| 6 | Filter & Projection Iterators | FilterExecutor (predicate evaluation), ProjectionExecutor (column selection) | M2 | Survey |
| 7 | Direct C++ Plan Execution | C++ unit tests constructing physical plan trees directly without SQL parser | M2 | Survey |
| 8 | Catalog Schema Metadata | TableMetadata, Column schema tracking, Catalog lookup interfaces | M3 | Survey |
| 9 | Binder (Logical Planner) | Resolve table names, column references, types against Catalog; output BoundStatement | M3 | Survey |
| 10 | Query Optimizer | Transform BoundStatement into logical plan and optimize into physical AbstractPlanNode tree | M3 | Survey |
| 11 | Executor Factory | Map AbstractPlanNode physical plan tree to stateful AbstractExecutor iterator tree | M4 | Survey |
| 12 | End-to-End Execution | Refactor ExecuteQuery to parse, bind, optimize, and pull tuples through Volcano iterators | M4 | Survey |
| 13 | CMake & Benchmark Verification | Add missing cpp files to CMakeLists.txt CORE_SOURCES, verify megatron_benchmark compile/run | M4 | Survey |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | R1 Frontend (Lexer & Parser) | Lexer, AST hierarchy, SQL Parser for Select/Join/GroupBy/Expressions | None | DONE |
| M2 | R2 Backend (Volcano Execution Engine) | AbstractExecutor, SeqScan, HashJoin, Aggregation, Filter, Projection, Direct C++ Tests | None | IN_PROGRESS |
| M3 | R3 Middle-end (Catalog, Binder & Optimizer) | Catalog, Binder, Logical/Physical Plan Nodes, Query Optimizer | M1, M2 | PLANNED |
| M4 | R4 Orchestration & Integration | ExecutorFactory, ExecuteQuery E2E pipeline, CMake update, Benchmark verification | M1, M2, M3 | PLANNED |

## Interface Contracts

### R1 (Frontend) ↔ R3 (Middle-end)
```cpp
namespace megatron {
struct ParseResult {
    bool success;
    std::string error_message;
    std::unique_ptr<ASTNode> ast;
};
ParseResult ParseQuery(const std::string& sql);
}
```

### R3 (Middle-end) ↔ R2 (Backend Execution)
```cpp
namespace megatron {
class AbstractPlanNode {
public:
    virtual ~AbstractPlanNode() = default;
    virtual PlanType GetType() const = 0;
    virtual const Schema& GetOutputSchema() const = 0;
    virtual const std::vector<std::shared_ptr<const AbstractPlanNode>>& GetChildren() const = 0;
};

class ExecutorFactory {
public:
    static std::unique_ptr<AbstractExecutor> CreateExecutor(
        ExecutorContext* exec_ctx, 
        const AbstractPlanNode* plan);
};
}
```

### R2 Backend Volcano Iterator Standard
```cpp
namespace megatron {
class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void Init() = 0;
    virtual bool Next(Tuple* tuple, RID* rid = nullptr) = 0;
    virtual const Schema& GetOutputSchema() const = 0;
};
}
```
