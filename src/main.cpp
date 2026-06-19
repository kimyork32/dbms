#include <iostream>
#include <string>
#include "execution/query_executor.hpp"
#include "storage/engine/disk_storage_engine.hpp"

using namespace megatron;

int main() {
    DiskStorageEngine storage;

    std::cout << "--- Megatron SQL CLI ---\n";
    std::cout << "Type your SQL queries (or type 'exit' to quit):\n";
    
    std::string line;
    while (true) {
        std::cout << "megatron> ";
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;

        megatron::execution::ExecuteQuery(line, storage, true);
    }

    return 0;
}
