#ifndef BASE_SHARD_CACHE_H
#define BASE_SHARD_CACHE_H

#include <atomic>
#include <boost/utility/string_view.hpp>
#include <mutex>

namespace base {
struct KeyType {
    boost::string_view slice;
    uint32_t hash_value;

    KeyType() = default;
    KeyType(const boost::string_view &k, uint32_t h) {
        slice = k;
        hash_value = h;
    }
    static bool equal(const KeyType &a, const KeyType &b) {
        return a.hash_value == b.hash_value && a.slice == b.slice;
    }
    static size_t hash(const KeyType &a) {
        return static_cast<size_t>(a.hash_value);
    }
    static KeyType FromString(const std::string &s, uint32_t offset = 0) {
        char *str = new char[s.size() + 1];
        std::strcpy(str, s.data());
        return {str, hashString(s, offset)};
    }
    static KeyType FromStringRef(const std::string &s, uint32_t offset = 0) {
        return {s, hashString(s, offset)};
    }
    static uint32_t hashString(const std::string &s, uint32_t offset) {
        std::hash<std::string> h;
        return static_cast<uint32_t>(h(s)) + offset;
    }
};

struct IntKeyType {
    uint64_t slice;
    uint32_t hash_value;

    IntKeyType() = default;
    IntKeyType(uint64_t k, uint32_t h) {
        slice = k;
        hash_value = h;
    }
    static bool equal(const IntKeyType &a, const IntKeyType &b) {
        return a.hash_value == b.hash_value && a.slice == b.slice;
    }
    static size_t hash(const IntKeyType &a) {
        return static_cast<size_t>(a.hash_value);
    }
    static IntKeyType FromInt(uint64_t k, uint32_t offset = 0) {
        return {k, hashInt(k, offset)};
    }
    static uint32_t hashInt(uint64_t k, uint32_t offset) {
        std::hash<uint64_t> h;
        return static_cast<uint32_t>(h(k)) + offset;
    }
};

class ShardCache {
public:
    using Deleter = std::function<void(const KeyType &, void *value)>;
    using IntDeleter = std::function<void(const IntKeyType &, void *value)>;

    ShardCache() = default;
    virtual ~ShardCache() = default;
    virtual bool lookup(const KeyType &, std::function<void(void *)>) {
        return false;
    }
    virtual bool lookup(const IntKeyType &, std::function<void(void *)>) {
        return false;
    }
    virtual bool insert(const KeyType &, void *, size_t, Deleter ) {
        return false;
    }
    virtual bool insert(const IntKeyType &, void *, size_t, IntDeleter) {
        return false;
    }
    virtual void setCapacity(size_t capacity) = 0;
    virtual void setStrictCapacityLimit(bool strict_capacity_limit) = 0;
    virtual size_t getUsage() = 0;
    virtual size_t getPinnedUsage() = 0;
    virtual size_t getHashTableSize() = 0;
};

namespace impl {
template <typename KeyT, typename T>
class ShardCache : public ::base::ShardCache {
    using CacheHandle = typename T::CacheHandle;
    using DeleterT = std::function<void(const KeyT &, void *value)>;

public:
    ShardCache(size_t capacity, int num_shard_bits, bool strict_capacity_limit)
        : num_shard_bits_(num_shard_bits),
          capacity_(capacity),
          strict_capacity_limit_(strict_capacity_limit) {
        int num_shards = 1 << num_shard_bits;
        shards_ = new T[num_shards];
        setCapacity(capacity_);
        setStrictCapacityLimit(strict_capacity_limit_);
    }
    ~ShardCache() {
        delete[] shards_;
    }
    void setCapacity(size_t capacity) override {
        int num_shards = 1 << num_shard_bits_;
        const size_t per_shard = (capacity + (num_shards - 1)) / num_shards;
        std::lock_guard<std::mutex> l(capacity_mutex_);
        for (int s = 0; s < num_shards; s++) {
            getShard(s)->setCapacity(per_shard);
        }
        capacity_ = capacity;
    }
    void setStrictCapacityLimit(bool strict_capacity_limit) override {
        int num_shards = 1 << num_shard_bits_;
        std::lock_guard<std::mutex> l(capacity_mutex_);
        for (int s = 0; s < num_shards; s++) {
            getShard(s)->setStrictCapacityLimit(strict_capacity_limit);
        }
        strict_capacity_limit_ = strict_capacity_limit;
    }
    bool lookup(const KeyT &key, std::function<void(void *)> f) override {
        auto s = getShard(shard(key.hash_value));
        auto handle = s->lookup(key);
        if (handle == nullptr) {
            return false;
        }
        try {
            f(handle->value);
        } catch (...) {
        }
        s->release(handle);
        return true;
    }

    bool insert(const KeyT &key, void *value,
                size_t charge, DeleterT deleter) override {
        auto s = getShard(shard(key.hash_value));
        return s->insert(key, value, charge, deleter, nullptr);
    }

    size_t getUsage() override {
        size_t ret = 0;
        for (int i = 0; i < 1 << num_shard_bits_; i++) {
            ret += shards_[i].getUsage();
        }
        return ret;
    }

    size_t getPinnedUsage() override {
        size_t ret = 0;
        for (int i = 0; i < 1 << num_shard_bits_; i++) {
            ret += shards_[i].getPinnedUsage();
        }
        return ret;
    }

    size_t getHashTableSize() override {
        size_t ret = 0;
        for (int i = 0; i < 1 << num_shard_bits_; i++) {
            ret += shards_[i].getHashTableSize();
        }
        return ret;
    }

private:
    T *getShard(uint32_t offset) {
        return shards_ + offset;
    }
    uint32_t shard(uint32_t hash) {
        // Note, hash >> 32 yields hash in gcc, not the zero we expect!
        return (num_shard_bits_ > 0) ? (hash >> (32 - num_shard_bits_)) : 0;
    }

    int num_shard_bits_;
    mutable std::mutex capacity_mutex_;
    size_t capacity_;
    bool strict_capacity_limit_;
    T *shards_;
};
} // namespace impl
} // namespace base

#endif // BASE_SHARD_CACHE_H
