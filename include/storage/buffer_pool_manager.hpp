#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>
#include "storage/slotted_page.hpp"

namespace megatron {

/**
 * @brief custom hash function for pair<string, uint32_t>
 */
struct BPM_PageKeyHash {
    std::size_t operator()(const std::pair<std::string, uint32_t>& p) const {
        return std::hash<std::string>{}(p.first) ^ (std::hash<uint32_t>{}(p.second) << 1);
    }
};

/**
 * @brief manages a global buffer pool for caching pages in memory
 */
class GlobalBufferPoolManager {
public:
    /**
     * @brief constructs a global buffer pool manager
     * @param pool_size maximum number of frames in the pool
     */
    GlobalBufferPoolManager(size_t pool_size);

    /**
     * @brief destructs the global buffer pool manager and flushes pages
     */
    ~GlobalBufferPoolManager();

    /**
     * @brief fetches a page from the buffer pool or disk
     * @param table_name name of the table
     * @param page_id identifier of the page
     * @return pointer to the slotted page, or nullptr if fetch fails
     */
    SlottedPage* FetchPage(const std::string& table_name, uint32_t page_id);

    /**
     * @brief creates a new page in the buffer pool
     * @param table_name name of the table
     * @param page_id pointer to store the newly created page identifier
     * @return pointer to the new slotted page, or nullptr if pool is full
     */
    SlottedPage* NewPage(const std::string& table_name, uint32_t* page_id);

    /**
     * @brief unpins a page in the buffer pool
     * @param table_name name of the table
     * @param page_id identifier of the page
     * @param is_dirty true if the page was modified
     * @return true if successful
     */
    bool UnpinPage(const std::string& table_name, uint32_t page_id, bool is_dirty);

    /**
     * @brief flushes all valid and dirty pages to disk
     */
    void FlushAllPages();

    /**
     * @brief retrieves the total number of pages for a table
     * @param table_name name of the table
     * @return total number of pages
     */
    uint32_t GetNumPages(const std::string& table_name);

private:
    struct Frame {
        SlottedPage page;
        std::string table_name;
        uint32_t page_id = -1;
        int pin_count = 0;
        bool is_dirty = false;
        bool ref_bit = false;
        bool is_valid = false;
    };

    size_t pool_size_;
    std::vector<Frame> frames_;
    std::unordered_map<std::pair<std::string, uint32_t>, size_t, BPM_PageKeyHash> page_table_;
    size_t clock_hand_ = 0;
    std::mutex latch_;

    bool FindVictim(size_t* frame_id);
    void ReadPageFromDisk(const std::string& table_name, uint32_t page_id, SlottedPage& page);
    void WritePageToDisk(const std::string& table_name, uint32_t page_id, const SlottedPage& page);
};

} // namespace megatron
