#pragma once

#include "SlottedPage.hpp"

/**
 * provides input and output utilities for persistent storage
 */

/**
 * @brief writes a page to disk
 * @param target_page page number
 * @param page page data
 * @param filename target file
 */
void write_page(uint32_t target_page, SlottedPage& page, const char* filename);

/**
 * @brief reads a page from disk
 * @param target_page page number
 * @param page page object to fill
 * @param filename source file
 */
void read_page(uint32_t target_page, SlottedPage& page, const char* filename);
