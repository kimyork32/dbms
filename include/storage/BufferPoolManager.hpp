#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>
#include "storage/SlottedPage.hpp"

namespace megatron {

struct BPM_PageKeyHash {
    std::size_t operator()(const std::pair<std::string, uint32_t>& p) const {
        return std::hash<std::string>{}(p.first) ^ (std::hash<uint32_t>{}(p.second) << 1);
    }
};

class GlobalBufferPoolManager {
public:
    GlobalBufferPoolManager(size_t pool_size);
    ~GlobalBufferPoolManager();

    SlottedPage* FetchPage(const std::string& table_name, uint32_t page_id);
    SlottedPage* NewPage(const std::string& table_name, uint32_t* page_id);
    bool UnpinPage(const std::string& table_name, uint32_t page_id, bool is_dirty);
    void FlushAllPages();
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
