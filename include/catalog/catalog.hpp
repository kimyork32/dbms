#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

#include "storage/record/schema.hpp"

namespace megatron {

/**
 * @brief metadata structure for a table in the catalog
 */
struct TableMetadata {
    uint32_t table_id = 0;
    std::string table_name;
    Schema schema;

    TableMetadata() = default;
    TableMetadata(uint32_t id, std::string name, Schema sch)
        : table_id(id), table_name(std::move(name)), schema(std::move(sch)) {}
};

/**
 * @brief catalog class managing table metadata
 */
class Catalog {
public:
    Catalog() = default;

    /**
     * @brief creates a new table with a given schema
     * @param table_name name of the table
     * @param schema schema of the table
     * @return pointer to table metadata
     */
    const TableMetadata* CreateTable(const std::string& table_name, const Schema& schema);

    /**
     * @brief creates a new table with a list of column names
     * @param table_name name of the table
     * @param columns column names vector
     * @return pointer to table metadata
     */
    const TableMetadata* CreateTable(const std::string& table_name, const std::vector<std::string>& columns);

    /**
     * @brief retrieves table metadata by table name
     * @param table_name name of the table
     * @return pointer to metadata or nullptr if not found
     */
    const TableMetadata* GetTable(const std::string& table_name) const;

    /**
     * @brief retrieves table metadata by table id
     * @param table_id id of the table
     * @return pointer to metadata or nullptr if not found
     */
    const TableMetadata* GetTable(uint32_t table_id) const;

    /**
     * @brief checks if a table exists
     * @param table_name name of the table
     * @return true if table exists
     */
    bool TableExists(const std::string& table_name) const;

private:
    std::unordered_map<std::string, TableMetadata> tables_by_name_;
    std::unordered_map<uint32_t, std::string> table_id_to_name_;
    uint32_t next_table_id_ = 1;
};

} // namespace megatron
