#include <iostream>
#include <string>
#include "execution/QueryExecutor.hpp"
#include "benchmarks/PerformanceTests.hpp"
#include "storage/disk_storage_engine.hpp"

using namespace megatron;

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
        megatron::benchmarks::RunPerformanceTests(storage);
    } else {
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
    }

    return 0;
}
