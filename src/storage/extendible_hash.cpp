#include "storage/extendible_hash.hpp"
#include <string>

namespace megatron {

template <typename K, typename V>
size_t ExtendibleHash<K, V>::HashFunction(const K& key) const {
    return std::hash<K>{}(key);
}

template <typename K, typename V>
size_t ExtendibleHash<K, V>::GetIndex(const K& key) const {
    return HashFunction(key) & ((1 << global_depth_) - 1);
}

template <typename K, typename V>
void ExtendibleHash<K, V>::SplitBucket(size_t index) {
    auto old_bucket = directory_[index];
    if (old_bucket->local_depth == global_depth_) {
        size_t old_size = directory_.size();
        directory_.resize(old_size * 2);
        for (size_t i = 0; i < old_size; ++i) {
            directory_[old_size + i] = directory_[i];
        }
        global_depth_++;
    }

    auto new_bucket = std::make_shared<Bucket>(old_bucket->local_depth + 1, bucket_capacity_);
    old_bucket->local_depth++;

    std::vector<Entry> all_entries = std::move(old_bucket->entries);
    old_bucket->entries.clear();

    size_t local_mask = (1 << old_bucket->local_depth) - 1;
    
    for (size_t i = 0; i < directory_.size(); ++i) {
        if (directory_[i] == old_bucket) {
            if ((i & local_mask) != (index & local_mask)) {
                directory_[i] = new_bucket;
            }
        }
    }

    for (const auto& entry : all_entries) {
        Insert(entry.key, entry.value);
    }
}

template <typename K, typename V>
ExtendibleHash<K, V>::ExtendibleHash(size_t init_depth, size_t capacity) 
    : global_depth_(init_depth), bucket_capacity_(capacity) {
    size_t dir_size = 1 << global_depth_;
    directory_.resize(dir_size);
    for (size_t i = 0; i < dir_size; ++i) {
        directory_[i] = std::make_shared<Bucket>(global_depth_, bucket_capacity_);
    }
}

template <typename K, typename V>
void ExtendibleHash<K, V>::Insert(const K& key, const V& value) {
    size_t index = GetIndex(key);
    auto bucket = directory_[index];
    
    for (auto& entry : bucket->entries) {
        if (entry.key == key) {
            entry.value = value;
            return;
        }
    }

    if (bucket->is_full()) {
        SplitBucket(index);
        Insert(key, value);
    } else {
        bucket->entries.push_back({key, value});
    }
}

template <typename K, typename V>
bool ExtendibleHash<K, V>::Get(const K& key, V& out_value) const {
    size_t index = GetIndex(key);
    auto bucket = directory_[index];
    for (const auto& entry : bucket->entries) {
        if (entry.key == key) {
            out_value = entry.value;
            return true;
        }
    }
    return false;
}

// Explicit instantiations
template class ExtendibleHash<int, std::string>;
template class ExtendibleHash<int, int64_t>;

} // namespace megatron
