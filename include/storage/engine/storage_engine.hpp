#pragma once
#include <string>
#include <vector>
#include "storage/page/buffer_pool_manager.hpp"

namespace megatron {

// represents a simple column value (could be expanded to a more complex class for data types)
using Value = std::string;
using Tuple = std::vector<Value>;

/**
 * @brief base interface for the storage engine
 * defines the basic data access operations that the execution engine requires
 */
class StorageEngineInterface {
public:
    virtual ~StorageEngineInterface() = default;

    // metadata management
    virtual bool CreateTable(const std::string& table_name, const std::vector<std::string>& columns) = 0;
    virtual bool TableExists(const std::string& table_name) const = 0;

    // data operations
    virtual bool InsertTuple(const std::string& table_name, const Tuple& tuple) = 0;
    virtual bool UpdateTuple(const std::string& table_name, const std::string& set_col, const std::string& set_val, const std::string& where_col, const std::string& where_val) = 0;
    virtual bool DeleteTuple(const std::string& table_name, const std::string& where_col, const std::string& where_val) = 0;
    
    // table scanning in a real engine would use iterators (Volcano Model)
    // here we define a simplified version that returns all tuples
    virtual std::vector<Tuple> FullScan(const std::string& table_name, BufferHint hint = BufferHint::DEFAULT) = 0;

    // in the future, TransactionManager, LockManager, etc. would go here
};

} // namespace megatron
