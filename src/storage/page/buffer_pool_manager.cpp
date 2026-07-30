#include "storage/page/buffer_pool_manager.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdexcept>

#include <unordered_map>

namespace megatron {

std::unordered_map<std::string, uint32_t> table_num_pages_;
std::mutex table_meta_latch_;

GlobalBufferPoolManager::GlobalBufferPoolManager(size_t pool_size, ReplacementPolicy policy)
    : pool_size_(pool_size), policy_(policy) {
    frames_.resize(pool_size_);
}

GlobalBufferPoolManager::~GlobalBufferPoolManager() {
    FlushAllPages();
}

uint32_t GlobalBufferPoolManager::GetNumPages(const std::string& table_name) {
    std::lock_guard<std::mutex> lock(latch_);
    std::lock_guard<std::mutex> meta_lock(table_meta_latch_);
    std::string filename = table_name + ".bd";
    struct stat st;
    int stat_res = stat(filename.c_str(), &st);
    bool in_mem = false;
    for (const auto& pair : page_table_) {
        if (pair.first.first == table_name) {
            in_mem = true;
            break;
        }
    }
    if ((stat_res != 0 || st.st_size == 0) && !in_mem) {
        table_num_pages_[table_name] = 0;
        return 0;
    }
    if (stat_res == 0) {
        uint32_t np = st.st_size / 4096;
        if (table_num_pages_.find(table_name) != table_num_pages_.end()) {
            np = std::max(np, table_num_pages_[table_name]);
        }
        table_num_pages_[table_name] = np;
        return np;
    } else {
        return table_num_pages_[table_name];
    }
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
    disk_writes_++;
    std::string filename = table_name + ".bd";
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd == -1) throw std::runtime_error("failed to open file for writing: " + filename);
    
    if (pwrite(fd, page.data, 4096, page_id * 4096) != 4096) {
        close(fd);
        throw std::runtime_error("failed to write page to disk");
    }
    close(fd);
}

// ---------------------------------------------------------------------------
// FindVictim: dispatches to the active replacement policy
// ---------------------------------------------------------------------------
bool GlobalBufferPoolManager::FindVictim(size_t* frame_id) {
    switch (policy_) {
        case ReplacementPolicy::CLOCK_SWEEP:    return FindVictimClockSweep(frame_id);
        case ReplacementPolicy::TWO_Q:          return FindVictimTwoQ(frame_id);
        case ReplacementPolicy::OPERATOR_AWARE: return FindVictimOperatorAware(frame_id);
    }
    return FindVictimOperatorAware(frame_id);
}

