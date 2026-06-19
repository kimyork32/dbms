#include "benchmarks/PerformanceTests.hpp"
#include "execution/QueryExecutor.hpp"
#include "storage/disk_storage_engine.hpp"
#include <iostream>
#include <string>
#include <chrono>

namespace megatron {
namespace benchmarks {

void RunPerformanceTests(DiskStorageEngine& storage) {
    std::cout << "--- starting performance tests ---\n";
    
    // preparation
    execution::ExecuteQuery("CREATE TABLE perftest (id, data, moredata)", storage, false);

    const int NUM_RECORDS = 1000000; // can now handle many more thanks to dynamic paging.

    std::cout << "inserting " << NUM_RECORDS << " records...\n";
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS; ++i) {
        std::string query = "INSERT INTO perftest VALUES (" + std::to_string(i) + ", 'TestData" + std::to_string(i) + "', 'MoreData" + std::to_string(i) + "')";
        execution::ExecuteQuery(query, storage, false);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "insertion time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "performing full scan...\n";
    start = std::chrono::high_resolution_clock::now();
    execution::ExecuteQuery("SELECT * FROM perftest", storage, false);
    end = std::chrono::high_resolution_clock::now();
    std::cout << "read time (full scan): " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "updating " << NUM_RECORDS / 2 << " records (by PK)...\n";
    start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS / 2; ++i) {
        std::string query = "UPDATE perftest SET data = 'UpdatedData' WHERE id = " + std::to_string(i);
        execution::ExecuteQuery(query, storage, false);
    }
    end = std::chrono::high_resolution_clock::now();
    std::cout << "update time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "deleting " << NUM_RECORDS / 2 << " records (by PK)...\n";
    start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_RECORDS / 2; ++i) {
        std::string query = "DELETE FROM perftest WHERE id = " + std::to_string(i);
        execution::ExecuteQuery(query, storage, false);
    }
    end = std::chrono::high_resolution_clock::now();
    std::cout << "deletion time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

    std::cout << "--- end of performance tests ---\n";
}

} // namespace benchmarks
} // namespace megatron
