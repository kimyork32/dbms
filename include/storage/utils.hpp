#pragma once

#include "Schema.hpp"

namespace megatron {

/**
 * @brief prints a tuple to the standard output using its schema
 * @param schema pointer to the schema definition
 * @param tuple_data pointer to the raw tuple buffer
 */
void PrintTuple(const Schema* schema, const char* tuple_data);

} // namespace megatron
