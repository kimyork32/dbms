#pragma once

namespace megatron {
class DiskStorageEngine; // forward declaration

namespace benchmarks {
/**
 * @brief executes a suite of performance tests (insert, read, update, delete)
 * @param storage reference to the storage engine
 */
void RunPerformanceTests(DiskStorageEngine& storage);
} // namespace benchmarks
} // namespace megatron
