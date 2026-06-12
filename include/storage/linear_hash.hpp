#pragma once
#include <vector>
#include <list>
#include <functional>

namespace dbms {
namespace storage {

template <typename K, typename V>
class LinearHash {
private:
    struct Entry {
        K key;
        V value;
    };

    std::vector<std::list<Entry>> buckets;
    size_t N; // cant inicial de buckets
    size_t level; // nivel actual (i)
    size_t split_pointer; // puntero de division (s)
    size_t bucket_capacity;
    size_t num_elements;

    size_t hashFunction(const K& key, size_t current_level) const {
        size_t mod = N * (1 << current_level);
        return std::hash<K>{}(key) % mod;
    }

    size_t getIndex(const K& key) const {
        size_t index = hashFunction(key, level);
        if (index < split_pointer) {
            index = hashFunction(key, level + 1);
        }
        return index;
    }

    void split() {
        buckets.push_back(std::list<Entry>());
        size_t old_index = split_pointer;
        
        auto& old_bucket = buckets[old_index];
        auto it = old_bucket.begin();
        
        while (it != old_bucket.end()) {
            size_t new_index = hashFunction(it->key, level + 1);
            if (new_index != old_index) {
                buckets[new_index].push_back(*it);
                it = old_bucket.erase(it);
            } else {
                ++it;
            }
        }

        split_pointer++;
        if (split_pointer == N * (1 << level)) {
            level++;
            split_pointer = 0;
        }
    }

public:
    explicit LinearHash(size_t initial_buckets, size_t capacity) 
        : N(initial_buckets), level(0), split_pointer(0), bucket_capacity(capacity), num_elements(0) {
        buckets.resize(N);
    }

    void insert(const K& key, const V& value) {
        size_t index = getIndex(key);
        
        for (auto& entry : buckets[index]) {
            if (entry.key == key) {
                entry.value = value;
                return;
            }
        }

        buckets[index].push_back({key, value});
        num_elements++;

        // factor de carga basico
        double load_factor = static_cast<double>(num_elements) / (buckets.size() * bucket_capacity);
        if (load_factor > 0.75) {
            split();
        }
    }

    bool get(const K& key, V& out_value) const {
        size_t index = getIndex(key);
        for (const auto& entry : buckets[index]) {
            if (entry.key == key) {
                out_value = entry.value;
                return true;
            }
        }
        return false;
    }
};

} // namespace storage
} // namespace dbms
