#pragma once
#include <vector>
#include <list>
#include <functional>

namespace dbms {
namespace storage {

template <typename K, typename V>
class StaticHash {
private:
    struct Entry {
        K key;
        V value;
    };
    
    // tam de los buckets fijo 
    // listas actúan como paginas de desbordamiento
    std::vector<std::list<Entry>> buckets;
    size_t num_buckets;

    size_t hashFunction(const K& key) const {
        return std::hash<K>{}(key) % num_buckets;
    }

public:
    explicit StaticHash(size_t size) : num_buckets(size) {
        buckets.resize(num_buckets);
    }

    void insert(const K& key, const V& value) {
        size_t index = hashFunction(key);
        for (auto& entry : buckets[index]) {
            if (entry.key == key) {
                entry.value = value; // Update
                return;
            }
        }
        buckets[index].push_back({key, value});
    }

    bool get(const K& key, V& out_value) const {
        size_t index = hashFunction(key);
        for (const auto& entry : buckets[index]) {
            if (entry.key == key) {
                out_value = entry.value;
                return true;
            }
        }
        return false;
    }

    void remove(const K& key) {
        size_t index = hashFunction(key);
        buckets[index].remove_if([&key](const Entry& e) { return e.key == key; });
    }
};

} // namespace storage
} // namespace dbms
