#pragma once
#include <vector>
#include <list>

namespace megatron {

/**
 * @brief implements a static hashing table
 * @tparam K key type
 * @tparam V value type
 */
template <typename K, typename V>
class StaticHash {
private:
    struct Entry {
        K key;
        V value;
    };
    
    std::vector<std::list<Entry>> buckets_;
    size_t num_buckets_;

    size_t HashFunction(const K& key) const;

public:
    explicit StaticHash(size_t size);
    void Insert(const K& key, const V& value);
    bool Get(const K& key, V& out_value) const;
    void Remove(const K& key);
};

} // namespace megatron
