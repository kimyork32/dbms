#include "storage/page/buffer_pool_manager.hpp"
#include <iostream>
#include <cassert>
#include <unistd.h>

namespace megatron {

static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (cond) { \
            g_passed++; \
            std::cout << "  [PASS] " << msg << std::endl; \
        } else { \
            g_failed++; \
            std::cerr << "  [FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
        } \
    } while (0)

#define ASSERT_EQ(val1, val2, msg) \
    do { \
        if ((val1) == (val2)) { \
            g_passed++; \
            std::cout << "  [PASS] " << msg << std::endl; \
        } else { \
            g_failed++; \
            std::cerr << "  [FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
        } \
    } while (0)

void CleanupTestFiles() {
    unlink("test_t1.bd");
    unlink("test_t2.bd");
    unlink("test_t3.bd");
    unlink("test_t4.bd");
    unlink("test_t5.bd");
}

// Test 1: DISCARD_QUICKLY is evicted first under memory pressure
void TestDiscardQuicklyEvictedFirst() {
    std::cout << "--- Test 1: DISCARD_QUICKLY Evicted First ---\n";
    CleanupTestFiles();
    std::string table = "test_t1";
    GlobalBufferPoolManager bpm(3);

    uint32_t p0, p1, p2, p3;
    SlottedPage* ptr0 = bpm.NewPage(table, &p0, BufferHint::KEEP_HOT);
    SlottedPage* ptr1 = bpm.NewPage(table, &p1, BufferHint::DEFAULT);
    SlottedPage* ptr2 = bpm.NewPage(table, &p2, BufferHint::DISCARD_QUICKLY);

    ASSERT_TRUE(ptr0 != nullptr && ptr1 != nullptr && ptr2 != nullptr, "Pool filled with 3 pages");
    ASSERT_TRUE(ptr0 != ptr1 && ptr1 != ptr2 && ptr0 != ptr2, "Distinct frame pointers");

    // Unpin all 3 pages
    bpm.UnpinPage(table, p0, true);
    bpm.UnpinPage(table, p1, true);
    bpm.UnpinPage(table, p2, true);

    // Request 4th page (pool full) -> should evict p2 (DISCARD_QUICKLY)
    SlottedPage* ptr3 = bpm.NewPage(table, &p3, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr3 != nullptr, "Page 3 allocated successfully");
    ASSERT_EQ(ptr3, ptr2, "Frame holding p2 (DISCARD_QUICKLY) was reused for p3");

    // Request 5th page -> p0 is KEEP_HOT, p1 is DEFAULT, p3 is DEFAULT.
    // Should evict p1 or p3 (DEFAULT), NOT p0 (KEEP_HOT).
    uint32_t p4;
    SlottedPage* ptr4 = bpm.NewPage(table, &p4, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr4 != nullptr, "Page 4 allocated successfully");
    ASSERT_TRUE(ptr4 != ptr0, "Page 0 (KEEP_HOT) was NOT evicted when DEFAULT pages existed");
    ASSERT_TRUE(ptr4 == ptr1 || ptr4 == ptr3, "One of DEFAULT frames (p1 or p3) was evicted");
}

// Test 2: KEEP_HOT protection while DEFAULT/DISCARD_QUICKLY exist
void TestKeepHotProtection() {
    std::cout << "--- Test 2: KEEP_HOT Protection ---\n";
    CleanupTestFiles();
    std::string table = "test_t2";
    GlobalBufferPoolManager bpm(3);

    uint32_t p0, p1, p2;
    SlottedPage* ptr0 = bpm.NewPage(table, &p0, BufferHint::KEEP_HOT);
    SlottedPage* ptr1 = bpm.NewPage(table, &p1, BufferHint::DEFAULT);
    SlottedPage* ptr2 = bpm.NewPage(table, &p2, BufferHint::DEFAULT);

    bpm.UnpinPage(table, p0, true);
    bpm.UnpinPage(table, p1, true);
    bpm.UnpinPage(table, p2, true);

    // Evict 1: Page 3 requested -> should evict ptr1 or ptr2, NOT ptr0
    uint32_t p3;
    SlottedPage* ptr3 = bpm.NewPage(table, &p3, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr3 != ptr0, "1st Eviction: Page 0 (KEEP_HOT) was protected");

    bpm.UnpinPage(table, p3, true);

    // Evict 2: Page 4 requested -> should evict remaining DEFAULT frame, NOT ptr0
    uint32_t p4;
    SlottedPage* ptr4 = bpm.NewPage(table, &p4, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr4 != ptr0, "2nd Eviction: Page 0 (KEEP_HOT) was still protected");

    bpm.UnpinPage(table, p4, true);

    // Evict 3: Page 5 requested -> should evict remaining DEFAULT frame, NOT ptr0
    uint32_t p5;
    SlottedPage* ptr5 = bpm.NewPage(table, &p5, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr5 != ptr0, "3rd Eviction: Page 0 (KEEP_HOT) was STILL protected");
}

