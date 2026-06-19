#include "storage/index/static_hash.hpp"
#include <string>

namespace megatron {

template <typename K, typename V>
size_t StaticHash<K, V>::HashFunction(const K& key) const {
    return std::hash<K>{}(key) % num_buckets_;
}

template <typename K, typename V>
StaticHash<K, V>::StaticHash(size_t size) : num_buckets_(size) {
    buckets_.resize(num_buckets_);
}

template <typename K, typename V>
void StaticHash<K, V>::Insert(const K& key, const V& value) {
    size_t index = HashFunction(key);
    for (auto& entry : buckets_[index]) {
        if (entry.key == key) {
            entry.value = value; 
            return;
        }
    }
    buckets_[index].push_back({key, value});
}

template <typename K, typename V>
bool StaticHash<K, V>::Get(const K& key, V& out_value) const {
    size_t index = HashFunction(key);
    for (const auto& entry : buckets_[index]) {
        if (entry.key == key) {
            out_value = entry.value;
            return true;
        }
    }
    return false;
}

template <typename K, typename V>
void StaticHash<K, V>::Remove(const K& key) {
    size_t index = HashFunction(key);
    buckets_[index].remove_if([&key](const Entry& e) { return e.key == key; });
}

// Explicit instantiations
template class StaticHash<int, std::string>;
template class StaticHash<int, int64_t>;

} // namespace megatron
