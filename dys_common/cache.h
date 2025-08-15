#ifndef BASE_CACHE_H
#define BASE_CACHE_H

#include "clock_cache.h"
#include "result.h"
#include "task.h"
#include <boost/optional.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace base {
using namespace std::chrono_literals;
template <typename K, typename V, class Hash = std::hash<K>, class Clock = std::chrono::system_clock>
class Cache {
    using fetcherType = std::function<Result<V>(const K &)>;

public:
    Cache(typename Clock::duration expire)
        : expire(expire) {}
    Result<V> get(K key, fetcherType f, typename Clock::duration d = 10ms) noexcept {
        std::unique_lock<std::mutex> dl(data_lock);
        auto it = data.find(key);
        if (it == std::end(data)) {
            it = data.emplace_hint(it, std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple());
        }
        dl.unlock();
        std::unique_lock<std::timed_mutex> rl(it->second.value_lock, std::defer_lock);
        if (!rl.try_lock_for(d)) {
            return Error("Cache: try data_lock timeout");
        }
        if (isExpired(it->second)) {
            try {
                it->second.value = f(key);
            } catch (...) {
                it->second.value = Error(std::current_exception());
            }
            it->second.update_time = Clock::now();
            it->second.init = true;
        }
        return it->second.value;
    }

private:
    struct Data {
        Result<V> value;
        typename Clock::time_point update_time;
        bool init = false;
        std::timed_mutex value_lock;
    };
    bool isExpired(const Data &d) const noexcept {
        return !d.init || (Clock::now() - d.update_time) >= expire;
    }
    std::unordered_map<K, Data, Hash> data;
    std::mutex data_lock;

    const typename Clock::duration expire;
};

template <typename V, typename Clock = std::chrono::system_clock>
class CacheWithTTL {
    using fetcherType = std::function<Result<V>(const std::string &)>;
    using intFetcherType = std::function<Result<V>(uint64_t &)>;
    using time_point = typename Clock::time_point;
    using duration = typename Clock::duration;
    struct Value {
        Result<V> data;
        time_point deadline;
    };

public:

    CacheWithTTL(size_t capacity, int num_shard_bits, duration ttl, bool strict_capacity_limit = false)
        : ttl_(ttl),
        cache_(NewClockCache(capacity, num_shard_bits, strict_capacity_limit)),
        int_cache_(NewIntClockCache(capacity, num_shard_bits, strict_capacity_limit)) 
    {}

    boost::optional<Result<V>> get(const std::string &key) noexcept {
        Result<V> ret;
        auto ver = version();
        if (lookup(key, ver, ret) || lookup(key, ver - 1, ret)) {
            return ret;
        }
        return boost::none;
    }

    boost::optional<Result<V>> get(uint64_t key) noexcept {
        Result<V> ret;
        auto ver = version();
        if (lookup(key, ver, ret) || lookup(key, ver - 1, ret)) {
            return ret;
        }
        return boost::none;
    }

    Result<V> get(const std::string &key, fetcherType f) noexcept {
        Result<V> ret;
        auto ver = version();
        if (lookup(key, ver, ret) || lookup(key, ver - 1, ret)) {
            return ret;
        }
        return fetchAndSet(key, f);
    }

    void set(const std::string &key, const Result<V> &res) noexcept {
        auto now = Clock::now();
        auto v = new Value{
            .data = res,
            .deadline = now + ttl_,
        };
        auto d = [](const KeyType &, void *value) {
            delete static_cast<Value *>(value);
        };
        cache_->insert(KeyType::FromStringRef(key, version(now)), v, 1, d);
    }

    void set(uint64_t key, const Result<V> &res) noexcept {
        auto now = Clock::now();
        auto v = new Value{
            .data = res,
            .deadline = now + ttl_,
        };
        auto d = [](const IntKeyType &, void *value) {
            delete static_cast<Value *>(value);
        };
        int_cache_->insert(IntKeyType::FromInt(key, version(now)), v, 1, d);
    }

    size_t getUsage() {
        return cache_->getUsage();
    }

