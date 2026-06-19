#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include "storage/record/type_id.hpp"

namespace megatron {

/**
 * @brief defines a column in the database schema
 */
struct Column {
    std::string name;
    TypeId type;
    uint16_t size;
    uint16_t fixed_offset;
    bool is_variable;
    uint16_t var_index;
};

/**
 * @brief manages table structure and tuple layout
 */
class Schema {
public:
    std::vector<Column> columns;
    uint16_t tuple_header_size = 1; 
    uint16_t total_fixed_size = 0;
    uint16_t num_variable_cols = 0;

    /**
     * @brief adds a column to the schema
     * @param name column name
     * @param type column type
     */
    void AddColumn(const std::string& name, TypeId type);

    /**
     * @brief gets the start offset of variable length directory
     * @return byte offset
     */
    uint16_t GetVariableDirectoryOffset() const;
};

} // namespace megatron
