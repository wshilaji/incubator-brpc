#include "clock_cache.h"
#include "map_util.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <tbb/concurrent_hash_map.h>
#include <vector>

namespace base {
namespace impl {
using Deleter = ::base::ShardCache::Deleter;

// Cache entry meta data.
struct ClockCacheHandle {
    KeyType key;
    void *value;
    size_t charge;
    Deleter deleter;

    // Flags and counters associated with the cache handle:
    //   lowest bit: n-cache bit
    //   second lowest bit: usage bit
    //   the rest bits: reference count
    // The handle is unused when flags equals to 0. The thread decreases the count
    // to 0 is responsible to put the handle back to recycle_ and cleanup memory.
    std::atomic<uint32_t> flags;

    ClockCacheHandle() = default;
    ClockCacheHandle(const ClockCacheHandle &a) {
        *this = a;
    }
    ClockCacheHandle(const KeyType &k, void *v, Deleter del)
        : key(k), value(v), deleter(del) {}

    ClockCacheHandle &operator=(const ClockCacheHandle &a) {
        // Only copy members needed for deletion.
        key = a.key;
        value = a.value;
        deleter = a.deleter;
        return *this;
    }
};

struct CleanupContext {
    // List of values to be deleted, along with the key and deleter.
    std::vector<ClockCacheHandle> to_delete_value;

    // List of keys to be deleted.
    std::vector<const char *> to_delete_key;
};

// A cache shard which maintains its own CLOCK cache.
class ClockCacheShard {
public:
    // Hash map type.
    using CacheHandle = ClockCacheHandle;
    using HashTable = tbb::concurrent_hash_map<KeyType, CacheHandle *, KeyType>;

    ClockCacheShard();
    ~ClockCacheShard();

    // Interfaces
    void setCapacity(size_t capacity);
    void setStrictCapacityLimit(bool strict_capacity_limit);
    bool insert(const KeyType &key, void *value, size_t charge,
                Deleter deleter, CacheHandle **out_handle);
    CacheHandle *lookup(const KeyType &key);
    // If the entry in in cache, increase reference count and return true.
    // Return false otherwise.
    //
    // Not necessary to hold mutex_ before being called.
    bool ref(CacheHandle *handle);
    bool release(CacheHandle *handle, bool force_erase = false);
    void erase(const KeyType &key);
    bool eraseAndConfirm(const KeyType &key, CleanupContext *context);
    void eraseUnRefEntries();

    size_t getHashTableSize() {
        return table_.size();
    }

    size_t getUsage() {
        return usage_.load(std::memory_order_relaxed);
    }

    size_t getPinnedUsage() {
        return pinned_usage_.load(std::memory_order_relaxed);
    }

private:
    static const uint32_t kInCacheBit = 1;
    static const uint32_t kUsageBit = 2;
    static const uint32_t kRefsOffset = 2;
    static const uint32_t kOneRef = 1 << kRefsOffset;

    // Helper functions to extract cache handle flags and counters.
    static bool inCache(uint32_t flags) {
        return flags & kInCacheBit;
    }
    static bool hasUsage(uint32_t flags) {
        return flags & kUsageBit;
    }
    static uint32_t countRefs(uint32_t flags) {
        return flags >> kRefsOffset;
    }

    // Decrease reference count of the entry. If this decreases the count to 0,
    // recycle the entry. If set_usage is true, also set the usage bit.
    //
    // returns true if a value is erased.
    //
    // Not necessary to hold mutex_ before being called.
    bool unref(CacheHandle *handle, bool set_usage, CleanupContext *context);

    // Unset in-cache bit of the entry. Recycle the handle if necessary.
    //
    // returns true if a value is erased.
    //
    // Has to hold mutex_ before being called.
    bool unsetInCache(CacheHandle *handle, CleanupContext *context);

    // Put the handle back to recycle_ list, and put the value associated with
    // it into to-be-deleted list. It doesn't cleanup the key as it might be
    // reused by another handle.
    //
    // Has to hold mutex_ before being called.
    void recycleHandle(CacheHandle *handle, CleanupContext *context);