// Test 3: KEEP_HOT evicted as last resort when only KEEP_HOT pages exist
void TestKeepHotEvictedWhenOnlyKeepHot() {
    std::cout << "--- Test 3: KEEP_HOT Evicted as Last Resort ---\n";
    CleanupTestFiles();
    std::string table = "test_t3";
    GlobalBufferPoolManager bpm(3);

    uint32_t p0, p1, p2;
    SlottedPage* ptr0 = bpm.NewPage(table, &p0, BufferHint::KEEP_HOT);
    SlottedPage* ptr1 = bpm.NewPage(table, &p1, BufferHint::KEEP_HOT);
    SlottedPage* ptr2 = bpm.NewPage(table, &p2, BufferHint::KEEP_HOT);

    bpm.UnpinPage(table, p0, true);
    bpm.UnpinPage(table, p1, true);
    bpm.UnpinPage(table, p2, true);

    // All pages are KEEP_HOT and unpinned. Allocating p3 must succeed by evicting one KEEP_HOT page.
    uint32_t p3;
    SlottedPage* ptr3 = bpm.NewPage(table, &p3, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr3 != nullptr, "Eviction succeeded when only KEEP_HOT pages existed");
    ASSERT_TRUE(ptr3 == ptr0 || ptr3 == ptr1 || ptr3 == ptr2, "One of the KEEP_HOT frames was evicted");
}

// Test 4: Pinned pages are NEVER evicted
void TestPinnedPagesNeverEvicted() {
    std::cout << "--- Test 4: Pinned Pages Never Evicted ---\n";
    CleanupTestFiles();
    std::string table = "test_t4";
    GlobalBufferPoolManager bpm(3);

    // Subtest 4a: All 3 frames pinned
    uint32_t p0, p1, p2;
    SlottedPage* ptr0 = bpm.NewPage(table, &p0, BufferHint::DISCARD_QUICKLY);
    SlottedPage* ptr1 = bpm.NewPage(table, &p1, BufferHint::DEFAULT);
    SlottedPage* ptr2 = bpm.NewPage(table, &p2, BufferHint::KEEP_HOT);

    // Do NOT unpin! All pin_count = 1.
    uint32_t p3;
    SlottedPage* ptr3 = bpm.NewPage(table, &p3, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr3 == nullptr, "NewPage returned nullptr when all frames are pinned");

    // Subtest 4b: Pinned DISCARD_QUICKLY vs Unpinned DEFAULT / KEEP_HOT
    // Unpin p1 (DEFAULT) and p2 (KEEP_HOT), keep p0 (DISCARD_QUICKLY) pinned!
    bpm.UnpinPage(table, p1, true);
    bpm.UnpinPage(table, p2, true);

    // Now request p3 again
    ptr3 = bpm.NewPage(table, &p3, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr3 != nullptr, "NewPage succeeded after unpinning DEFAULT and KEEP_HOT pages");
    ASSERT_TRUE(ptr3 != ptr0, "Pinned DISCARD_QUICKLY page (p0) was NOT evicted");
    ASSERT_TRUE(ptr3 == ptr1 || ptr3 == ptr2, "Unpinned frame (p1 or p2) was evicted instead");
}

// Test 5: Multiple DISCARD_QUICKLY pages evicted in order before DEFAULT pages
void TestMultipleDiscardQuicklyEviction() {
    std::cout << "--- Test 5: Multiple DISCARD_QUICKLY Evictions ---\n";
    CleanupTestFiles();
    std::string table = "test_t5";
    GlobalBufferPoolManager bpm(3);

    uint32_t p0, p1, p2;
    SlottedPage* ptr0 = bpm.NewPage(table, &p0, BufferHint::DISCARD_QUICKLY);
    SlottedPage* ptr1 = bpm.NewPage(table, &p1, BufferHint::DISCARD_QUICKLY);
    SlottedPage* ptr2 = bpm.NewPage(table, &p2, BufferHint::KEEP_HOT);

    bpm.UnpinPage(table, p0, true);
    bpm.UnpinPage(table, p1, true);
    bpm.UnpinPage(table, p2, true);

    // Allocate p3 -> must evict DISCARD_QUICKLY (p0 or p1), NOT p2
    uint32_t p3;
    SlottedPage* ptr3 = bpm.NewPage(table, &p3, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr3 == ptr0 || ptr3 == ptr1, "1st Eviction: DISCARD_QUICKLY frame evicted");
    ASSERT_TRUE(ptr3 != ptr2, "1st Eviction: KEEP_HOT frame protected");
    bpm.UnpinPage(table, p3, true);

    // Allocate p4 -> must evict remaining DISCARD_QUICKLY frame, NOT p2
    uint32_t p4;
    SlottedPage* ptr4 = bpm.NewPage(table, &p4, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr4 == ptr0 || ptr4 == ptr1, "2nd Eviction: Remaining DISCARD_QUICKLY frame evicted");
    ASSERT_TRUE(ptr4 != ptr2, "2nd Eviction: KEEP_HOT frame STILL protected");
}

