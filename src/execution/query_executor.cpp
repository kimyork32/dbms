#include "execution/query_executor.hpp"
#include "parser/parser.hpp"
#include "storage/engine/disk_storage_engine.hpp"
#include <iostream>

namespace megatron {
namespace execution {

void ExecuteQuery(const std::string& query, DiskStorageEngine& storage, bool print_results) {
    if (query.empty()) return;
    try {
        auto ast = Parser::Parse(query);
        if (!ast) return;
        
        if (ast->GetType() == StatementType::CREATE) {
            auto stmt = static_cast<CreateStatement*>(ast.get());
            if (print_results) std::cout << "[Execution] creating table: " << stmt->table_name << "\n";
            storage.CreateTable(stmt->table_name, stmt->columns);
        } 
        else if (ast->GetType() == StatementType::INSERT) {
            auto stmt = static_cast<InsertStatement*>(ast.get());
            bool ok = storage.InsertTuple(stmt->table_name, stmt->values);
            if (print_results && !ok) std::cout << "[Execution] failed to insert.\n";
        }
        else if (ast->GetType() == StatementType::UPDATE) {
            auto stmt = static_cast<UpdateStatement*>(ast.get());
            if (stmt->where_clause.active) {
                storage.UpdateTuple(stmt->table_name, stmt->set_column, stmt->set_value, stmt->where_clause.column, stmt->where_clause.value);
            } else {
                if (print_results) std::cout << "[Execution] update without where not supported.\n";
            }
        }
        else if (ast->GetType() == StatementType::DELETE) {
            auto stmt = static_cast<DeleteStatement*>(ast.get());
            if (stmt->where_clause.active) {
                storage.DeleteTuple(stmt->table_name, stmt->where_clause.column, stmt->where_clause.value);
            } else {
                if (print_results) std::cout << "[Execution] delete without where not supported.\n";
            }
        }
        else if (ast->GetType() == StatementType::SELECT) {
            auto stmt = static_cast<SelectStatement*>(ast.get());
            std::string tbl = stmt->table_name;
            if (tbl.empty() && stmt->from_table && stmt->from_table->GetRefType() == TableRefType::BASE_TABLE) {
                tbl = static_cast<BaseTableRef*>(stmt->from_table.get())->table_name;
            }
            auto results = storage.FullScan(tbl);
            
            if (print_results) {
                std::cout << "[Execution] select results:\n";
                int count = 0;
                for (const auto& row : results) {
                    if (stmt->where_clause) {
                        bool match = false;
                        if (stmt->where_clause->GetExprType() == ExpressionType::BINARY_OP) {
                            auto bin_op = static_cast<BinaryOpExpression*>(stmt->where_clause.get());
                            if (bin_op->right && bin_op->right->GetExprType() == ExpressionType::LITERAL) {
                                std::string target_val = static_cast<LiteralExpression*>(bin_op->right.get())->value;
                                for (const auto& val : row) {
                                    if (val == target_val) match = true;
                                }
                            }
                        }
                        if (!match) continue;
                    }

                    for (const auto& val : row) std::cout << val << " | ";
                    std::cout << "\n";
                    count++;
                }
                std::cout << "(" << count << " rows)\n";
            }
        }
    } catch (const std::exception& e) {
        if (print_results) std::cerr << "Error: " << e.what() << "\n";
    }
}

} // namespace execution
} // namespace megatron
