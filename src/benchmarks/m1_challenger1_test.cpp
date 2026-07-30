#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>

#include "storage/page/buffer_pool_manager.hpp"
#include "storage/engine/disk_storage_engine.hpp"

using namespace megatron;

static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_EQ(val1, val2, msg) \
    do { \
        if ((val1) == (val2)) { \
            g_passed++; \
        } else { \
            g_failed++; \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg \
                      << " (Expected: " << (val2) \
                      << ", Actual: " << (val1) << ")" << std::endl; \
        } \
    } while (0)

#define ASSERT_DOUBLE_EQ(val1, val2, msg) \
    do { \
        if (std::abs((val1) - (val2)) < 1e-9) { \
            g_passed++; \
        } else { \
            g_failed++; \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg \
                      << " (Expected: " << (val2) \
                      << ", Actual: " << (val1) << ")" << std::endl; \
        } \
    } while (0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (cond) { \
            g_passed++; \
        } else { \
            g_failed++; \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
        } \
    } while (0)

void CleanUpFiles(const std::vector<std::string>& tables) {
    for (const auto& t : tables) {
        std::remove((t + ".bd").c_str());
        std::remove((t + "_index.db").c_str());
    }
}

// Requirement 1 & 2: Initial state & zero-fetch ratio handling
void TestInitialStateAndZeroFetchRatio() {
    std::cout << "--- 1. Testing Initial State & Zero-Fetch Ratio Handling ---" << std::endl;
    GlobalBufferPoolManager bpm(5);

    ASSERT_EQ(bpm.GetPageHits(), 0, "Initial page hits must be 0");
    ASSERT_EQ(bpm.GetPageMisses(), 0, "Initial page misses must be 0");
    ASSERT_EQ(bpm.GetDiskWrites(), 0, "Initial disk writes must be 0");
    ASSERT_EQ(bpm.GetDiskIOCount(), 0, "Initial disk I/O count must be 0");
    
    double miss_ratio = bpm.GetMissRatio();
    ASSERT_TRUE(!std::isnan(miss_ratio), "Initial miss ratio must not be NaN");
    ASSERT_TRUE(!std::isinf(miss_ratio), "Initial miss ratio must not be Inf");
    ASSERT_DOUBLE_EQ(miss_ratio, 0.0, "Initial miss ratio must be 0.0 when zero fetches");

    bpm.ResetMetrics();
    ASSERT_EQ(bpm.GetPageHits(), 0, "Hits after ResetMetrics must be 0");
    ASSERT_EQ(bpm.GetPageMisses(), 0, "Misses after ResetMetrics must be 0");
    ASSERT_EQ(bpm.GetDiskWrites(), 0, "Disk writes after ResetMetrics must be 0");
    ASSERT_DOUBLE_EQ(bpm.GetMissRatio(), 0.0, "Miss ratio after ResetMetrics must be 0.0");
}