// Test 6: Hint Update on Existing Page Fetch
void TestHintUpdateOnFetch() {
    std::cout << "--- Test 6: Hint Upgrade on FetchPage ---\n";
    CleanupTestFiles();
    std::string table = "test_t6";
    GlobalBufferPoolManager bpm(3);

    uint32_t p0, p1, p2;
    SlottedPage* ptr0 = bpm.NewPage(table, &p0, BufferHint::DISCARD_QUICKLY);
    SlottedPage* ptr1 = bpm.NewPage(table, &p1, BufferHint::DEFAULT);
    SlottedPage* ptr2 = bpm.NewPage(table, &p2, BufferHint::DEFAULT);

    bpm.UnpinPage(table, p0, true);
    bpm.UnpinPage(table, p1, true);
    bpm.UnpinPage(table, p2, true);

    // Re-fetch p0 with KEEP_HOT hint -> should upgrade hint from DISCARD_QUICKLY to KEEP_HOT
    bpm.FetchPage(table, p0, BufferHint::KEEP_HOT);
    bpm.UnpinPage(table, p0, false);

    // Now request p3 (DEFAULT) -> p0 is now KEEP_HOT, so p1 or p2 (DEFAULT) must be evicted!
    uint32_t p3;
    SlottedPage* ptr3 = bpm.NewPage(table, &p3, BufferHint::DEFAULT);
    ASSERT_TRUE(ptr3 != ptr0, "Upgraded page p0 (KEEP_HOT) was protected from eviction");
    ASSERT_TRUE(ptr3 == ptr1 || ptr3 == ptr2, "DEFAULT frame (p1 or p2) was evicted");
}

// Test 7: High Volume Eviction Cycle
void TestHighVolumeEvictionCycle() {
    std::cout << "--- Test 7: High Volume Eviction Cycle ---\n";
    CleanupTestFiles();
    std::string table = "test_t7";
    GlobalBufferPoolManager bpm(3);

    uint32_t hot_page_id;
    SlottedPage* hot_ptr = bpm.NewPage(table, &hot_page_id, BufferHint::KEEP_HOT);
    bpm.UnpinPage(table, hot_page_id, true);

    // Perform 100 allocation-eviction cycles with DISCARD_QUICKLY and DEFAULT hints
    for (int i = 0; i < 100; ++i) {
        uint32_t pid;
        BufferHint hint = (i % 2 == 0) ? BufferHint::DISCARD_QUICKLY : BufferHint::DEFAULT;
        SlottedPage* ptr = bpm.NewPage(table, &pid, hint);
        ASSERT_TRUE(ptr != nullptr, "Allocation in loop succeeded");
        ASSERT_TRUE(ptr != hot_ptr, "KEEP_HOT page was NEVER evicted during 100 iterations");
        bpm.UnpinPage(table, pid, true);
    }
}

} // namespace megatron

int main() {
    std::cout << "======================================================\n";
    std::cout << "   GlobalBufferPoolManager 4-Tier Eviction Test Suite \n";
    std::cout << "======================================================\n";

    megatron::TestDiscardQuicklyEvictedFirst();
    megatron::TestKeepHotProtection();
    megatron::TestKeepHotEvictedWhenOnlyKeepHot();
    megatron::TestPinnedPagesNeverEvicted();
    megatron::TestMultipleDiscardQuicklyEviction();
    megatron::TestHintUpdateOnFetch();
    megatron::TestHighVolumeEvictionCycle();

    megatron::CleanupTestFiles();

    std::cout << "======================================================\n";
    std::cout << " Summary: " << megatron::g_passed << " PASSED, "
              << megatron::g_failed << " FAILED\n";
    std::cout << "======================================================\n";

    return (megatron::g_failed == 0) ? 0 : 1;
}
