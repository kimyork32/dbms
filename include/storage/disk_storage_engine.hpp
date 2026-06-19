#pragma once

#include <string>
#include <vector>
#include <map>
#include "storage/storage_engine.hpp"
#include "storage/schema.hpp"
#include "storage/buffer_pool_manager.hpp"

namespace megatron {

/**
 * implements a disk-based storage engine
 */
class DiskStorageEngine : public IStorageEngine {
public:
    DiskStorageEngine();

    /**
     * @brief creates a new table
     * @param table_name name of the table
     * @param columns list of column definitions
     * @return true if creation succeeded
     */
    bool CreateTable(const std::string& table_name, const std::vector<std::string>& columns) override;

    /**
     * @brief checks if a table exists
     * @param table_name name of the table
     * @return true if table exists
     */
    bool TableExists(const std::string& table_name) const override;

    /**
     * @brief inserts a tuple into a table
     * @param table_name name of the table
     * @param tuple tuple to insert
     * @return true if insertion succeeded
     */
    bool InsertTuple(const std::string& table_name, const Tuple& tuple) override;

    /**
     * @brief updates tuples matching a condition
     * @param table_name name of the table
     * @param set_col column to update
     * @param set_val new value
     * @param where_col condition column
     * @param where_val condition value
     * @return true if update succeeded
     */
    bool UpdateTuple(const std::string& table_name, const std::string& set_col, const std::string& set_val, const std::string& where_col, const std::string& where_val) override;

    /**
     * @brief deletes tuples matching a condition
     * @param table_name name of the table
     * @param where_col condition column
     * @param where_val condition value
     * @return true if deletion succeeded
     */
    bool DeleteTuple(const std::string& table_name, const std::string& where_col, const std::string& where_val) override;

    /**
     * @brief retrieves all tuples from a table
     * @param table_name name of the table
     * @return vector of tuples
     */
    std::vector<Tuple> FullScan(const std::string& table_name) override;

private:
    std::map<std::string, Schema> schemas_;
    GlobalBufferPoolManager bpm_;
};

}
