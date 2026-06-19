#include "benchmarks/performance_tests.hpp"
#include "storage/disk_storage_engine.hpp"
#include <iostream>

using namespace megatron;

int main() {
    DiskStorageEngine storage;
    
    std::cout << "Starting Megatron Benchmark Suite...\n";
    megatron::benchmarks::RunPerformanceTests(storage);
    
    return 0;
}
