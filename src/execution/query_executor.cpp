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
            auto results = storage.FullScan(stmt->table_name);
            
            if (print_results) {
                std::cout << "[Execution] select results:\n";
                int count = 0;
                for (const auto& row : results) {
                    if (stmt->where_clause.active) {
                        bool match = false;
                        for (const auto& val : row) {
                            if (val == stmt->where_clause.value) match = true;
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