// Requirement 3: Page fetches increment page_hits_ on hit and page_misses_ on miss
void TestPageFetchMetricsTracking() {
    std::cout << "--- 2. Testing Page Fetch Hit/Miss Tracking ---" << std::endl;
    CleanUpFiles({"test_fetch_tbl"});

    // Create a BPM with pool size 2
    GlobalBufferPoolManager bpm(2);
    uint32_t pid0, pid1, pid2;

    // Create 2 pages
    SlottedPage* p0 = bpm.NewPage("test_fetch_tbl", &pid0);
    ASSERT_TRUE(p0 != nullptr, "Created page 0");
    bpm.UnpinPage("test_fetch_tbl", pid0, true);

    SlottedPage* p1 = bpm.NewPage("test_fetch_tbl", &pid1);
    ASSERT_TRUE(p1 != nullptr, "Created page 1");
    bpm.UnpinPage("test_fetch_tbl", pid1, true);

    // Initial fetch counters after NewPage (NewPage does not increment hit/miss)
    ASSERT_EQ(bpm.GetPageHits(), 0, "Hits 0 before fetches");
    ASSERT_EQ(bpm.GetPageMisses(), 0, "Misses 0 before fetches");

    // Fetch page 0 (it is currently in buffer pool frame 0) -> HIT!
    SlottedPage* f0 = bpm.FetchPage("test_fetch_tbl", pid0);
    ASSERT_TRUE(f0 != nullptr, "Fetched page 0 from pool");
    ASSERT_EQ(bpm.GetPageHits(), 1, "Page 0 fetch must increment page_hits_ to 1");
    ASSERT_EQ(bpm.GetPageMisses(), 0, "Page 0 fetch must keep page_misses_ at 0");
    bpm.UnpinPage("test_fetch_tbl", pid0, false);

    // Fetch page 1 (it is currently in buffer pool frame 1) -> HIT!
    SlottedPage* f1 = bpm.FetchPage("test_fetch_tbl", pid1);
    ASSERT_TRUE(f1 != nullptr, "Fetched page 1 from pool");
    ASSERT_EQ(bpm.GetPageHits(), 2, "Page 1 fetch must increment page_hits_ to 2");
    ASSERT_EQ(bpm.GetPageMisses(), 0, "Page 1 fetch must keep page_misses_ at 0");
    bpm.UnpinPage("test_fetch_tbl", pid1, false);

    // Now force an eviction by creating page 2 in a full pool (size 2)
    SlottedPage* p2 = bpm.NewPage("test_fetch_tbl", &pid2);
    ASSERT_TRUE(p2 != nullptr, "Created page 2 causing eviction");
    bpm.UnpinPage("test_fetch_tbl", pid2, true);

    // Page 0 was evicted to make room for Page 2.
    // Now fetch page 0 -> MISS!
    SlottedPage* f0_miss = bpm.FetchPage("test_fetch_tbl", pid0);
    ASSERT_TRUE(f0_miss != nullptr, "Fetched evicted page 0 from disk");
    ASSERT_EQ(bpm.GetPageHits(), 2, "Page 0 miss must keep page_hits_ at 2");
    ASSERT_EQ(bpm.GetPageMisses(), 1, "Evicted page 0 fetch must increment page_misses_ to 1");
    bpm.UnpinPage("test_fetch_tbl", pid0, false);

    // Test miss ratio calculation: 1 miss out of (2 hits + 1 miss) = 1/3 = 0.33333333333...
    double expected_ratio = 1.0 / 3.0;
    ASSERT_DOUBLE_EQ(bpm.GetMissRatio(), expected_ratio, "Miss ratio calculation 1/3");

    CleanUpFiles({"test_fetch_tbl"});
}

// Requirement 4: Dirty frame flushes correctly increment disk_writes_
void TestDirtyFrameFlushesIncrementDiskWrites() {
    std::cout << "--- 3. Testing Dirty Frame Flushes Increment Disk Writes ---" << std::endl;
    CleanUpFiles({"test_flush_tbl"});

    GlobalBufferPoolManager bpm(2);
    uint32_t pid0, pid1, pid2;

    ASSERT_EQ(bpm.GetDiskWrites(), 0, "Initial disk_writes_ must be 0");

    // Create page 0 and page 1, marking them dirty
    SlottedPage* p0 = bpm.NewPage("test_flush_tbl", &pid0);
    (void)p0;
    bpm.UnpinPage("test_flush_tbl", pid0, true);

    SlottedPage* p1 = bpm.NewPage("test_flush_tbl", &pid1);
    (void)p1;
    bpm.UnpinPage("test_flush_tbl", pid1, true);

    // Pool size is 2, no writes yet
    ASSERT_EQ(bpm.GetDiskWrites(), 0, "No disk writes before eviction or explicit flush");

    // Allocate page 2 -> triggers eviction of a dirty frame -> disk_writes_++
    SlottedPage* p2 = bpm.NewPage("test_flush_tbl", &pid2);
    (void)p2;
    bpm.UnpinPage("test_flush_tbl", pid2, true);

    ASSERT_EQ(bpm.GetDiskWrites(), 1, "Evicting dirty frame must increment disk_writes_ to 1");

    // Call FlushAllPages() -> remaining dirty pages are flushed to disk
    bpm.FlushAllPages();

    // After FlushAllPages(), the remaining 2 dirty pages in pool were written to disk
    ASSERT_EQ(bpm.GetDiskWrites(), 3, "FlushAllPages must write remaining 2 dirty frames, bringing disk_writes_ to 3");
    ASSERT_EQ(bpm.GetDiskIOCount(), bpm.GetPageMisses() + 3, "DiskIOCount must equal page_misses + disk_writes");

    CleanUpFiles({"test_flush_tbl"});
}

