#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>
#include <atomic>
#include "storage/page/slotted_page.hpp"

namespace megatron {

/**
 * @brief Buffer access hint supplied by physical plan nodes and access methods
 */
enum class BufferHint {
    DEFAULT = 0,
    KEEP_HOT,
    DISCARD_QUICKLY
};

/**
 * @brief Page replacement policy for the buffer pool manager
 *
 * CLOCK_SWEEP   – Classic Clock-Sweep independent of operator hints (LRU baseline).
 * TWO_Q         – Two-queue algorithm (Johnson & Shasha, VLDB 1994):
 *                 A1 probation queue (FIFO) + Am protected queue (Clock).
 * OPERATOR_AWARE– Hint-driven 4-tier Clock-Sweep (this work):
 *                 eviction order: DISCARD_QUICKLY → DEFAULT → KEEP_HOT.
 */
enum class ReplacementPolicy {
    CLOCK_SWEEP = 0,
    TWO_Q,
    OPERATOR_AWARE
};

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
    /**
     * @param pool_size  maximum number of frames in the pool
     * @param policy     replacement policy (default = OPERATOR_AWARE)
     */
    GlobalBufferPoolManager(size_t pool_size,
                            ReplacementPolicy policy = ReplacementPolicy::OPERATOR_AWARE);

    /**
     * @brief destructs the global buffer pool manager and flushes pages
     */
    ~GlobalBufferPoolManager();

    /**
     * @brief fetches a page from the buffer pool or disk
     * @param table_name name of the table
     * @param page_id identifier of the page
     * @param hint buffer retention hint
     * @return pointer to the slotted page, or nullptr if fetch fails
     */
    SlottedPage* FetchPage(const std::string& table_name, uint32_t page_id, BufferHint hint = BufferHint::DEFAULT);

    /**
     * @brief creates a new page in the buffer pool
     * @param table_name name of the table
     * @param page_id pointer to store the newly created page identifier
     * @param hint buffer retention hint
     * @return pointer to the new slotted page, or nullptr if pool is full
     */
    SlottedPage* NewPage(const std::string& table_name, uint32_t* page_id, BufferHint hint = BufferHint::DEFAULT);

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

    /**
     * @brief resets metrics counters (page_hits, page_misses, disk_writes) to 0
     */
    void ResetMetrics();

    /**
     * @brief clears page metadata and frame mappings for a table
     * @param table_name name of the table
     */
    void ClearTablePages(const std::string& table_name);

    /**
     * @brief clears all global page allocation metadata
     */
    static void ResetGlobalState();

    /**
     * @brief returns total page hits
     */
    size_t GetPageHits() const;

    /**
     * @brief returns total page misses
     */
    size_t GetPageMisses() const;

    /**
     * @brief returns total disk writes
     */
    size_t GetDiskWrites() const;

    /**
     * @brief returns total disk I/O count (page_misses + disk_writes)
     */
    size_t GetDiskIOCount() const;

    /**
     * @brief returns miss ratio: page_misses / (page_hits + page_misses)
     */
    double GetMissRatio() const;

private:
    struct Frame {
        SlottedPage page;
        std::string table_name;
        uint32_t page_id = -1;
        int pin_count = 0;
        bool is_dirty = false;
        bool ref_bit = false;
        bool is_valid = false;
        BufferHint hint = BufferHint::DEFAULT;
        /// For TWO_Q: true = Am (protected, clock-evicted), false = A1 (probation, FIFO-evicted)
        bool in_am = false;
    };

    size_t pool_size_;
    ReplacementPolicy policy_;
    std::vector<Frame> frames_;
    std::unordered_map<std::pair<std::string, uint32_t>, size_t, BPM_PageKeyHash> page_table_;
    size_t clock_hand_ = 0;
    std::mutex latch_;

    std::atomic<size_t> page_hits_{0};
    std::atomic<size_t> page_misses_{0};
    std::atomic<size_t> disk_writes_{0};

    /// Dispatch to policy-specific victim finder
    bool FindVictim(size_t* frame_id);
    /// Classic 2-pass Clock-Sweep ignoring hints
    bool FindVictimClockSweep(size_t* frame_id);
    /// 2Q: evict from A1 (FIFO) first, then Am (Clock)
    bool FindVictimTwoQ(size_t* frame_id);
    /// Operator-Aware 4-tier: DISCARD_QUICKLY → DEFAULT → KEEP_HOT
    bool FindVictimOperatorAware(size_t* frame_id);
    void ReadPageFromDisk(const std::string& table_name, uint32_t page_id, SlottedPage& page);
    void WritePageToDisk(const std::string& table_name, uint32_t page_id, const SlottedPage& page);
};

} // namespace megatron