    size_t getPinnedUsage() {
        return cache_->getPinnedUsage();
    }

    size_t getHashTableSize() {
        return cache_->getHashTableSize();
    }

private:
    bool lookup(const std::string &key, uint32_t ver, Result<V> &ret) {
        Value res;
        auto h = [&res](void *v) {
            res = *static_cast<Value *>(v);
        };
        auto ok = cache_->lookup(KeyType::FromStringRef(key, ver), h);
        if (!ok || Clock::now() >= res.deadline) {
            return false;
        }
        ret = std::move(res.data);
        return true;
    }

    bool lookup(uint64_t key, uint32_t ver, Result<V> &ret) {
        Value res;
        auto h = [&res](void *v) {
            res = *static_cast<Value *>(v);
        };
        auto ok = int_cache_->lookup(IntKeyType::FromInt(key, ver), h);
        if (!ok || Clock::now() >= res.deadline) {
            return false;
        }
        ret = std::move(res.data);
        return true;
    }

    Result<V> fetchAndSet(const std::string &key, fetcherType f) {
        auto now = Clock::now();
        auto v = new Value{
            .deadline = now + ttl_,
        };
        try {
            v->data = f(key);
        } catch (...) {
            v->data = Error(std::current_exception());
        }
        auto ret = v->data;
        auto d = [](const KeyType &, void *value) {
            delete static_cast<Value *>(value);
        };
        cache_->insert(KeyType::FromStringRef(key, version(now)), v, 1, d);
        return ret;
    }

    uint32_t version(time_point t = Clock::now()) {
        using namespace std::chrono;
        return duration_cast<seconds>(t.time_since_epoch()).count() /
               duration_cast<seconds>(ttl_).count();
    }

    const duration ttl_;
    std::shared_ptr<ShardCache> cache_;
    std::shared_ptr<ShardCache> int_cache_;
}; // namespace base

// real-time indexing cache
template <typename V, typename Clock = std::chrono::system_clock>
class RTICache {
    using fetcherType = std::function<Result<V>(const std::string &)>;
    using time_point = typename Clock::time_point;
    using duration = typename Clock::duration;
    struct Value {
        Result<V> data;
        time_point deadline;
    };

public:
    RTICache(size_t capacity, int num_shard_bits, duration ttl, bool strict_capacity_limit = false)
        : ttl_(ttl), cache_(NewClockCache(capacity, num_shard_bits, strict_capacity_limit)) {
    }
    boost::optional<Result<V>> get(const std::string &key) noexcept {
        Result<V> ret;
        //auto ver = version();
        if (lookup(key, 0, ret)) {
            return ret;
        }
        return boost::none;
    }

    // 缓存不过期
    boost::optional<Result<V>> getWithoutExpire(const std::string &key) noexcept {
        Result<V> ret;
        //auto ver = version();
        if (lookupWithoutExpire(key, 0, ret)) {
            return ret;
        }
        return boost::none;
    }

    // 添加动态ttl支持
    void set(const std::string &key, const Result<V> &res, const duration &ttl) noexcept {
        auto now = Clock::now();
        auto v = new Value{
            .data = res,
            .deadline = now + ttl,
        };
        auto d = [](const KeyType &, void *value) {
            delete static_cast<Value *>(value);
        };
        cache_->insert(KeyType::FromStringRef(key, 0), v, 1, d);
    }

    void set(const std::string &key, const Result<V> &res) noexcept {
        auto now = Clock::now();
        auto v = new Value {
            .data = res,
            .deadline = now + ttl_,
        };
        auto d = [](const KeyType &, void *value) {
            delete static_cast<Value *>(value);
        };
        cache_->insert(KeyType::FromStringRef(key, 0), v, 1, d);
    }

    size_t getUsage() {
        return cache_->getUsage();
    }

    size_t getPinnedUsage() {
        return cache_->getPinnedUsage();
    }

