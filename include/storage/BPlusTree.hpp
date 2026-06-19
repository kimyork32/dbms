#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <stdexcept>

namespace megatron {

#include "PageHeader.hpp"
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

    /**
     * @brief reads a full page from disk
     * @param offset byte position from file start
     * @param buffer pointer to read destination
     */
    void ReadPage(int64_t offset, void* buffer) {
        if (pread(fd_, buffer, PAGE_SIZE, offset) != PAGE_SIZE) {
            throw std::runtime_error("error in read page (I/O)");
        }
    }

    /**
     * @brief writes a full page to disk
     * @param offset byte position where to write
     * @param buffer data to write
     */
    void WritePage(int64_t offset, const void* buffer) {
        if (pwrite(fd_, buffer, PAGE_SIZE, offset) != PAGE_SIZE) {
            throw std::runtime_error("error in write page (I/O)");
        }
    }

    /**
     * @brief synchronizes current metadata page with disk
     */
    void SaveMeta() {
        WritePage(0, &meta_);
    }

    /**
     * @brief reserves a new page, reusing from free list if possible
     * @return offset of reserved page
     */
    int64_t AllocatePage() {
        if (meta_.free_list_head != -1) {
            int64_t recycled_off = meta_.free_list_head;
            
            // read next in free list
            char buf[PAGE_SIZE];
            ReadPage(recycled_off, buf);
            int64_t next_free = *reinterpret_cast<int64_t*>(buf);
            
            meta_.free_list_head = next_free;
            SaveMeta();
            
            // clear recycled page
            char empty[PAGE_SIZE] = {0};
            WritePage(recycled_off, empty);
            return recycled_off;
        }

        int64_t new_offset = meta_.next_append_offset;
        meta_.next_append_offset += PAGE_SIZE;
        SaveMeta();
        
        // initialize block to 0 on disk
        char empty[PAGE_SIZE] = {0};
        WritePage(new_offset, empty);
        return new_offset;
    }

    /**
     * @brief frees a page and adds it to free list
     * @param offset offset of page to free
     */
    void DeallocatePage(int64_t offset) {
        if (offset <= 0) return; // do not free meta-page or invalid offsets

        char buf[PAGE_SIZE] = {0};
        int64_t* next_ptr = reinterpret_cast<int64_t*>(buf);
        *next_ptr = meta_.free_list_head;
        
        WritePage(offset, buf);
        
        meta_.free_list_head = offset;
        SaveMeta();
    }

    /**
     * @brief splits a child node that has reached maximum capacity
     * @param parent_off offset of parent node
     * @param index child index in parent
     * @param child_off offset of child node to split
     */
    void SplitChild(int64_t parent_off, int index, int64_t child_off) {
        char p_buf[PAGE_SIZE], c_buf[PAGE_SIZE], z_buf[PAGE_SIZE] = {0};
        
        ReadPage(parent_off, p_buf);
        ReadPage(child_off, c_buf);
        
        auto* parent = reinterpret_cast<BPlusInternalNode<int>*>(p_buf);
        auto* child_hdr = reinterpret_cast<NodeHeader*>(c_buf);
        
        int64_t z_off = AllocatePage();
        int k_up;

        if (child_hdr->is_leaf) {
            auto* y = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(c_buf);
            auto* z = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(z_buf);
            
            z->header.is_leaf = 1;
            z->header.num_keys = B;
            y->header.num_keys = B - 1;
            
            for (int j = 0; j < B; j++) {
                z->keys[j] = y->keys[j + B - 1];
                z->payloads[j] = y->payloads[j + B - 1];
            }
            
            z->next_leaf = y->next_leaf;
            z->prev_leaf = child_off;
            y->next_leaf = z_off;
            
            if (z->next_leaf != -1) {
                char right_buf[PAGE_SIZE];
                ReadPage(z->next_leaf, right_buf);
                auto* right_sibling = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(right_buf);
                right_sibling->prev_leaf = z_off;
                WritePage(z->next_leaf, right_buf);
            }
            
            k_up = z->keys[0]; 
        } else {
            auto* y = reinterpret_cast<BPlusInternalNode<int>*>(c_buf);
            auto* z = reinterpret_cast<BPlusInternalNode<int>*>(z_buf);
            
            z->header.is_leaf = 0;
            z->header.num_keys = B - 1;
            y->header.num_keys = B - 1;
            
            for (int j = 0; j < B - 1; j++) {
                z->keys[j] = y->keys[j + B];
            }
            for (int j = 0; j < B; j++) {
                z->children_offsets[j] = y->children_offsets[j + B];
            }
            
            k_up = y->keys[B - 1]; 
        }

        for (int j = parent->header.num_keys; j >= index + 1; j--) {
            parent->children_offsets[j + 1] = parent->children_offsets[j];
        }
        parent->children_offsets[index + 1] = z_off;

        for (int j = parent->header.num_keys - 1; j >= index; j--) {
            parent->keys[j + 1] = parent->keys[j];
        }
        parent->keys[index] = k_up;
        parent->header.num_keys++;

        WritePage(parent_off, p_buf);
        WritePage(child_off, c_buf);
        WritePage(z_off, z_buf);
    }

