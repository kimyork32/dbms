#pragma once

#include <cstdint>

namespace megatron {

constexpr size_t PAGE_SIZE = 4096;

#pragma pack(push, 1)
/**
 * represents a single entry in the page slot directory
 */
struct Slot {
    uint16_t offset;
    uint16_t length;
};

/**
 * contains metadata for a slotted page
 */
struct PageHeader {
    uint32_t page_id;
    uint16_t num_slots;
    uint16_t free_lower; // grows upwards (begin in 16)
    uint16_t free_upper; // grows downwards (begin in 4096)
    uint8_t  flags[6];
};
#pragma pack(pop)

} // namespace megatron
