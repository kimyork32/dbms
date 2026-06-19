#pragma once

#include <string>

namespace megatron {
class DiskStorageEngine; // forward declaration

namespace execution {
/**
 * @brief executes an sql query on the storage engine
 * @param query the sql query string
 * @param storage reference to the storage engine
 * @param print_results whether to print the results to console
 */
void ExecuteQuery(const std::string& query, DiskStorageEngine& storage, bool print_results = true);
} // namespace execution
} // namespace megatron
