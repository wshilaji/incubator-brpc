#ifndef BASE_CLOCK_CACHE_H
#define BASE_CLOCK_CACHE_H

#include "result.h"
#include "shard_cache.h"
#include <boost/optional.hpp>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_priority_queue.h>
#include <bthread/bthread.h>
#include "log.h"

namespace base {
std::shared_ptr<ShardCache> NewClockCache(
    size_t capacity, int num_shard_bits, bool strict_capacity_limit = false);

std::shared_ptr<ShardCache> NewIntClockCache(
    size_t capacity, int num_shard_bits, bool strict_capacity_limit = false);

template<typename K, typename V>
class ClockCache {
public:
    using time_point = std::chrono::system_clock::time_point;
    using duration = std::chrono::system_clock::duration;
    struct HashValue {
        std::shared_ptr<V> data;
        time_point dead_line;
    };
    struct QueueValue {
        std::shared_ptr<K> data;
        time_point dead_line;
    };
    struct ComparValue {
        bool operator() (const QueueValue &a, const QueueValue &b) {
            return a.dead_line > b.dead_line;
        }
    };
    using HashTable = tbb::concurrent_hash_map<K, HashValue>;
    using PriorityQueue = tbb::concurrent_priority_queue<QueueValue, ComparValue>;

public:
    ClockCache() : tid_(INVALID_BTHREAD), stopped_(false), 
                   ttl_(std::chrono::seconds(0)) {}

    static ClockCache &Singleton() {
        static ClockCache<K, V> instance_;
        return instance_;
    }

    bool Init(const duration ttl) {
        ttl_ = ttl;
        if (bthread_start_background(&tid_, nullptr, RunClear, this) != 0) {
            ALOG(error, "Fail to init ClockCache, bthread start error");
            return false;
        }
        return true;
    }

    void Clear() {
        while (!stopped_) {
            QueueValue queue_value;
            while (queue_.try_pop(queue_value)) {
                if (queue_value.dead_line <= std::chrono::system_clock::now()) {
                    FindAndDelete(queue_value);
                } else {
                    queue_.push(queue_value);
                    break;
                }
            }
            bthread_usleep(1000 * 1000);
        }
    }

    static void *RunClear(void* arg) {
        auto clockcache = static_cast<ClockCache*>(arg);
        clockcache->Clear();
        return nullptr;
    }

    void stop() {
        bool stopped = false;
        if (!stopped_.compare_exchange_strong(stopped, true)) {
            return ;
        }
        bthread_stop(tid_);
        bthread_join(tid_, nullptr);
        tid_ = INVALID_BTHREAD;
        ALOG(info, "Succ stop ClockCache");
    }

    bool FindAndDelete(const QueueValue &key) {
        typename HashTable::accessor accessor;
        if (table_.find(accessor, *(key.data))) {
            table_.erase(accessor);
            return true;
        }
        return false;
    }

    std::shared_ptr<V> Get(const K &key) {
        typename HashTable::const_accessor accessor;
        if (!table_.find(accessor, key)) {
            return nullptr;
        }
        auto value = accessor->second.data;
        table_.erase(accessor);
        return value;
    }

    bool Set(const K &key, const std::shared_ptr<V> &value) {
        auto now = std::chrono::system_clock::now();
        HashValue hash_value {
            .data = value,
            .dead_line = now + ttl_,
        };
        QueueValue queue_value {
            .data = std::make_shared<K>(key),
            .dead_line = now + ttl_,
        };
        typename HashTable::value_type hash_pair(key, std::move(hash_value));
        table_.insert(std::move(hash_pair));
        queue_.push(std::move(queue_value));
        return true;
    }

private:
    duration ttl_;
    bthread_t tid_;
    std::atomic<bool> stopped_;
    HashTable table_;
    PriorityQueue queue_;
};

} // namespace base

#endif // BASE_CLOCK_CACHE_H
