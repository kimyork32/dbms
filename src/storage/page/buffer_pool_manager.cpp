#include "storage/page/buffer_pool_manager.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdexcept>

#include <unordered_map>

namespace megatron {

std::unordered_map<std::string, uint32_t> table_num_pages_;
std::mutex table_meta_latch_;

GlobalBufferPoolManager::GlobalBufferPoolManager(size_t pool_size) : pool_size_(pool_size) {
    frames_.resize(pool_size_);
}

GlobalBufferPoolManager::~GlobalBufferPoolManager() {
    FlushAllPages();
}

uint32_t GlobalBufferPoolManager::GetNumPages(const std::string& table_name) {
    std::lock_guard<std::mutex> lock(table_meta_latch_);
    if (table_num_pages_.find(table_name) != table_num_pages_.end()) {
        return table_num_pages_[table_name];
    }
    
    std::string filename = table_name + ".bd";
    struct stat st;
    if (stat(filename.c_str(), &st) == 0) {
        uint32_t np = st.st_size / 4096;
        table_num_pages_[table_name] = np;
        return np;
    }
    return 0;
}

void GlobalBufferPoolManager::ReadPageFromDisk(const std::string& table_name, uint32_t page_id, SlottedPage& page) {
    std::string filename = table_name + ".bd";
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) throw std::runtime_error("failed to open file for reading: " + filename);
    
    if (pread(fd, page.data, 4096, page_id * 4096) != 4096) {
        close(fd);
        throw std::runtime_error("failed to read page from disk");
    }
    close(fd);
}

void GlobalBufferPoolManager::WritePageToDisk(const std::string& table_name, uint32_t page_id, const SlottedPage& page) {
    std::string filename = table_name + ".bd";
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd == -1) throw std::runtime_error("failed to open file for writing: " + filename);
    
    if (pwrite(fd, page.data, 4096, page_id * 4096) != 4096) {
        close(fd);
        throw std::runtime_error("failed to write page to disk");
    }
    close(fd);
}

bool GlobalBufferPoolManager::FindVictim(size_t* frame_id) {
    // Tier 0: Invalid / empty frames (!is_valid)
    for (size_t i = 0; i < pool_size_; ++i) {
        size_t idx = (clock_hand_ + i) % pool_size_;
        if (!frames_[idx].is_valid) {
            *frame_id = idx;
            clock_hand_ = (idx + 1) % pool_size_;
            return true;
        }
    }

    // Tier 1: Unpinned DISCARD_QUICKLY frames
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < pool_size_; ++i) {
            size_t idx = (clock_hand_ + i) % pool_size_;
            if (frames_[idx].pin_count == 0 && frames_[idx].hint == BufferHint::DISCARD_QUICKLY) {
                if (frames_[idx].ref_bit) {
                    frames_[idx].ref_bit = false;
                } else {
                    *frame_id = idx;
                    clock_hand_ = (idx + 1) % pool_size_;
                    return true;
                }
            }
        }
    }

    // Tier 2: Unpinned DEFAULT frames via 2-pass Clock algorithm
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < pool_size_; ++i) {
            size_t idx = (clock_hand_ + i) % pool_size_;
            if (frames_[idx].pin_count == 0 && frames_[idx].hint == BufferHint::DEFAULT) {
                if (frames_[idx].ref_bit) {
                    frames_[idx].ref_bit = false;
                } else {
                    *frame_id = idx;
                    clock_hand_ = (idx + 1) % pool_size_;
                    return true;
                }
            }
        }
    }

    // Tier 3: Unpinned KEEP_HOT frames via 2-pass Clock algorithm as last resort
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < pool_size_; ++i) {
            size_t idx = (clock_hand_ + i) % pool_size_;
            if (frames_[idx].pin_count == 0 && frames_[idx].hint == BufferHint::KEEP_HOT) {
                if (frames_[idx].ref_bit) {
                    frames_[idx].ref_bit = false;
                } else {
                    *frame_id = idx;
                    clock_hand_ = (idx + 1) % pool_size_;
                    return true;
                }
            }
        }
    }

    return false;
}

