#pragma once

#include "Schema.hpp"

/**
 * contains general utility functions for the dbms project
 */

/**
 * @brief prints a tuple to the standard output using its schema
 * @param schema pointer to the schema definition
 * @param tuple_data pointer to the raw tuple buffer
 */
void print_tuple(const Schema* schema, const char* tuple_data);
