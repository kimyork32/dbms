#include <cstring>

#include "storage/record/tuple_builder.hpp"

namespace megatron {

TupleBuilder::TupleBuilder(const Schema* s) : schema_(s) {
    uint16_t base_size = schema_->tuple_header_size + 
                         schema_->total_fixed_size + 
                         (schema_->num_variable_cols * 4);
    buffer_.assign(base_size, 0);
    current_var_offset_ = base_size;
}

void TupleBuilder::SetInt(const std::string& col_name, int32_t value) {
    for (const auto& col : schema_->columns) {
        if (col.name == col_name && col.type == TypeId::INTEGER) {
            std::memcpy(buffer_.data() + col.fixed_offset, &value, sizeof(int32_t));
            return;
        }
    }
}

void TupleBuilder::SetVarchar(const std::string& col_name, const std::string& value) {
    for (const auto& col : schema_->columns) {
        if (col.name == col_name && col.is_variable) {
            uint16_t text_length = value.size();
            uint16_t dir_pos = schema_->GetVariableDirectoryOffset() + (col.var_index * 4);

            std::memcpy(buffer_.data() + dir_pos, &current_var_offset_, sizeof(uint16_t));
            std::memcpy(buffer_.data() + dir_pos + 2, &text_length, sizeof(uint16_t));

            buffer_.insert(buffer_.end(), value.begin(), value.end());
            current_var_offset_ += text_length;
            return;
        }
    }
}

const char* TupleBuilder::GetData() const { return buffer_.data(); }

uint16_t TupleBuilder::GetSize() const { return buffer_.size(); }

} // namespace megatron