// Requirement 1 (re-test): ResetMetrics resets hits, misses, disk_writes to 0
void TestResetMetricsFull() {
    std::cout << "--- 4. Testing ResetMetrics ---" << std::endl;
    CleanUpFiles({"test_reset_tbl"});

    GlobalBufferPoolManager bpm(2);
    uint32_t pid0, pid1, pid2;

    bpm.NewPage("test_reset_tbl", &pid0);
    bpm.UnpinPage("test_reset_tbl", pid0, true);
    bpm.NewPage("test_reset_tbl", &pid1);
    bpm.UnpinPage("test_reset_tbl", pid1, true);

    bpm.FetchPage("test_reset_tbl", pid0); // Hit 1
    bpm.UnpinPage("test_reset_tbl", pid0, false);

    bpm.NewPage("test_reset_tbl", &pid2); // Evicts pid0 (dirty) -> disk_writes_ = 1
    bpm.UnpinPage("test_reset_tbl", pid2, true);

    bpm.FetchPage("test_reset_tbl", pid0); // Miss 1
    bpm.UnpinPage("test_reset_tbl", pid0, false);

    ASSERT_EQ(bpm.GetPageHits(), 1, "Hits = 1");
    ASSERT_EQ(bpm.GetPageMisses(), 1, "Misses = 1");
    ASSERT_EQ(bpm.GetDiskWrites(), 2, "DiskWrites = 2 (1 write from NewPage eviction + 1 write from FetchPage eviction)");
    ASSERT_EQ(bpm.GetDiskIOCount(), 3, "DiskIOCount = 3 (1 miss + 2 disk writes)");

    // Now reset metrics
    bpm.ResetMetrics();

    ASSERT_EQ(bpm.GetPageHits(), 0, "Hits reset to 0");
    ASSERT_EQ(bpm.GetPageMisses(), 0, "Misses reset to 0");
    ASSERT_EQ(bpm.GetDiskWrites(), 0, "DiskWrites reset to 0");
    ASSERT_EQ(bpm.GetDiskIOCount(), 0, "DiskIOCount reset to 0");
    ASSERT_DOUBLE_EQ(bpm.GetMissRatio(), 0.0, "MissRatio reset to 0.0");

    // Perform another hit to ensure counter works after reset
    bpm.FetchPage("test_reset_tbl", pid0);
    ASSERT_EQ(bpm.GetPageHits(), 1, "Hits counter works after reset");
    ASSERT_EQ(bpm.GetPageMisses(), 0, "Misses counter works after reset");
    bpm.UnpinPage("test_reset_tbl", pid0, false);

    CleanUpFiles({"test_reset_tbl"});
}

// Requirement 5: DiskStorageEngine(size_t pool_size) properly initializes bpm_ with custom frame pool size
void TestDiskStorageEngineCustomPoolSize() {
    std::cout << "--- 5. Testing DiskStorageEngine Custom Pool Size Initialization ---" << std::endl;
    CleanUpFiles({"tbl_small_pool", "tbl_large_pool"});

    // Create DiskStorageEngine with small pool size = 2
    {
        DiskStorageEngine engine_small(2);
        engine_small.CreateTable("tbl_small_pool", {"id", "val"});
        
        // Insert enough tuples to span 3 pages
        // Each tuple is ~50 bytes, slotted page 4KB holds ~70 tuples.
        // Let's insert 250 tuples to create at least 3 pages.
        for (int i = 0; i < 250; ++i) {
            engine_small.InsertTuple("tbl_small_pool", {std::to_string(i), "test_data_string_" + std::to_string(i)});
        }

        // Check if eviction occurred. With pool_size = 2 and 3 pages created,
        // evicted dirty frames MUST have been written to disk before engine destruction/flush!
        struct stat st;
        int res = stat("tbl_small_pool.bd", &st);
        ASSERT_EQ(res, 0, "Table file must exist on disk");
        ASSERT_TRUE(st.st_size >= 4096, "Eviction in small pool (size 2) must have flushed evicted dirty frames to disk");
    }

    // Create DiskStorageEngine with large pool size = 100
    {
        DiskStorageEngine engine_large(100);
        engine_large.CreateTable("tbl_large_pool", {"id", "val"});
        
        for (int i = 0; i < 250; ++i) {
            engine_large.InsertTuple("tbl_large_pool", {std::to_string(i), "test_data_string_" + std::to_string(i)});
        }
        
        // With pool size 100 and only ~3-4 pages created, all pages fit in memory.
        // Doing FullScan on all 250 tuples should succeed completely.
        auto results = engine_large.FullScan("tbl_large_pool");
        ASSERT_EQ(results.size(), 250, "FullScan must return all 250 inserted tuples");
    }

    CleanUpFiles({"tbl_small_pool", "tbl_large_pool"});
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   M1 Challenger 1 Empirical Verification Test Suite    \n";
    std::cout << "========================================================\n";

    TestInitialStateAndZeroFetchRatio();
    TestPageFetchMetricsTracking();
    TestDirtyFrameFlushesIncrementDiskWrites();
    TestResetMetricsFull();
    TestDiskStorageEngineCustomPoolSize();

    std::cout << "========================================================\n";
    std::cout << " Summary: " << g_passed << " PASSED, " << g_failed << " FAILED\n";
    std::cout << "========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}