    /**
     * @brief inserts a key into a node guaranteed not to be full
     * @param node_off node offset
     * @param k key to insert
     * @param v associated value
     */
    void InsertNonFull(int64_t node_off, int k, int64_t v) {
        char buf[PAGE_SIZE];
        ReadPage(node_off, buf);
        NodeHeader* hdr = reinterpret_cast<NodeHeader*>(buf);
        
        int i = hdr->num_keys - 1;
        
        if (hdr->is_leaf) {
            auto* leaf = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(buf);
            while (i >= 0 && k < leaf->keys[i]) {
                leaf->keys[i + 1] = leaf->keys[i];
                leaf->payloads[i + 1] = leaf->payloads[i];
                i--;
            }
            leaf->keys[i + 1] = k;
            leaf->payloads[i + 1] = v;
            leaf->header.num_keys++;
            WritePage(node_off, buf);
        } else {
            auto* internal = reinterpret_cast<BPlusInternalNode<int>*>(buf);
            while (i >= 0 && k < internal->keys[i]) i--;
            i++;
            
            int64_t child_off = internal->children_offsets[i];
            char c_buf[PAGE_SIZE];
            ReadPage(child_off, c_buf);
            NodeHeader* child_hdr = reinterpret_cast<NodeHeader*>(c_buf);
            
            if (child_hdr->num_keys == 2 * B - 1) {
                SplitChild(node_off, i, child_off);
                ReadPage(node_off, buf); // refresh parent post-split
                internal = reinterpret_cast<BPlusInternalNode<int>*>(buf);
                if (k >= internal->keys[i]) i++;
            }
            
            InsertNonFull(internal->children_offsets[i], k, v);
        }
    }

    /**
     * @brief deletes a key recursively
     * @param node_off current node offset
     * @param k key to delete
     */
    void DeleteRecursive(int64_t node_off, int k) {
        char buf[PAGE_SIZE];
        ReadPage(node_off, buf);
        NodeHeader* hdr = reinterpret_cast<NodeHeader*>(buf);

        if (hdr->is_leaf) {
            auto* leaf = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(buf);
            int idx = -1;
            for (int i = 0; i < leaf->header.num_keys; i++) {
                if (leaf->keys[i] == k) { idx = i; break; }
            }
            if (idx != -1) {
                for (int i = idx; i < leaf->header.num_keys - 1; i++) {
                    leaf->keys[i] = leaf->keys[i + 1];
                    leaf->payloads[i] = leaf->payloads[i + 1];
                }
                leaf->header.num_keys--;
                WritePage(node_off, buf);
            }
            return;
        }

        auto* internal = reinterpret_cast<BPlusInternalNode<int>*>(buf);
        int i = 0;
        while (i < internal->header.num_keys && k >= internal->keys[i]) i++;

        int64_t child_off = internal->children_offsets[i];
        char c_buf[PAGE_SIZE];
        ReadPage(child_off, c_buf);
        NodeHeader* c_hdr = reinterpret_cast<NodeHeader*>(c_buf);

        if (c_hdr->num_keys == B - 1) {
            FixUnderflow(node_off, buf, internal, i, child_off, c_buf, c_hdr);
            
            // recalculate routing index after possible parent restructuring
            i = 0;
            while (i < internal->header.num_keys && k >= internal->keys[i]) i++;
            child_off = internal->children_offsets[i];
        }

        DeleteRecursive(child_off, k);
    }

