#include "storage/Schema.hpp"

void Schema::add_column(const std::string& name, TypeId type) {
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

uint16_t Schema::get_variable_directory_offset() const {
    return tuple_header_size + total_fixed_size;
}