// ---------------------------------------------------------------------------
// CLOCK_SWEEP: classic 2-pass clock ignoring buffer hints (LRU baseline)
// ---------------------------------------------------------------------------
bool GlobalBufferPoolManager::FindVictimClockSweep(size_t* frame_id) {
    // Pass 0: free frames
    for (size_t i = 0; i < pool_size_; ++i) {
        size_t idx = (clock_hand_ + i) % pool_size_;
        if (!frames_[idx].is_valid) {
            *frame_id = idx;
            clock_hand_ = (idx + 1) % pool_size_;
            return true;
        }
    }
    // 2-pass clock over all unpinned frames
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < pool_size_; ++i) {
            size_t idx = (clock_hand_ + i) % pool_size_;
            if (frames_[idx].pin_count == 0) {
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

// ---------------------------------------------------------------------------
// TWO_Q: Johnson & Shasha (VLDB 1994)
//   A1 (probation, FIFO) - pages accessed only once
//   Am (protected, Clock) - pages accessed more than once
// Victim selection: A1 first (FIFO-scan, no ref_bit check),
//                  then Am (2-pass clock).
// Promotion: when a page already in A1 is accessed again -> moved to Am.
// ---------------------------------------------------------------------------
bool GlobalBufferPoolManager::FindVictimTwoQ(size_t* frame_id) {
    // Phase 0: invalid/empty frames
    for (size_t i = 0; i < pool_size_; ++i) {
        size_t idx = (clock_hand_ + i) % pool_size_;
        if (!frames_[idx].is_valid) {
            *frame_id = idx;
            clock_hand_ = (idx + 1) % pool_size_;
            return true;
        }
    }
    // Phase 1: evict from A1 (probation), FIFO – first unpinned A1 frame
    for (size_t i = 0; i < pool_size_; ++i) {
        size_t idx = (clock_hand_ + i) % pool_size_;
        if (frames_[idx].pin_count == 0 && !frames_[idx].in_am) {
            *frame_id = idx;
            clock_hand_ = (idx + 1) % pool_size_;
            return true;
        }
    }
    // Phase 2: evict from Am (protected), 2-pass clock
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < pool_size_; ++i) {
            size_t idx = (clock_hand_ + i) % pool_size_;
            if (frames_[idx].pin_count == 0 && frames_[idx].in_am) {
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

// ---------------------------------------------------------------------------
// OPERATOR_AWARE: 4-tier hint-driven Clock-Sweep (this work)
// Eviction order: invalid -> DISCARD_QUICKLY -> DEFAULT -> KEEP_HOT
// ---------------------------------------------------------------------------
bool GlobalBufferPoolManager::FindVictimOperatorAware(size_t* frame_id) {
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
        page_hits_++;
        size_t frame_id = page_table_[key];
        frames_[frame_id].pin_count++;
        frames_[frame_id].ref_bit = true;
        // TWO_Q: promote A1 -> Am on second access
        if (policy_ == ReplacementPolicy::TWO_Q && !frames_[frame_id].in_am) {
            frames_[frame_id].in_am = true;
        }
        // OPERATOR_AWARE: update hint only when a stronger one is supplied
        if (policy_ == ReplacementPolicy::OPERATOR_AWARE && hint != BufferHint::DEFAULT) {
            frames_[frame_id].hint = hint;
        }
        return &frames_[frame_id].page;
    }

    page_misses_++;
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
    victim.in_am = false;  // all new pages enter A1 in TWO_Q
    // CLOCK_SWEEP / TWO_Q ignore hints; OPERATOR_AWARE uses them
    victim.hint = (policy_ == ReplacementPolicy::OPERATOR_AWARE) ? hint : BufferHint::DEFAULT;
    
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
    victim.in_am = false;  // new pages always start in A1 for TWO_Q

    // determine new page id
    uint32_t new_page_id = 0;
    {
        std::lock_guard<std::mutex> meta_lock(table_meta_latch_);
        std::string filename = table_name + ".bd";
        struct stat st;
        int stat_res = stat(filename.c_str(), &st);
        bool has_in_mem_frames = false;
        for (const auto& pair : page_table_) {
            if (pair.first.first == table_name) {
                has_in_mem_frames = true;
                break;
            }
        }
        if ((stat_res != 0 || st.st_size == 0) && !has_in_mem_frames) {
            table_num_pages_[table_name] = 0;
        }

        if (table_num_pages_.find(table_name) != table_num_pages_.end()) {
            new_page_id = table_num_pages_[table_name]++;
        } else {
            if (stat_res == 0) {
                table_num_pages_[table_name] = static_cast<uint32_t>(st.st_size / 4096);
            } else {
                table_num_pages_[table_name] = 0;
            }
            new_page_id = table_num_pages_[table_name]++;
        }
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
    victim.hint = (policy_ == ReplacementPolicy::OPERATOR_AWARE) ? hint : BufferHint::DEFAULT;
    
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

void GlobalBufferPoolManager::ResetMetrics() {
    page_hits_.store(0);
    page_misses_.store(0);
    disk_writes_.store(0);
}

void GlobalBufferPoolManager::ClearTablePages(const std::string& table_name) {
    std::lock_guard<std::mutex> lock(latch_);
    {
        std::lock_guard<std::mutex> meta_lock(table_meta_latch_);
        table_num_pages_.erase(table_name);
    }
    for (auto it = page_table_.begin(); it != page_table_.end(); ) {
        if (it->first.first == table_name) {
            size_t frame_id = it->second;
            frames_[frame_id].is_valid = false;
            frames_[frame_id].is_dirty = false;
            frames_[frame_id].pin_count = 0;
            frames_[frame_id].ref_bit = false;
            frames_[frame_id].table_name.clear();
            frames_[frame_id].page_id = static_cast<uint32_t>(-1);
            it = page_table_.erase(it);
        } else {
            ++it;
        }
    }
}

void GlobalBufferPoolManager::ResetGlobalState() {
    std::lock_guard<std::mutex> meta_lock(table_meta_latch_);
    table_num_pages_.clear();
}

size_t GlobalBufferPoolManager::GetPageHits() const {
    return page_hits_.load();
}

size_t GlobalBufferPoolManager::GetPageMisses() const {
    return page_misses_.load();
}

size_t GlobalBufferPoolManager::GetDiskWrites() const {
    return disk_writes_.load();
}

size_t GlobalBufferPoolManager::GetDiskIOCount() const {
    return page_misses_.load() + disk_writes_.load();
}

double GlobalBufferPoolManager::GetMissRatio() const {
    size_t hits = page_hits_.load();
    size_t misses = page_misses_.load();
    size_t total = hits + misses;
    if (total == 0) {
        return 0.0;
    }
    return static_cast<double>(misses) / static_cast<double>(total);
}

} // namespace megatron