    /**
     * @brief fixes underflow of a child node
     * @param p_off parent offset
     * @param p_buf parent buffer
     * @param parent parent structure
     * @param c_idx child index in parent
     * @param c_off child offset
     * @param c_buf child buffer
     * @param c_hdr child header
     */
    void FixUnderflow(int64_t p_off, char* p_buf, BPlusInternalNode<int>* parent, int c_idx, int64_t c_off, char* c_buf, NodeHeader* c_hdr) {
        int64_t left_off = (c_idx > 0) ? parent->children_offsets[c_idx - 1] : -1;
        int64_t right_off = (c_idx < parent->header.num_keys) ? parent->children_offsets[c_idx + 1] : -1;

        char s_buf[PAGE_SIZE]; // sibling buffer

        // try borrow from left sibling
        if (left_off != -1) {
            ReadPage(left_off, s_buf);
            NodeHeader* left_hdr = reinterpret_cast<NodeHeader*>(s_buf);
            if (left_hdr->num_keys >= B) {
                BorrowFromLeft(parent, c_idx, c_buf, c_hdr, s_buf, left_hdr);
                WritePage(p_off, p_buf);
                WritePage(c_off, c_buf);
                WritePage(left_off, s_buf);
                return;
            }
        }

        // try borrow from right sibling
        if (right_off != -1) {
            ReadPage(right_off, s_buf);
            NodeHeader* right_hdr = reinterpret_cast<NodeHeader*>(s_buf);
            if (right_hdr->num_keys >= B) {
                BorrowFromRight(parent, c_idx, c_buf, c_hdr, s_buf, right_hdr);
                WritePage(p_off, p_buf);
                WritePage(c_off, c_buf);
                WritePage(right_off, s_buf);
                return;
            }
        }

        // mandatory merge
        if (left_off != -1) {
            ReadPage(left_off, s_buf);
            NodeHeader* left_hdr = reinterpret_cast<NodeHeader*>(s_buf);
            MergeNodes(parent, c_idx - 1, s_buf, left_hdr, c_buf, c_hdr);
            WritePage(p_off, p_buf);
            WritePage(left_off, s_buf);
            DeallocatePage(c_off);
        } else {
            ReadPage(right_off, s_buf); // force load of right to merge rightwards
            NodeHeader* right_hdr = reinterpret_cast<NodeHeader*>(s_buf);
            MergeNodes(parent, c_idx, c_buf, c_hdr, s_buf, right_hdr);
            WritePage(p_off, p_buf);
            WritePage(c_off, c_buf);
            DeallocatePage(right_off);
        }
    }

    /**
     * @brief borrows a key from left sibling
     */
    void BorrowFromLeft(BPlusInternalNode<int>* parent, int c_idx, char* c_buf, NodeHeader* c_hdr, char* l_buf, NodeHeader* l_hdr) {
        if (c_hdr->is_leaf) {
            auto* child = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(c_buf);
            auto* left = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(l_buf);

            // shift child right
            for (int i = child->header.num_keys; i > 0; i--) {
                child->keys[i] = child->keys[i - 1];
                child->payloads[i] = child->payloads[i - 1];
            }
            child->keys[0] = left->keys[left->header.num_keys - 1];
            child->payloads[0] = left->payloads[left->header.num_keys - 1];
            
            parent->keys[c_idx - 1] = child->keys[0]; // update router
        } else {
            auto* child = reinterpret_cast<BPlusInternalNode<int>*>(c_buf);
            auto* left = reinterpret_cast<BPlusInternalNode<int>*>(l_buf);

            for (int i = child->header.num_keys; i > 0; i--) child->keys[i] = child->keys[i - 1];
            for (int i = child->header.num_keys + 1; i > 0; i--) child->children_offsets[i] = child->children_offsets[i - 1];

            child->keys[0] = parent->keys[c_idx - 1]; // pull down separator
            child->children_offsets[0] = left->children_offsets[left->header.num_keys]; // move child
            parent->keys[c_idx - 1] = left->keys[left->header.num_keys - 1]; // push up new separator
        }
        c_hdr->num_keys++;
        l_hdr->num_keys--;
    }

