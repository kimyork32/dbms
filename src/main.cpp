#include <iostream>
#include <string>
#include <chrono>
#include "parser/parser.hpp"

// MVP Inclusions
#include "storage/disk_storage_engine.hpp"
#include "storage/BPlusTree.hpp"

using namespace megatron;

void ExecuteQuery(const std::string& query, DiskStorageEngine& storage, bool print_results = true) {
    if (query.empty()) return;
    try {
        auto ast = Parser::Parse(query);
        if (!ast) return;
        
        if (ast->GetType() == StatementType::CREATE) {
            auto stmt = static_cast<CreateStatement*>(ast.get());
            if (print_results) std::cout << "[Execution] Creando tabla: " << stmt->table_name << "\n";
            storage.CreateTable(stmt->table_name, stmt->columns);
        } 
        else if (ast->GetType() == StatementType::INSERT) {
            auto stmt = static_cast<InsertStatement*>(ast.get());
            bool ok = storage.InsertTuple(stmt->table_name, stmt->values);
            if (print_results && !ok) std::cout << "[Execution] Failed to insert.\n";
        }
        else if (ast->GetType() == StatementType::UPDATE) {
            auto stmt = static_cast<UpdateStatement*>(ast.get());
            if (stmt->where_clause.active) {
                storage.UpdateTuple(stmt->table_name, stmt->set_column, stmt->set_value, stmt->where_clause.column, stmt->where_clause.value);
            } else {
                if (print_results) std::cout << "[Execution] UPDATE without WHERE not supported.\n";
            }
        }
        else if (ast->GetType() == StatementType::DELETE) {
            auto stmt = static_cast<DeleteStatement*>(ast.get());
            if (stmt->where_clause.active) {
                storage.DeleteTuple(stmt->table_name, stmt->where_clause.column, stmt->where_clause.value);
            } else {
                if (print_results) std::cout << "[Execution] DELETE without WHERE not supported.\n";
            }
        }
        else if (ast->GetType() == StatementType::SELECT) {
            auto stmt = static_cast<SelectStatement*>(ast.get());
            auto results = storage.FullScan(stmt->table_name);
            
            if (print_results) {
                std::cout << "[Execution] Resultados de SELECT:\n";
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

void RunPerformanceTests(DiskStorageEngine& storage) {
    std::cout << "--- Inciando Tests de Rendimiento ---\n";
    
    // Preparación
    ExecuteQuery("CREATE TABLE perftest (id, data, moredata)", storage, false);

    const int NUM_RECORDS = 1000000; // Ahora puede manejar muchos más gracias a la paginación dinámica.

    std::cout << "Insertando " << NUM_RECORDS << " registros...\n";
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS; ++i) {
        std::string query = "INSERT INTO perftest VALUES (" + std::to_string(i) + ", 'TestData" + std::to_string(i) + "', 'MoreData" + std::to_string(i) + "')";
        ExecuteQuery(query, storage, false);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Tiempo de Insercion: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "Realizando un Full Scan...\n";
    start = std::chrono::high_resolution_clock::now();
    ExecuteQuery("SELECT * FROM perftest", storage, false);
    end = std::chrono::high_resolution_clock::now();
    std::cout << "Tiempo de Lectura (Full Scan): " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "Actualizando " << NUM_RECORDS / 2 << " registros (por PK)...\n";
    start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS / 2; ++i) {
        std::string query = "UPDATE perftest SET data = 'UpdatedData' WHERE id = " + std::to_string(i);
        ExecuteQuery(query, storage, false);
    }
    end = std::chrono::high_resolution_clock::now();
    std::cout << "Tiempo de Actualizacion: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "Eliminando " << NUM_RECORDS / 2 << " registros (por PK)...\n";
    start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS / 2; ++i) {
        std::string query = "DELETE FROM perftest WHERE id = " + std::to_string(i);
        ExecuteQuery(query, storage, false);
    }
    end = std::chrono::high_resolution_clock::now();
    std::cout << "Tiempo de Eliminacion: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "--- Fin de Tests de Rendimiento ---\n";
}

int main(int argc, char* argv[]) {
    DiskStorageEngine storage;

    bool perf_mode = false;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--perf") {
                perf_mode = true;
            }
        }
    }

    if (perf_mode) {
        RunPerformanceTests(storage);
    } else {
        std::cout << "--- Megatron SQL CLI ---\n";
        std::cout << "Type your SQL queries (or type 'exit' to quit):\n";
        
        std::string line;
        while (true) {
            std::cout << "megatron> ";
            if (!std::getline(std::cin, line)) break;
            if (line == "exit" || line == "quit") break;
            if (line.empty()) continue;

            ExecuteQuery(line, storage, true);
        }
    }

    return 0;
}
