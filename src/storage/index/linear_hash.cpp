#include "storage/index/linear_hash.hpp"
#include <string>

namespace megatron {

template <typename K, typename V>
size_t LinearHash<K, V>::HashFunction(const K& key, size_t current_level) const {
    size_t mod = n_ * (1 << current_level);
    return std::hash<K>{}(key) % mod;
}

template <typename K, typename V>
size_t LinearHash<K, V>::GetIndex(const K& key) const {
    size_t index = HashFunction(key, level_);
    if (index < split_pointer_) {
        index = HashFunction(key, level_ + 1);
    }
    return index;
}

template <typename K, typename V>
void LinearHash<K, V>::Split() {
    buckets_.push_back(std::list<Entry>());
    size_t old_index = split_pointer_;
    
    auto& old_bucket = buckets_[old_index];
    auto it = old_bucket.begin();
    
    while (it != old_bucket.end()) {
        size_t new_index = HashFunction(it->key, level_ + 1);
        if (new_index != old_index) {
            buckets_[new_index].push_back(*it);
            it = old_bucket.erase(it);
        } else {
            ++it;
        }
    }

    split_pointer_++;
    if (split_pointer_ == n_ * (1 << level_)) {
        level_++;
        split_pointer_ = 0;
    }
}

template <typename K, typename V>
LinearHash<K, V>::LinearHash(size_t initial_buckets, size_t capacity) 
    : n_(initial_buckets), level_(0), split_pointer_(0), bucket_capacity_(capacity), num_elements_(0) {
    buckets_.resize(n_);
}

template <typename K, typename V>
void LinearHash<K, V>::Insert(const K& key, const V& value) {
    size_t index = GetIndex(key);
    
    for (auto& entry : buckets_[index]) {
        if (entry.key == key) {
            entry.value = value;
            return;
        }
    }

    buckets_[index].push_back({key, value});
    num_elements_++;

    double load_factor = static_cast<double>(num_elements_) / (buckets_.size() * bucket_capacity_);
    if (load_factor > 0.75) {
        Split();
    }
}

template <typename K, typename V>
bool LinearHash<K, V>::Get(const K& key, V& out_value) const {
    size_t index = GetIndex(key);
    for (const auto& entry : buckets_[index]) {
        if (entry.key == key) {
            out_value = entry.value;
            return true;
        }
    }
    return false;
}

// Explicit instantiations
template class LinearHash<int, std::string>;
template class LinearHash<int, int64_t>;

} // namespace megatron
