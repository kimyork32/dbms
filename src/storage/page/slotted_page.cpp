#include <cstring>

#include "storage/page/slotted_page.hpp"

namespace megatron {

PageHeader* SlottedPage::GetHeader() {
    return reinterpret_cast<PageHeader*>(data);
}

Slot* SlottedPage::GetSlot(uint16_t slot_index) {
    return reinterpret_cast<Slot*>(data + sizeof(PageHeader) + (slot_index * sizeof(Slot)));
}

void SlottedPage::Init(uint32_t page_id) {
    PageHeader* header = GetHeader();
    header->page_id = page_id;
    header->num_slots = 0;
    header->free_lower = sizeof(PageHeader);
    header->free_upper = 4096;
    for(int i = 0; i < 6; ++i) header->flags[i] = 0;
}

bool SlottedPage::InsertTuple(const char* tuple_data, uint16_t size) {
    PageHeader* header = GetHeader();
    
    // check if there is space (tuple size + 4 bytes of the slot)
    if (header->free_upper - header->free_lower < size + uint16_t(sizeof(Slot))) {
        return false; // insufficient memory
    }

    // calculate new offset (upwards)
    uint16_t new_offset = header->free_upper - size;

    // copy the tuple data to the physical area
    std::memcpy(data + new_offset, tuple_data, size);

    // create the entry in the slot directory
    Slot* new_slot = GetSlot(header->num_slots);
    new_slot->offset = new_offset;
    new_slot->length = size;

    // update header pointers
    header->free_upper = new_offset;
    header->free_lower += sizeof(Slot);
    header->num_slots++;

    return true;
}

const char* SlottedPage::ReadTuple(uint16_t slot_id, uint16_t& out_size) {
    PageHeader* header = GetHeader();
    if (slot_id >= header->num_slots) return nullptr;

    Slot* slot = GetSlot(slot_id);
    if (slot->length == 0) return nullptr; // tuple already deleted

    out_size = slot->length;
    return data + slot->offset;
}

void SlottedPage::Compact() {
    char temp_data[4096];
    uint16_t current_upper = 4096;
    PageHeader* header = GetHeader();

    // move all live tuples to the bottom of the temporary buffer
    for (uint16_t i = 0; i < header->num_slots; ++i) {
        Slot* slot = GetSlot(i);
        if (slot->length > 0) { // if the tuple is NOT deleted
            current_upper -= slot->length;
            std::memcpy(temp_data + current_upper, data + slot->offset, slot->length);
            
            // update the offset to the new physical location
            slot->offset = current_upper;
        }
    }

    // copy the compacted block back to the real page
    uint16_t bytes_to_copy = 4096 - current_upper;
    if (bytes_to_copy > 0) {
        std::memcpy(data + current_upper, temp_data + current_upper, bytes_to_copy);
    }

    // update the upper limit of the free space
    header->free_upper = current_upper;
}

void SlottedPage::DeleteTuple(uint16_t slot_id) {
    PageHeader* header = GetHeader();
    if (slot_id >= header->num_slots) return;

    Slot* slot = GetSlot(slot_id);
    if (slot->length == 0) return; // already deleted
    
    // logical marking
    slot->length = 0; 
    
    // compact fragmented space
    Compact();
}

bool SlottedPage::UpdateTuple(uint16_t slot_id, const char* new_data, uint16_t new_size) {
    PageHeader* header = GetHeader();
    if (slot_id >= header->num_slots) return false;

    Slot* slot = GetSlot(slot_id);
    if (slot->length == 0) return false; // cannot update a deleted tuple

    // case 1: in-place update (the new tuple fits in the current space)
    if (new_size <= slot->length) {
        std::memcpy(data + slot->offset, new_data, new_size);
        slot->length = new_size; // update the size in case it decreased
        return true;
    }

    // case 2: out-of-place update (the new tuple is larger)
    // check if there is space, if not, force compaction
    if (header->free_upper - header->free_lower < new_size) {
        Compact();
        // if there is still no space after compacting, fail
        if (header->free_upper - header->free_lower < new_size) {
            return false; 
        }
    }

    // calculate the new position in the free space
    uint16_t new_offset = header->free_upper - new_size;
    
    // copy the new data
    std::memcpy(data + new_offset, new_data, new_size);
    
    // update the existing slot (keeps its id but changes the pointer)
    slot->offset = new_offset;
    slot->length = new_size;
    
    // update the free space
    header->free_upper = new_offset;
    
    return true; 
}

} // namespace megatron
