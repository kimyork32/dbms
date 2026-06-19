#pragma once

#include <cstdint>
#include "PageHeader.hpp"
 
/**
 * manages tuple storage within a fixed-size disk page
 */
class SlottedPage {
public:
    char data[PAGE_SIZE];

    /**
     * @brief retrieves the page header
     * @return pointer to header
     */
    PageHeader* get_header();

    /**
     * @brief retrieves a specific slot
     * @param slot_index index of the slot
     * @return pointer to slot
     */
    Slot* get_slot(uint16_t slot_index);

    /**
     * @brief initializes the page
     * @param page_id unique page identifier
     */
    void init(uint32_t page_id);

    /**
     * @brief inserts a tuple into the page
     * @param tuple_data raw tuple data
     * @param size size of the data
     * @return true if insertion succeeded
     */
    bool insert_tuple(const char* tuple_data, uint16_t size);

    /**
     * @brief reads a tuple from a slot
     * @param slot_id index of the slot
     * @param out_size output parameter for tuple size
     * @return pointer to tuple data
     */
    const char* read_tuple(uint16_t slot_id, uint16_t& out_size);

    /**
     * @brief compacts the page to reclaim space
     */
    void compact();

    /**
     * @brief deletes a tuple from the page
     * @param slot_id index of the slot
     */
    void delete_tuple(uint16_t slot_id);

    /**
     * @brief updates an existing tuple
     * @param slot_id index of the slot
     * @param new_data new tuple data
     * @param new_size size of new data
     * @return true if update succeeded
     */
    bool update_tuple(uint16_t slot_id, const char* new_data, uint16_t new_size);
};