SlottedPage* GlobalBufferPoolManager::FetchPage(const std::string& table_name, uint32_t page_id, BufferHint hint) {
    std::lock_guard<std::mutex> lock(latch_);
    auto key = std::make_pair(table_name, page_id);
    
    if (page_table_.find(key) != page_table_.end()) {
        size_t frame_id = page_table_[key];
        frames_[frame_id].pin_count++;
        frames_[frame_id].ref_bit = true;
        if (hint != BufferHint::DEFAULT) {
            frames_[frame_id].hint = hint;
        }
        return &frames_[frame_id].page;
    }

    size_t victim_frame;
    if (!FindVictim(&victim_frame)) {
        return nullptr; // all pinned
    }

    auto& victim = frames_[victim_frame];

    if (victim.is_valid && victim.is_dirty) {
        WritePageToDisk(victim.table_name, victim.page_id, victim.page);
    }

    if (victim.is_valid) {
        page_table_.erase({victim.table_name, victim.page_id});
    }

    ReadPageFromDisk(table_name, page_id, victim.page);
    
    victim.table_name = table_name;
    victim.page_id = page_id;
    victim.pin_count = 1;
    victim.is_dirty = false;
    victim.ref_bit = true;
    victim.is_valid = true;
    victim.hint = hint;
    
    page_table_[key] = victim_frame;

    return &victim.page;
}

SlottedPage* GlobalBufferPoolManager::NewPage(const std::string& table_name, uint32_t* page_id, BufferHint hint) {
    std::lock_guard<std::mutex> lock(latch_);
    
    size_t victim_frame;
    if (!FindVictim(&victim_frame)) {
        return nullptr;
    }

    auto& victim = frames_[victim_frame];

    if (victim.is_valid && victim.is_dirty) {
        WritePageToDisk(victim.table_name, victim.page_id, victim.page);
    }

    if (victim.is_valid) {
        page_table_.erase({victim.table_name, victim.page_id});
    }

    // determine new page id
    uint32_t new_page_id = 0;
    {
        std::lock_guard<std::mutex> meta_lock(table_meta_latch_);
        if (table_num_pages_.find(table_name) != table_num_pages_.end()) {
            new_page_id = table_num_pages_[table_name];
        } else {
            std::string filename = table_name + ".bd";
            struct stat st;
            if (stat(filename.c_str(), &st) == 0) {
                new_page_id = st.st_size / 4096;
            }
        }
        table_num_pages_[table_name] = new_page_id + 1;
    }
    
    *page_id = new_page_id;

    // initialize formatting
    victim.page.Init(new_page_id);
    
    victim.table_name = table_name;
    victim.page_id = new_page_id;
    victim.pin_count = 1;
    victim.is_dirty = true; // newly created means needs flushing
    victim.ref_bit = true;
    victim.is_valid = true;
    victim.hint = hint;
    
    page_table_[{table_name, new_page_id}] = victim_frame;

    return &victim.page;
}

bool GlobalBufferPoolManager::UnpinPage(const std::string& table_name, uint32_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);
    auto key = std::make_pair(table_name, page_id);
    if (page_table_.find(key) == page_table_.end()) return false;
    
    size_t frame_id = page_table_[key];
    if (frames_[frame_id].pin_count <= 0) return false;
    
    frames_[frame_id].pin_count--;
    if (is_dirty) frames_[frame_id].is_dirty = true;
    
    return true;
}

void GlobalBufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (size_t i = 0; i < pool_size_; ++i) {
        if (frames_[i].is_valid && frames_[i].is_dirty) {
            WritePageToDisk(frames_[i].table_name, frames_[i].page_id, frames_[i].page);
            frames_[i].is_dirty = false;
        }
    }
}

} // namespace megatron
