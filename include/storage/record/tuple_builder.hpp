#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include "storage/record/schema.hpp"

namespace megatron {

/**
 * @brief constructs binary tuples based on a schema
 */
class TupleBuilder {
private:
    const Schema* schema_;
    std::vector<char> buffer_;
    uint16_t current_var_offset_;

public:
    /**
     * @brief initializes builder with a schema
     * @param s pointer to schema
     */
    TupleBuilder(const Schema* s);

    /**
     * @brief sets an integer field
     * @param col_name name of the column
     * @param value integer value
     */
    void SetInt(const std::string& col_name, int32_t value);

    /**
     * @brief sets a varchar field
     * @param col_name name of the column
     * @param value string value
     */
    void SetVarchar(const std::string& col_name, const std::string& value);

    /**
     * @brief gets the constructed tuple data
     * @return pointer to data buffer
     */
    const char* GetData() const;

    /**
     * @brief gets the total size of the tuple
     * @return size in bytes
     */
    uint16_t GetSize() const;
};

} // namespace megatron
