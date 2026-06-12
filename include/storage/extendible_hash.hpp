#pragma once
#include <vector>
#include <memory>
#include <functional>

namespace dbms {
namespace storage {

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

    size_t global_depth;
    size_t bucket_capacity;
    std::vector<std::shared_ptr<Bucket>> directory;

    size_t hashFunction(const K& key) const {
        return std::hash<K>{}(key);
    }

    size_t getIndex(const K& key) const {
        return hashFunction(key) & ((1 << global_depth) - 1);
    }

    void splitBucket(size_t index) {
        auto old_bucket = directory[index];
        if (old_bucket->local_depth == global_depth) {
            size_t old_size = directory.size();
            directory.resize(old_size * 2);
            for (size_t i = 0; i < old_size; ++i) {
                directory[old_size + i] = directory[i];
            }
            global_depth++;
        }

        auto new_bucket = std::make_shared<Bucket>(old_bucket->local_depth + 1, bucket_capacity);
        old_bucket->local_depth++;

        std::vector<Entry> all_entries = std::move(old_bucket->entries);
        old_bucket->entries.clear();

        size_t local_mask = (1 << old_bucket->local_depth) - 1;
        
        for (size_t i = 0; i < directory.size(); ++i) {
            if (directory[i] == old_bucket) {
                if ((i & local_mask) != (index & local_mask)) {
                    directory[i] = new_bucket;
                }
            }
        }

        for (const auto& entry : all_entries) {
            insert(entry.key, entry.value);
        }
    }

public:
    explicit ExtendibleHash(size_t init_depth, size_t capacity) 
        : global_depth(init_depth), bucket_capacity(capacity) {
        size_t dir_size = 1 << global_depth;
        directory.resize(dir_size);
        for (size_t i = 0; i < dir_size; ++i) {
            directory[i] = std::make_shared<Bucket>(global_depth, bucket_capacity);
        }
    }

    void insert(const K& key, const V& value) {
        size_t index = getIndex(key);
        auto bucket = directory[index];
        
        for (auto& entry : bucket->entries) {
            if (entry.key == key) {
                entry.value = value;
                return;
            }
        }

        if (bucket->is_full()) {
            splitBucket(index);
            insert(key, value);
        } else {
            bucket->entries.push_back({key, value});
        }
    }

    bool get(const K& key, V& out_value) const {
        size_t index = getIndex(key);
        auto bucket = directory[index];
        for (const auto& entry : bucket->entries) {
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