    /**
     * @brief borrows a key from right sibling
     */
    void BorrowFromRight(BPlusInternalNode<int>* parent, int c_idx, char* c_buf, NodeHeader* c_hdr, char* r_buf, NodeHeader* r_hdr) {
        if (c_hdr->is_leaf) {
            auto* child = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(c_buf);
            auto* right = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(r_buf);

            child->keys[child->header.num_keys] = right->keys[0];
            child->payloads[child->header.num_keys] = right->payloads[0];

            for (int i = 0; i < right->header.num_keys - 1; i++) {
                right->keys[i] = right->keys[i + 1];
                right->payloads[i] = right->payloads[i + 1];
            }
            parent->keys[c_idx] = right->keys[0];
        } else {
            auto* child = reinterpret_cast<BPlusInternalNode<int>*>(c_buf);
            auto* right = reinterpret_cast<BPlusInternalNode<int>*>(r_buf);

            child->keys[child->header.num_keys] = parent->keys[c_idx];
            child->children_offsets[child->header.num_keys + 1] = right->children_offsets[0];
            parent->keys[c_idx] = right->keys[0];

            for (int i = 0; i < right->header.num_keys - 1; i++) right->keys[i] = right->keys[i + 1];
            for (int i = 0; i < right->header.num_keys; i++) right->children_offsets[i] = right->children_offsets[i + 1];
        }
        c_hdr->num_keys++;
        r_hdr->num_keys--;
    }

    /**
     * @brief merges two sibling nodes
     */
    void MergeNodes(BPlusInternalNode<int>* parent, int p_idx, char* l_buf, NodeHeader* l_hdr, char* r_buf, NodeHeader* r_hdr) {
        if (l_hdr->is_leaf) {
            auto* left = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(l_buf);
            auto* right = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(r_buf);

            for (int i = 0; i < right->header.num_keys; i++) {
                left->keys[left->header.num_keys + i] = right->keys[i];
                left->payloads[left->header.num_keys + i] = right->payloads[i];
            }
            left->header.num_keys += right->header.num_keys;
            
            // relink horizontal list
            left->next_leaf = right->next_leaf;
            if (right->next_leaf != -1) {
                char next_buf[PAGE_SIZE];
                ReadPage(right->next_leaf, next_buf);
                auto* next_leaf = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(next_buf);
                next_leaf->prev_leaf = parent->children_offsets[p_idx]; // left offset
                WritePage(right->next_leaf, next_buf);
            }
        } else {
            auto* left = reinterpret_cast<BPlusInternalNode<int>*>(l_buf);
            auto* right = reinterpret_cast<BPlusInternalNode<int>*>(r_buf);

            left->keys[left->header.num_keys] = parent->keys[p_idx]; // push down separator
            left->header.num_keys++;

            for (int i = 0; i < right->header.num_keys; i++) {
                left->keys[left->header.num_keys + i] = right->keys[i];
            }
            for (int i = 0; i <= right->header.num_keys; i++) {
                left->children_offsets[left->header.num_keys + i] = right->children_offsets[i];
            }
            left->header.num_keys += right->header.num_keys;
        }

        // remove router key and pointer to right child in parent
        for (int i = p_idx; i < parent->header.num_keys - 1; i++) {
            parent->keys[i] = parent->keys[i + 1];
        }
        for (int i = p_idx + 1; i < parent->header.num_keys; i++) {
            parent->children_offsets[i] = parent->children_offsets[i + 1];
        }
        parent->header.num_keys--;
    }

public:
    /**
     * @brief constructor. opens file and loads or initializes tree structure
     * @param filename file name of the database
     */
    BPlusTreeDisk(const char* filename) {
        fd_ = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd_ < 0) throw std::runtime_error("error opening db file");

