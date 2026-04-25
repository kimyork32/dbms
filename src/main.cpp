#include <iostream>
#include <vector>
#include <string>
#include "parser/parser.hpp"
#include "storage/storage_engine.hpp"

using namespace megatron;

/**
 * @brief Implementación mínima de Storage Engine en memoria para fines de demostración.
 * Esto cumple con la interfaz IStorageEngine.
 */
class MemoryStorageEngine : public IStorageEngine {
public:
    bool CreateTable(const std::string& table_name, const std::vector<std::string>& columns) override {
        if (TableExists(table_name)) throw std::runtime_error("table exists");
        std::cout << "[Storage] Creando tabla: " << table_name << " con " << columns.size() << " columnas.\n";
        tables_[table_name] = {};
        return true;
    }

    bool TableExists(const std::string& table_name) const override {
        return tables_.find(table_name) != tables_.end();
    }

    bool InsertTuple(const std::string& table_name, const Tuple& tuple) override {
        if (!TableExists(table_name)) return false;
        std::cout << "[Storage] Insertando tupla en: " << table_name << "\n";
        tables_[table_name].push_back(tuple);
        return true;
    }

    std::vector<Tuple> FullScan(const std::string& table_name) override {
        if (!TableExists(table_name)) return {};
        return tables_[table_name];
    }

private:
    std::map<std::string, std::vector<Tuple>> tables_;
};

int main() {
    auto storage = std::make_unique<MemoryStorageEngine>();
    
    std::vector<std::string> queries = {
        "CREATE TABLE users (id, name, age)",
        "CREATE TABLE users (id, name, email)",
        "INSERT INTO users VALUES (1, 'Alice', 'alice@example.com')",
        "INSERT INTO users VALUES (2, 'Bob', 'bob@example.com')",
        "SELECT * FROM users WHERE name = 'Alice'"
    };

    std::cout << "--- Megatron SQL Simulator ---\n";

    for (const auto& query : queries) {
        std::cout << "\nQuery: " << query << "\n";
        try {
            auto ast = Parser::Parse(query);
            
            // Simulación de ejecución basada en el AST
            if (ast->GetType() == StatementType::CREATE) {
                auto stmt = static_cast<CreateStatement*>(ast.get());
                storage->CreateTable(stmt->table_name, stmt->columns);
            } 
            else if (ast->GetType() == StatementType::INSERT) {
                auto stmt = static_cast<InsertStatement*>(ast.get());
                storage->InsertTuple(stmt->table_name, stmt->values);
            }
            else if (ast->GetType() == StatementType::SELECT) {
                auto stmt = static_cast<SelectStatement*>(ast.get());
                auto results = storage->FullScan(stmt->table_name);
                
                std::cout << "[Execution] Resultados de SELECT:\n";
                for (const auto& row : results) {
                    // Filtrado rudimentario para demostración
                    if (stmt->where_clause.active) {
                        bool match = false;
                        for (const auto& val : row) {
                            if (val == stmt->where_clause.value) match = true;
                        }
                        if (!match) continue;
                    }

                    for (const auto& val : row) std::cout << val << " | ";
                    std::cout << "\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }

    return 0;
}
