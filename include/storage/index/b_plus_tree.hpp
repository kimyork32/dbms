#pragma once

#include <cstdint>
#include <stdexcept>
#include "storage/page/page_header.hpp"

namespace megatron {

constexpr int B = 50;  // minimum degree. max children = 100, max keys = 99.

/**
 * @brief metadata page structure (page 0) of the file
 * stores critical information for tree persistence
 */
struct MetaPage {
    uint32_t magic;             ///< magic number to identify file format
    int64_t root_offset;        ///< offset (in bytes) of the root page
    int64_t free_list_head;     ///< head of the free page list for reuse
    int64_t next_append_offset; ///< offset for the next new page at the end of the file
    char padding[PAGE_SIZE - 28]; ///< padding to complete page size
};

/**
 * @brief common header for all tree nodes
 */
struct NodeHeader {
    uint8_t is_leaf;    ///< indicates if node is leaf (1) or internal (0)
    uint16_t num_keys;  ///< current number of keys stored in the node
};

/**
 * @brief representation of a leaf node in the b+ tree
 * @tparam K key type
 * @tparam V value type (payload)
 */
template <typename K, typename V>
struct BPlusLeafNode {
    NodeHeader header;      ///< node header
    int64_t prev_leaf;      ///< offset to previous leaf (doubly linked list)
    int64_t next_leaf;      ///< offset to next leaf (doubly linked list)
    K keys[2 * B - 1];      ///< array of keys
    V payloads[2 * B - 1];  ///< array of values associated with keys
};

/**
 * @brief representation of an internal node in the b+ tree
 * @tparam K key type
 */
template <typename K>
struct BPlusInternalNode {
    NodeHeader header;               ///< node header
    K keys[2 * B - 1];               ///< array of keys that act as separators
    int64_t children_offsets[2 * B]; ///< offsets to child nodes
};

/**
 * @brief main class to manage a b+ tree stored on disk
 * supports insertion, search, and deletion with persistence
 */
class BPlusTreeDisk {
private:
    int fd_;            ///< file descriptor for i/o operations
    MetaPage meta_;     ///< in-memory structure of metadata page

    void ReadPage(int64_t offset, void* buffer);
    void WritePage(int64_t offset, const void* buffer);
    void SaveMeta();
    int64_t AllocatePage();
    void DeallocatePage(int64_t offset);
    void SplitChild(int64_t parent_off, int index, int64_t child_off);
    void InsertNonFull(int64_t node_off, int k, int64_t v);
    void DeleteRecursive(int64_t node_off, int k);
    void FixUnderflow(int64_t p_off, char* p_buf, BPlusInternalNode<int>* parent, int c_idx, int64_t c_off, char* c_buf, NodeHeader* c_hdr);
    void BorrowFromLeft(BPlusInternalNode<int>* parent, int c_idx, char* c_buf, NodeHeader* c_hdr, char* l_buf, NodeHeader* l_hdr);
    void BorrowFromRight(BPlusInternalNode<int>* parent, int c_idx, char* c_buf, NodeHeader* c_hdr, char* r_buf, NodeHeader* r_hdr);
    void MergeNodes(BPlusInternalNode<int>* parent, int p_idx, char* l_buf, NodeHeader* l_hdr, char* r_buf, NodeHeader* r_hdr);

public:
    BPlusTreeDisk(const char* filename);
    ~BPlusTreeDisk();
    int64_t Search(int k);
    void Insert(int k, int64_t v);
    void Remove(int k);
};

} // namespace megatron