    // Delete keys and values in to-be-deleted list. Call the method without
    // holding mutex, as destructors can be expensive.
    void cleanup(const CleanupContext &context);

    // Examine the handle for eviction. If the handle is in cache, usage bit is
    // not set, and referece count is 0, evict it from cache. Otherwise unset
    // the usage bit.
    //
    // Has to hold mutex_ before being called.
    bool tryEvict(CacheHandle *value, CleanupContext *context);

    // Scan through the circular list, evict entries until we get enough capacity
    // for new cache entry of specific size. Return true if success, false
    // otherwise.
    //
    // Has to hold mutex_ before being called.
    bool evictFromCache(size_t charge, CleanupContext *context);

    CacheHandle *insert(
        const KeyType &key, void *value, size_t charge,
        Deleter deleter, bool hold_reference, CleanupContext *context);

    // Guards list_, head_, and recycle_. In addition, updating table_ also has
    // to hold the mutex, to avoid the cache being in inconsistent state.
    mutable std::mutex mutex_;

    // The circular list of cache handles. Initially the list is empty. Once a
    // handle is needed by insertion, and no more handles are available in
    // recycle bin, one more handle is appended to the end.
    //
    // We use std::deque for the circular list because we want to make sure
    // pointers to handles are valid through out the life-cycle of the cache
    // (in contrast to std::vector), and be able to grow the list (in contrast
    // to statically allocated arrays).
    std::deque<CacheHandle> list_;

    // Pointer to the next handle in the circular list to be examine for
    // eviction.
    size_t head_;

    // Recycle bin of cache handles.
    std::vector<CacheHandle *> recycle_;

    // Maximum cache size.
    std::atomic<size_t> capacity_;

    // Current total size of the cache.
    std::atomic<size_t> usage_;

    // Total un-released cache size.
    std::atomic<size_t> pinned_usage_;

    // Whether allow insert into cache if cache is full.
    std::atomic<bool> strict_capacity_limit_;

