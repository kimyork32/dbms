#pragma once
#include <vector>
#include <list>

namespace megatron {

/**
 * @brief implements a linear hashing table
 * @tparam K key type
 * @tparam V value type
 */
template <typename K, typename V>
class LinearHash {
private:
    struct Entry {
        K key;
        V value;
    };

    std::vector<std::list<Entry>> buckets_;
    size_t n_; 
    size_t level_; 
    size_t split_pointer_; 
    size_t bucket_capacity_;
    size_t num_elements_;

    size_t HashFunction(const K& key, size_t current_level) const;
    size_t GetIndex(const K& key) const;
    void Split();

public:
    explicit LinearHash(size_t initial_buckets, size_t capacity);
    void Insert(const K& key, const V& value);
    bool Get(const K& key, V& out_value) const;
};

} // namespace megatron