        struct stat st;
        fstat(fd_, &st);
        if (st.st_size == 0) {
            meta_.magic = 0x42545245; // 'BTRE'
            meta_.root_offset = PAGE_SIZE;
            meta_.free_list_head = -1;
            meta_.next_append_offset = PAGE_SIZE * 2;
            SaveMeta();

            // create root
            char root_buf[PAGE_SIZE] = {0};
            auto* root = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(root_buf);
            root->header.is_leaf = 1;
            root->header.num_keys = 0;
            root->prev_leaf = -1;
            root->next_leaf = -1;
            WritePage(PAGE_SIZE, root_buf);
        } else {
            ReadPage(0, &meta_);
        }
    }

    /**
     * @brief destructor. closes file descriptor
     */
    ~BPlusTreeDisk() {
        close(fd_);
    }

    /**
     * @brief searches for a value associated with a key
     * @param k key to search
     * @return value (payload) if found, or -1 if not exists
     */
    int64_t Search(int k) {
        int64_t curr_off = meta_.root_offset;
        char buf[PAGE_SIZE];

        while (true) {
            ReadPage(curr_off, buf);
            NodeHeader* hdr = reinterpret_cast<NodeHeader*>(buf);

            if (hdr->is_leaf) {
                auto* leaf = reinterpret_cast<BPlusLeafNode<int, int64_t>*>(buf);
                for (int i = 0; i < leaf->header.num_keys; i++) {
                    if (leaf->keys[i] == k) return leaf->payloads[i];
                }
                return -1; // not found
            } else {
                auto* internal = reinterpret_cast<BPlusInternalNode<int>*>(buf);
                int i = 0;
                while (i < internal->header.num_keys && k >= internal->keys[i]) i++;
                curr_off = internal->children_offsets[i];
            }
        }
    }

    /**
     * @brief inserts a key-value pair into the tree
     * @param k key to insert
     * @param v associated value
     */
    void Insert(int k, int64_t v) {
        char root_buf[PAGE_SIZE];
        ReadPage(meta_.root_offset, root_buf);
        NodeHeader* root_hdr = reinterpret_cast<NodeHeader*>(root_buf);

        if (root_hdr->num_keys == 2 * B - 1) {
            int64_t new_root_off = AllocatePage();
            char new_root_buf[PAGE_SIZE] = {0};
            auto* new_root = reinterpret_cast<BPlusInternalNode<int>*>(new_root_buf);
            
            new_root->header.is_leaf = 0;
            new_root->header.num_keys = 0;
            new_root->children_offsets[0] = meta_.root_offset;
            WritePage(new_root_off, new_root_buf);
            
            SplitChild(new_root_off, 0, meta_.root_offset);
            
            meta_.root_offset = new_root_off;
            SaveMeta();
            
            InsertNonFull(new_root_off, k, v);
        } else {
            InsertNonFull(meta_.root_offset, k, v);
        }
    }

    /**
     * @brief removes a key from the tree
     * @param k key to remove
     */
    void Remove(int k) {
        if (meta_.root_offset == -1) return;

        DeleteRecursive(meta_.root_offset, k);

        // root collapse check
        char root_buf[PAGE_SIZE];
        ReadPage(meta_.root_offset, root_buf);
        NodeHeader* root_hdr = reinterpret_cast<NodeHeader*>(root_buf);

        if (root_hdr->num_keys == 0 && !root_hdr->is_leaf) {
            auto* root_int = reinterpret_cast<BPlusInternalNode<int>*>(root_buf);
            int64_t old_root = meta_.root_offset;
            meta_.root_offset = root_int->children_offsets[0];
            SaveMeta();
            DeallocatePage(old_root);
        }
    }
};

} // namespace megatron
