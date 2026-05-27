#include <iostream>
#include <cstring>

#include "storage/utils.hpp"

void print_tuple(const Schema* schema, const char* tuple_data) {
    std::cout << "{ ";
    for (size_t i = 0; i < schema->columns.size(); ++i) {
        const auto& col = schema->columns[i];
        std::cout << col.name << ": ";
        
        if (col.type == TypeId::INTEGER) {
            int32_t val;
            std::memcpy(&val, tuple_data + col.fixed_offset, sizeof(int32_t));
            std::cout << val;
        } else if (col.type == TypeId::SMALLINT) {
            int16_t val;
            std::memcpy(&val, tuple_data + col.fixed_offset, sizeof(int16_t));
            std::cout << val;
        } else if (col.is_variable) {
            uint16_t dir_pos = schema->get_variable_directory_offset() + (col.var_index * 4);
            uint16_t offset;
            uint16_t length;
            std::memcpy(&offset, tuple_data + dir_pos, sizeof(uint16_t));
            std::memcpy(&length, tuple_data + dir_pos + 2, sizeof(uint16_t));
            
            std::string val(tuple_data + offset, length);
            std::cout << "\"" << val << "\"";
        }
        
        if (i < schema->columns.size() - 1) std::cout << ", ";
    }
    std::cout << " }\n";
}
