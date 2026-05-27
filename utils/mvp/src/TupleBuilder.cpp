#include <cstring>

#include "../include/TupleBuilder.hpp"

TupleBuilder::TupleBuilder(const Schema* s) : schema(s) {
    uint16_t base_size = schema->tuple_header_size + 
                         schema->total_fixed_size + 
                         (schema->num_variable_cols * 4);
    buffer.assign(base_size, 0);
    current_var_offset = base_size;
}

void TupleBuilder::set_int(const std::string& col_name, int32_t value) {
    for (const auto& col : schema->columns) {
        if (col.name == col_name && col.type == TypeId::INTEGER) {
            std::memcpy(buffer.data() + col.fixed_offset, &value, sizeof(int32_t));
            return;
        }
    }
}

void TupleBuilder::set_varchar(const std::string& col_name, const std::string& value) {
    for (const auto& col : schema->columns) {
        if (col.name == col_name && col.is_variable) {
            uint16_t text_length = value.size();
            uint16_t dir_pos = schema->get_variable_directory_offset() + (col.var_index * 4);

            std::memcpy(buffer.data() + dir_pos, &current_var_offset, sizeof(uint16_t));
            std::memcpy(buffer.data() + dir_pos + 2, &text_length, sizeof(uint16_t));

            buffer.insert(buffer.end(), value.begin(), value.end());
            current_var_offset += text_length;
            return;
        }
    }
}

const char* TupleBuilder::get_data() const { return buffer.data(); }

uint16_t TupleBuilder::get_size() const { return buffer.size(); }
