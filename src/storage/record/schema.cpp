#include "storage/record/schema.hpp"

namespace megatron {

void Schema::AddColumn(const std::string& name, TypeId type) {
    Column col;
    col.name = name;
    col.type = type;
    
    switch (type) {
        case TypeId::VARCHAR:
            col.is_variable = true;
            col.size = 0; 
            col.fixed_offset = 0; 
            col.var_index = num_variable_cols++;
            break;

        case TypeId::INTEGER:
            col.is_variable = false;
            col.size = 4;
            col.var_index = 0;
            break;

        case TypeId::SMALLINT:
            col.is_variable = false;
            col.size = 2;
            col.var_index = 0;
            break;
    }

    if (!col.is_variable) {
        col.fixed_offset = tuple_header_size + total_fixed_size;
        total_fixed_size += col.size;
    }

    columns.push_back(col);
}

int Schema::GetColIdx(const std::string& name) const {
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

uint16_t Schema::GetVariableDirectoryOffset() const {
    return tuple_header_size + total_fixed_size;
}

} // namespace megatron
