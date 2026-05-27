#pragma once

#include <cstdint>

/**
 * Utility namespace to pack and unpack Page ID and Slot ID
 * into a single 64-bit integer payload for the B+Tree.
 */
namespace RecordId {

    /**
     * @brief Packs a Page ID and a Slot ID into a 64-bit integer.
     * @param page_id 32-bit Page ID.
     * @param slot_id 16-bit Slot ID.
     * @return 64-bit packed Record ID.
     */
    inline int64_t make_rid(uint32_t page_id, uint16_t slot_id) {
        return (static_cast<int64_t>(page_id) << 16) | slot_id;
    }

    /**
     * @brief Extracts the Page ID from a packed 64-bit Record ID.
     * @param rid 64-bit packed Record ID.
     * @return 32-bit Page ID.
     */
    inline uint32_t get_page_id(int64_t rid) {
        return static_cast<uint32_t>(rid >> 16);
    }

    /**
     * @brief Extracts the Slot ID from a packed 64-bit Record ID.
     * @param rid 64-bit packed Record ID.
     * @return 16-bit Slot ID.
     */
    inline uint16_t get_slot_id(int64_t rid) {
        return static_cast<uint16_t>(rid & 0xFFFF);
    }

} // namespace RecordId