    // Hash table (tbb::concurrent_hash_map) for lookup.
    HashTable table_;
};
ClockCacheShard::ClockCacheShard()
    : head_(0), usage_(0), pinned_usage_(0), strict_capacity_limit_(false) {}

ClockCacheShard::~ClockCacheShard() {
    for (auto &handle : list_) {
        uint32_t flags = handle.flags.load(std::memory_order_relaxed);
        if (inCache(flags) || countRefs(flags) > 0) {
            if (handle.deleter != nullptr) {
                handle.deleter(handle.key, handle.value);
                delete[] handle.key.slice.data();
            }
        }
    }
}
void ClockCacheShard::recycleHandle(CacheHandle *handle, CleanupContext *context) {
    context->to_delete_key.push_back(handle->key.slice.data());
    context->to_delete_value.emplace_back(*handle);
    handle->key.slice.clear();
    handle->value = nullptr;
    handle->deleter = nullptr;
    recycle_.push_back(handle);
    usage_.fetch_sub(handle->charge, std::memory_order_relaxed);
}
void ClockCacheShard::cleanup(const CleanupContext &context) {
    for (const CacheHandle &handle : context.to_delete_value) {
        if (handle.deleter != nullptr) {
            handle.deleter(handle.key, handle.value);
        }
    }
    for (const char *key : context.to_delete_key) {
        delete[] key;
    }
}
bool ClockCacheShard::ref(CacheHandle *handle) {
    // CAS loop to increase reference count.
    uint32_t flags = handle->flags.load(std::memory_order_relaxed);
    while (inCache(flags)) {
        // Use acquire semantics on success, as further operations on the cache
        // entry has to be order after reference count is increased.
        if (handle->flags.compare_exchange_weak(flags, flags + kOneRef,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
            if (countRefs(flags) == 0) {
                // No reference count before the operation.
                pinned_usage_.fetch_add(handle->charge, std::memory_order_relaxed);
            }
            return true;
        }
    }
    return false;
}
bool ClockCacheShard::unref(CacheHandle *handle, bool set_usage, CleanupContext *context) {
    if (set_usage) {
        handle->flags.fetch_or(kUsageBit, std::memory_order_relaxed);
    }
    // Use acquire-release semantics as previous operations on the cache entry
    // has to be order before reference count is decreased, and potential cleanup
    // of the entry has to be order after.
    uint32_t flags = handle->flags.fetch_sub(kOneRef, std::memory_order_acq_rel);
    if (countRefs(flags) == 1) {
        // this is the last reference.
        pinned_usage_.fetch_sub(handle->charge, std::memory_order_relaxed);
        // Cleanup if it is the last reference.
        if (!inCache(flags)) {
            std::lock_guard<std::mutex> l(mutex_);
            recycleHandle(handle, context);
        }
    }
    return context->to_delete_value.size();
}
bool ClockCacheShard::unsetInCache(CacheHandle *handle, CleanupContext *context) {
    // Use acquire-release semantics as previous operations on the cache entry
    // has to be order before reference count is decreased, and potential cleanup
    // of the entry has to be order after.
    uint32_t flags = handle->flags.fetch_and(~kInCacheBit, std::memory_order_acq_rel);
    // Cleanup if it is the last reference.
    if (inCache(flags) && countRefs(flags) == 0) {
        recycleHandle(handle, context);
    }
    return context->to_delete_value.size();
}
bool ClockCacheShard::tryEvict(CacheHandle *handle, CleanupContext *context) {
    uint32_t flags = kInCacheBit;
    if (handle->flags.compare_exchange_strong(
            flags, 0, std::memory_order_acquire, std::memory_order_relaxed)) {
        table_.erase(handle->key);
        recycleHandle(handle, context);
        return true;
    }
    handle->flags.fetch_and(~kUsageBit, std::memory_order_relaxed);
    return false;
}
bool ClockCacheShard::evictFromCache(size_t charge, CleanupContext *context) {
    size_t usage = usage_.load(std::memory_order_relaxed);
    size_t capacity = capacity_.load(std::memory_order_relaxed);
    if (usage == 0) {
        return charge <= capacity;
    }
    size_t new_head = head_;
    bool second_iteration = false;
    while (usage + charge > capacity) {
        if (tryEvict(&list_[new_head], context)) {
            usage = usage_.load(std::memory_order_relaxed);
        }
        new_head = (new_head + 1 >= list_.size()) ? 0 : new_head + 1;
        if (new_head == head_) {
            if (second_iteration) {
                return false;
            } else {
                second_iteration = true;
            }
        }
    }
    head_ = new_head;
    return true;
}
void ClockCacheShard::setCapacity(size_t capacity) {
    CleanupContext context;
    {
        std::lock_guard<std::mutex> l(mutex_);
        capacity_.store(capacity, std::memory_order_relaxed);
        evictFromCache(0, &context);
    }
    cleanup(context);
}
void ClockCacheShard::setStrictCapacityLimit(bool strict_capacity_limit) {
    strict_capacity_limit_.store(strict_capacity_limit, std::memory_order_relaxed);
}
ClockCacheShard::CacheHandle *ClockCacheShard::insert(
    const KeyType &key, void *value, size_t charge,
    Deleter deleter, bool hold_reference, CleanupContext *context) {
    std::lock_guard<std::mutex> l(mutex_);
    bool success = evictFromCache(charge, context);
    bool strict = strict_capacity_limit_.load(std::memory_order_relaxed);
    if (!success && (strict || !hold_reference)) {
        context->to_delete_key.push_back(key.slice.data());
        if (!hold_reference) {
            context->to_delete_value.emplace_back(key, value, deleter);
        }
        return nullptr;
    }
    // Grab available handle from recycle bin. If recycle bin is empty, create
    // and append new handle to end of circular list.
    CacheHandle *handle = nullptr;
    if (!recycle_.empty()) {
        handle = recycle_.back();
        recycle_.pop_back();
    } else {
        list_.emplace_back();
        handle = &list_.back();
    }
    // Fill handle.
    handle->key = key;
    handle->value = value;
    handle->charge = charge;
    handle->deleter = deleter;
    uint32_t flags = hold_reference ? kInCacheBit + kOneRef : kInCacheBit;
    handle->flags.store(flags, std::memory_order_relaxed);
    HashTable::accessor accessor;
    if (table_.find(accessor, key)) {
        CacheHandle *existing_handle = accessor->second;
        table_.erase(accessor);
        unsetInCache(existing_handle, context);
    }
    table_.insert(HashTable::value_type(key, handle));
    if (hold_reference) {
        pinned_usage_.fetch_add(charge, std::memory_order_relaxed);
    }
    usage_.fetch_add(charge, std::memory_order_relaxed);
    return handle;
}
bool ClockCacheShard::insert(const KeyType &key, void *value, size_t charge,
                             Deleter deleter, CacheHandle **out_handle) {
    CleanupContext context;
    HashTable::accessor accessor;
    char *key_data = new char[key.slice.size()];
    memcpy(key_data, key.slice.data(), key.slice.size());
    auto slice = boost::string_view(key_data, key.slice.size());
    KeyType key_copy{slice, key.hash_value};
    CacheHandle *handle = insert(key_copy, value, charge, deleter,
                                 out_handle != nullptr, &context);
    bool s = true;
    if (out_handle != nullptr) {
        if (handle == nullptr) {
            s = false;
        } else {
            *out_handle = handle;
        }
    }
    cleanup(context);
    return s;
}
ClockCacheShard::CacheHandle *ClockCacheShard::lookup(const KeyType &key) {
    HashTable::const_accessor accessor;
    if (!table_.find(accessor, key)) {
        return nullptr;
    }
    CacheHandle *handle = accessor->second;
    accessor.release();
    // ref() could fail if another thread sneak in and evict/erase the cache
    // entry before we are able to hold reference.
    if (!ref(handle)) {
        return nullptr;
    }
    // Double check the key since the handle may now representing another key
    // if other threads sneak in, evict/erase the entry and re-used the handle
    // for another cache entry.
    if (!KeyType::equal(handle->key, key)) {
        CleanupContext context;
        unref(handle, false, &context);
        // It is possible unref() delete the entry, so we need to cleanup.
        cleanup(context);
        return nullptr;
    }
    return handle;
}
bool ClockCacheShard::release(CacheHandle *handle, bool force_erase) {
    CleanupContext context;
    bool erased = unref(handle, true, &context);
    if (force_erase && !erased) {
        erased = eraseAndConfirm(handle->key, &context);
    }
    cleanup(context);
    return erased;
}
void ClockCacheShard::erase(const KeyType &key) {
    CleanupContext context;
    eraseAndConfirm(key, &context);
    cleanup(context);
}
bool ClockCacheShard::eraseAndConfirm(const KeyType &key, CleanupContext *context) {
    std::lock_guard<std::mutex> l(mutex_);
    HashTable::accessor accessor;
    bool erased = false;
    if (table_.find(accessor, key)) {
        CacheHandle *handle = accessor->second;
        table_.erase(accessor);
        erased = unsetInCache(handle, context);
    }
    return erased;
}
void ClockCacheShard::eraseUnRefEntries() {
    CleanupContext context;
    {
        std::lock_guard<std::mutex> l(mutex_);
        table_.clear();
        for (auto &handle : list_) {
            unsetInCache(&handle, &context);
        }
    }
    cleanup(context);
}
} // namespace impl
std::shared_ptr<ShardCache> NewClockCache(
    size_t capacity, int num_shard_bits, bool strict_capacity_limit) {
    return std::make_shared<::base::impl::ShardCache<KeyType, impl::ClockCacheShard>>(
        capacity, num_shard_bits, strict_capacity_limit);
}
} // namespace base
