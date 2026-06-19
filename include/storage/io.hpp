#pragma once

#include "slotted_page.hpp"

namespace megatron {

/**
 * @brief writes a page to disk
 * @param target_page page number
 * @param page page data
 * @param filename target file
 */
void WritePage(uint32_t target_page, SlottedPage& page, const char* filename);

/**
 * @brief reads a page from disk
 * @param target_page page number
 * @param page page object to fill
 * @param filename source file
 */
void ReadPage(uint32_t target_page, SlottedPage& page, const char* filename);

} // namespace megatron
