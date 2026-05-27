#pragma once
#include <string>
#include <vector>
#include <map>
#include "storage/storage_engine.hpp"
#include "storage/Schema.hpp"

#include "storage/BufferPoolManager.hpp"

namespace megatron {

/**
 * @brief Implementación de Storage Engine en disco utilizando MVP.
 */
class DiskStorageEngine : public IStorageEngine {
public:
    DiskStorageEngine();

    bool CreateTable(const std::string& table_name, const std::vector<std::string>& columns) override;
    bool TableExists(const std::string& table_name) const override;
    bool InsertTuple(const std::string& table_name, const Tuple& tuple) override;
    bool UpdateTuple(const std::string& table_name, const std::string& set_col, const std::string& set_val, const std::string& where_col, const std::string& where_val) override;
    bool DeleteTuple(const std::string& table_name, const std::string& where_col, const std::string& where_val) override;
    std::vector<Tuple> FullScan(const std::string& table_name) override;

private:
    std::map<std::string, Schema> schemas_;
    GlobalBufferPoolManager bpm_;
};

} // namespace megatron