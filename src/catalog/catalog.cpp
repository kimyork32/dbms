#include "catalog/catalog.hpp"

namespace megatron {

const TableMetadata* Catalog::CreateTable(const std::string& table_name, const Schema& schema) {
    if (TableExists(table_name)) {
        return GetTable(table_name);
    }
    uint32_t id = next_table_id_++;
    TableMetadata meta(id, table_name, schema);
    tables_by_name_[table_name] = std::move(meta);
    table_id_to_name_[id] = table_name;
    return &tables_by_name_[table_name];
}

const TableMetadata* Catalog::CreateTable(const std::string& table_name, const std::vector<std::string>& columns) {
    if (TableExists(table_name)) {
        return GetTable(table_name);
    }
    Schema schema;
    for (const auto& col_name : columns) {
        schema.AddColumn(col_name, TypeId::VARCHAR);
    }
    return CreateTable(table_name, schema);
}

const TableMetadata* Catalog::GetTable(const std::string& table_name) const {
    auto it = tables_by_name_.find(table_name);
    if (it == tables_by_name_.end()) {
        return nullptr;
    }
    return &it->second;
}

const TableMetadata* Catalog::GetTable(uint32_t table_id) const {
    auto it = table_id_to_name_.find(table_id);
    if (it == table_id_to_name_.end()) {
        return nullptr;
    }
    return GetTable(it->second);
}

bool Catalog::TableExists(const std::string& table_name) const {
    return tables_by_name_.find(table_name) != tables_by_name_.end();
}

} // namespace megatron