    size_t getHashTableSize() {
        return cache_->getHashTableSize();
    }

private:
    bool lookup(const std::string &key, uint32_t ver, Result<V> &ret) {
        Value res;
        auto h = [&res](void *v) {
            res = *static_cast<Value *>(v);
        };
        auto ok = cache_->lookup(KeyType::FromStringRef(key, ver), h);
        if (!ok || Clock::now() >= res.deadline) {
            return false;
        }
        ret = std::move(res.data);
        return true;
    }

    // 缓存不过期
    bool lookupWithoutExpire(const std::string &key, uint32_t ver, Result<V> &ret) {
        Value res;
        auto h = [&res](void *v) {
            res = *static_cast<Value *>(v);
        };
        auto ok = cache_->lookup(KeyType::FromStringRef(key, ver), h);
        if (!ok) {
            return false;
        }
        ret = std::move(res.data);
        return true;
    }

    uint32_t version(time_point t = Clock::now()) {
        using namespace std::chrono;
        return duration_cast<seconds>(t.time_since_epoch()).count() /
               duration_cast<seconds>(ttl_).count();
    }

    const duration ttl_;
    std::shared_ptr<ShardCache> cache_;
}; // namespace base

template <typename V>
class CacheWithCapacity {
    using Clock = std::chrono::system_clock;
    using fetcherType = std::function<Result<V>(const std::string &)>;

public:
    CacheWithCapacity(size_t capacity, int num_shard_bits,
                      Clock::duration ttl, TaskPool *pool)
        : version_(0), ttl_(ttl), pool_(pool),
          cache_(NewClockCache(capacity, num_shard_bits)) {}
    Result<V> get(const std::string &key, fetcherType f) noexcept {
        Result<V> ret;
        auto ver = version();
        if (lookup(key, ver, ret)) {
            return ret;
        }
        if (!deadlineReached() && lookup(key, ver - 1, ret)) {
            fetchAndSet(false, key, ver, f);
            return ret;
        }
        auto future = fetchAndSet(true, key, ver, f);
        if (!future.valid()) {
            return Error("CacheWithCapacity: add task to pool failed");
        }
        return future.get();
    }
    void refresh() {
        using namespace std::chrono;
        auto d = Clock::now() + ttl_;
        auto t = duration_cast<seconds>(d.time_since_epoch()).count();
        deadline_.store(t, std::memory_order_relaxed);
        version_.fetch_add(1, std::memory_order_relaxed);
    }
    size_t get_usage() {
        return cache_->getUsage();
    }
    size_t get_pinned_usage() {
        return cache_->getPinnedUsage();
    }
    size_t get_hashtable_size() {
        return cache_->getHashTableSize();
    }

private:
    bool lookup(const std::string &key, uint8_t ver, Result<V> &res) {
        auto h = [&res](void *v) {
            res = *static_cast<Result<V> *>(v);
        };
        return cache_->lookup(KeyType::FromStringRef(key, ver), h);
    }
    std::future<Result<V>> fetchAndSet(bool high, const std::string &key,
                                       uint8_t ver, fetcherType f) {
        return pool_->add(high, [=]() -> Result<V> {
            auto v = new Result<V>;
            size_t size = 0;
            try {
                *v = f(key);
                size = v->unwrap().size();
            } catch (...) {
                *v = Error(std::current_exception());
                size = v->errorString().length();
            }
            auto ret = *v;
            auto d = [](const KeyType &, void *value) {
                delete static_cast<Result<V> *>(value);
            };
            cache_->insert(KeyType::FromStringRef(key, ver), v, size + key.size(), d);
            return ret;
        });
    }
    bool deadlineReached() {
        using namespace std::chrono;
        auto now = duration_cast<seconds>(Clock::now().time_since_epoch()).count();
        return now > deadline_.load(std::memory_order_relaxed);
    }
    uint8_t version() {
        return version_.load(std::memory_order_relaxed);
    }

    std::atomic<uint8_t> version_;
    std::atomic<time_t> deadline_;
    const Clock::duration ttl_;
    std::shared_ptr<ShardCache> cache_;
    TaskPool *pool_;
};
} // namespace base

#endif // BASE_CACHE_H
