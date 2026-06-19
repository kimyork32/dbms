#pragma once
#include <vector>
#include <memory>

namespace megatron {

/**
 * @brief implements an extendible hashing table
 * @tparam K key type
 * @tparam V value type
 */
template <typename K, typename V>
class ExtendibleHash {
private:
    struct Entry {
        K key;
        V value;
    };

    struct Bucket {
        size_t local_depth;
        size_t capacity;
        std::vector<Entry> entries;

        explicit Bucket(size_t depth, size_t cap) : local_depth(depth), capacity(cap) {}
        bool is_full() const { return entries.size() >= capacity; }
    };

    size_t global_depth_;
    size_t bucket_capacity_;
    std::vector<std::shared_ptr<Bucket>> directory_;

    size_t HashFunction(const K& key) const;
    size_t GetIndex(const K& key) const;
    void SplitBucket(size_t index);

public:
    explicit ExtendibleHash(size_t init_depth, size_t capacity);
    void Insert(const K& key, const V& value);
    bool Get(const K& key, V& out_value) const;
};

} // namespace megatron
