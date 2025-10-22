#pragma once

#include <sys/time.h>
#include <unistd.h>
#include <cstddef>
#include <ctime>

#include <thread>
#include <limits>
#include <algorithm>
#include <functional>
#include <atomic>
#include <memory>
#include <vector>
#include <unordered_map>

#include "butil/logging.h"
#include "butil/object_pool.h"
#include "butil/time.h"
#include "bvar/bvar.h"
#include "gflags/gflags.h"

namespace base {
DECLARE_uint32(lru_evict_timestamp);

static inline uint64_t get_usec_ts() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000000 + tp.tv_usec;
}

template <class T>
struct ObjectCounter {
  static ObjectCounter* instance() {
    static ObjectCounter x;
    return &x;
  }
  static inline void Get() {
    instance()->count.fetch_add(1, std::memory_order_relaxed);
  }
  static inline  void Ret() {
    instance()->count.fetch_sub(1, std::memory_order_relaxed);
  }
  static inline int Count() {
    return instance()->count.load(std::memory_order_relaxed);
  }
  std::atomic<int> count{0};
};

template <class T>
inline T* get_object() {
  ObjectCounter<T>::Get();
  return butil::get_object<T>();
}

template <class T>
inline void return_object(T* obj) {
  ObjectCounter<T>::Ret();
  obj->reset();
  butil::return_object<T>(obj);
}

struct CacheOption {
  CacheOption(const std::string &name, uint32_t capacity, uint64_t ttl, bool strict = false)
      : name(name), capacity(capacity), ttl(ttl), strict_evict(strict) {
    if (ttl <= 3) {
      LOG(FATAL) << "Invalid ttl" << ttl << "for cache:" << name;
    }
  }

  CacheOption() = delete;

  const std::string name;
  uint32_t capacity;
  uint64_t ttl; // seconds
  uint32_t clear_batch;
  uint32_t counter = 0;
  bool strict_evict = false;
};

// NOTE: use LruHashMapManager to new/delte LruHashMap instance
// auto lru_ptr = LruHashMapManager::instance()->NewLruHashMap<K, V>(opt.capacity, opt.ttl);
// LruHashMapManager::instance()->DeleteLruHashMap<K, V>(std::move(lru_ptr));
template<typename K, typename V, typename Hasher, typename ValueDeleter>
class LruHashMap {
  struct ValueNode;
  struct Node;

 public:
  friend struct LruHashMapManager;
  // NOTE: use LruHashMapManager to new/delte LruHashMap instance
  LruHashMap(const CacheOption& option);
  ~LruHashMap();

  size_t size() {
    return node_count_.load(std::memory_order_relaxed);
  }

  bool empty() {
    return size() == 0;
  }

  size_t capacity() {
    return cache_size_;
  }

  bool contains(const K& key) {
    return bool(Seek(key));
  }

  const V* Seek(const K& k) {
    bool expired = false;
    return Seek(k, &expired);
  }

  void SetTTL(uint64_t ttl) {
    ttl_us_ = ttl * 1000000;
  }

  void SetSkipEvict(bool skip_evict) {
    skip_evict_.store(skip_evict, std::memory_order_relaxed);
  }

  const V* SeekWithoutExpired(const K& key);
  const V* Seek(const K& key, bool* expired);
  const V* Add(const K& key, const V* value);

  void Erase(const K& key);

  void Evict();
  void EvictNodes();

  void clear() {
    // do nothing
    // LruHashMap clear() not be implemented!
  }

 private:
  const V* AddInternal(const K& key, const V* value);

  void ReturnNode(Node* node) {
    auto* value = node->value.exchange(nullptr, std::memory_order_relaxed);
    if (value) {
      return_object(value);
    }
    return_object(node);
  }

  void DelayedReleaseValue(ValueNode* value) {
    auto* head = delayed_release_value_.load(std::memory_order_relaxed);
    value->release_list_next = head;
    while (!delayed_release_value_.compare_exchange_weak(
               value->release_list_next, value, std::memory_order_relaxed));
  }

  void DelayedReturnNode(Node* node) {
    auto* value = node->value.exchange(nullptr, std::memory_order_relaxed);
    if (value) {
      DelayedReleaseValue(value);
    }
    return_object(node);
  }

  size_t ComputeBucket(const K& key) {
    return hash_(key) % bucket_size_;
  }

  struct ValueNode {
    ValueNode* release_list_next;
    const V* value = nullptr;
    uint64_t deadline;

    const V* v() const {
      return value;
    }

    void reset() {
      ValueDeleter()(const_cast<V*>(value));
      value = nullptr;
      release_list_next = nullptr;
    }

    void init_deadline(uint64_t ttl, uint64_t now) {
      uint64_t t = std::log(ttl);
      uint64_t r = ttl + ((int64_t)butil::fast_rand_less_than(t) - t / 2);
      deadline = now + r;
    }

    bool valid(uint64_t now) {
      return now < deadline;
    }
  };

  struct Node {
    std::atomic<uint64_t> ts;

    K key;
    std::atomic<ValueNode*> value;
    Node* next;
    void reset() {
      value.store(nullptr, std::memory_order_relaxed);
      next = nullptr;
    }
  };

  CacheOption _option;
  bvar::LatencyRecorder _latency;
  //brec::BvarCountRecorder _cache_hit, _cache_miss, _cache_expire;
  bvar::Status<uint64_t> _cached_size;

  size_t bucket_size_;
  size_t cache_size_;
  uint64_t ttl_us_;
  std::atomic<size_t> node_count_;
  std::vector<std::atomic<Node*>> buckets_;
  Hasher hash_;

  std::vector<Node*> evicted_nodes_;

  std::atomic<ValueNode*> delayed_release_value_;
  ValueNode* ready_to_release_value_;

  std::atomic<bool> skip_evict_;
  bool return_expired_value_ = false;
};

template<typename K, typename V, typename Hasher, typename Deleter>
LruHashMap<K, V, Hasher, Deleter>::LruHashMap(const CacheOption& option):
    _option(option),
    _latency("lru_" + option.name + "_latency"),
    // _cache_hit("lru", option.name + "_hit"),
    // _cache_miss("lru", option.name + "_miss"),
    // _cache_expire("lru", option.name + "_expire"),
    _cached_size("lru_" + option.name + "_cached_size", 0),

    bucket_size_(option.capacity),
    cache_size_(option.capacity),
    ttl_us_(option.ttl * 1000000),
    node_count_(0),
    buckets_(bucket_size_),
    evicted_nodes_(),
    delayed_release_value_{nullptr},
    ready_to_release_value_(nullptr),
    skip_evict_(false) {
    for (auto & bucket : buckets_) {
        bucket.store(nullptr, std::memory_order_relaxed);
    }
    LOG(INFO) << "new LruHashMap:" << _option.name
        << " capacity:" << option.capacity << " ttl:" << option.ttl;
}

template<typename K, typename V, typename Hasher, typename Deleter>
LruHashMap<K, V, Hasher, Deleter>::~LruHashMap() {
  for (auto& bucket : buckets_) {
    auto* head = bucket.load(std::memory_order_relaxed);
    while (head != nullptr) {
      auto* node = head;
      head = head->next;
      ReturnNode(node);
    }
  }
  for (auto* node : evicted_nodes_) {
    ReturnNode(node);
  }

  auto* vnode = delayed_release_value_.load(std::memory_order_relaxed);
  while (vnode != nullptr) {
    auto* tmp = vnode;
    vnode = vnode->release_list_next;
    return_object(tmp);
  }
  vnode = ready_to_release_value_;
  while (vnode != nullptr) {
    auto* tmp = vnode;
    vnode = vnode->release_list_next;
    return_object(tmp);
  }

  LOG(INFO) << "del LruHashMap:" << _option.name << " node:" << node_count_.load(std::memory_order_relaxed)
            << " object:" << ObjectCounter<Node>::Count() << ", " << ObjectCounter<ValueNode>::Count();
}

template<typename K, typename V, typename Hasher, typename Deleter>
const V* LruHashMap<K, V, Hasher, Deleter>::SeekWithoutExpired(const K& key) {

  auto current_ts = get_usec_ts();
  auto index = ComputeBucket(key);
  auto& bucket = buckets_[index];
  auto* node = bucket.load(std::memory_order_relaxed);
  while (node != nullptr) {
    if (node->key == key) {
      auto ts = node->ts.load(std::memory_order_relaxed);
      if (ts != 2 && ts != 0) {
        node->ts.compare_exchange_strong(
              ts, current_ts, std::memory_order_relaxed);
        auto* vnode = node->value.load(std::memory_order_relaxed);
        return vnode->v();
      } else {
        //_cache_miss.count();
        // 0: to-be-removed from linked list
        // 2: value-is-writing
        return nullptr;
      }
    }
    node = node->next;
  }
  //_cache_miss.count();
  return nullptr;
}

template<typename K, typename V, typename Hasher, typename Deleter>
const V* LruHashMap<K, V, Hasher, Deleter>::Seek(const K& key, bool* expired) {
  //bvar::BvarLatencyRecorderGuard c(_latency);
  *expired = false;

  auto current_ts = get_usec_ts();
  auto index = ComputeBucket(key);
  auto& bucket = buckets_[index];
  auto* node = bucket.load(std::memory_order_relaxed);
  while (node != nullptr) {
    if (node->key == key) {
      auto ts = node->ts.load(std::memory_order_relaxed);
      if (ts > 2) {
        auto* vnode = node->value.load(std::memory_order_relaxed);
        if (vnode->valid(current_ts)) {
          //_cache_hit.count();
          // we don't care about the success of this cas
          // the evict thread will keep the node for N seconds at least
          node->ts.compare_exchange_strong(
              ts, current_ts, std::memory_order_relaxed);
          auto* vnode = node->value.load(std::memory_order_relaxed);
          return vnode->v();
        } else {
          //_cache_expire.count();
          *expired = true;
          if (return_expired_value_) {    // return expired value
            auto* vnode = node->value.load(std::memory_order_relaxed);
            return vnode->v();
          }
          // mark it as expired
          node->ts.compare_exchange_strong(ts, 1, std::memory_order_relaxed);
          return nullptr;
        }
      } else if (ts == 1) {
        //_cache_expire.count();
        // 1: has been marked as expired
        *expired = true;
        if (return_expired_value_) {    // return expired value
          auto* vnode = node->value.load(std::memory_order_relaxed);
          return vnode->v();
        }
        return nullptr;
      } else {
        //_cache_miss.count();
        // 0: to-be-removed from linked list
        // 2: value-is-writing
        return nullptr;
      }
    }
    node = node->next;
  }
  //_cache_miss.count();
  return nullptr;
}

template<typename K, typename V, typename Hasher, typename Deleter>
const V* LruHashMap<K, V, Hasher, Deleter>::Add(const K& key, const V* value) {
  while (AddInternal(key, value) != value) continue;
  return value;
}

template<typename K, typename V, typename Hasher, typename Deleter>
const V* LruHashMap<K, V, Hasher, Deleter>::AddInternal(const K& key, const V* value) {
  const V* rc = nullptr;
  auto index = ComputeBucket(key);
  auto& bucket = buckets_[index];
  auto* head = bucket.load(std::memory_order_relaxed);
  Node* new_node = nullptr;
  auto current_ts = get_usec_ts();
  do {
    rc = nullptr;
    auto* node = head;
    while (node != nullptr) {
      if (node->key == key) {
        auto ts = node->ts.load(std::memory_order_relaxed);
        if (ts == 1 || ts > 2) {
          if (node->ts.compare_exchange_strong(ts, 2, std::memory_order_relaxed)) {
            // get the right to write value
            auto* vnode = get_object<ValueNode>();
            vnode->value = value;
            vnode->init_deadline(ttl_us_, current_ts);
            auto* prev = node->value.exchange(vnode, std::memory_order_relaxed);
            DelayedReleaseValue(prev);
            auto current_ts = get_usec_ts();
            node->ts.store(current_ts, std::memory_order_release);
            rc = value;
          } // else: other threads is writing this value
        } else if (ts == 0) {
          break; // add a new duplicated key
        } else { // else: value-is-writing(2)
          rc = nullptr;
        }

        if (new_node != nullptr) {
          new_node->value.load(std::memory_order_relaxed)->value = nullptr;
          DelayedReturnNode(new_node);
        }
        return rc;
      }
      node = node->next;
    }

    if (new_node == nullptr) {
      new_node = get_object<Node>();
      new_node->key = key;
      auto* vnode = get_object<ValueNode>();
      vnode->value = value;
      vnode->init_deadline(ttl_us_, current_ts);
      new_node->value.store(vnode, std::memory_order_release);
    }
    new_node->ts.store(current_ts, std::memory_order_relaxed);
    new_node->next = head;
    rc = new_node->value.load(std::memory_order_relaxed)->value;
  } while (!bucket.compare_exchange_weak(head, new_node, std::memory_order_acq_rel));
  node_count_.fetch_add(1, std::memory_order_relaxed);
  return rc;
}

template<typename K, typename V, typename Hasher, typename Deleter>
void LruHashMap<K, V, Hasher, Deleter>::Erase(const K& key) {
  auto index = ComputeBucket(key);
  auto& bucket = buckets_[index];
  auto* node = bucket.load(std::memory_order_relaxed);
  while (node != nullptr) {
    if (node->key == key) {
      auto ts = node->ts.load(std::memory_order_relaxed);
      if (ts > 2) {
        // set expired to erase
        while (ts > 2 && !node->ts.compare_exchange_weak(ts, 1, std::memory_order_release)) ;
        return;
      }
      // else 2(update) 1(expired) 0(del)
    }

    node = node->next;
  }
}

template<typename K, typename V, typename Hasher, typename Deleter>
void LruHashMap<K, V, Hasher, Deleter>::EvictNodes() {
  // free evicted nodes, delay free node, ensure the access is end
  for (auto* node : evicted_nodes_) {
    ReturnNode(node);
  }
  evicted_nodes_.clear();

  size_t node_count = node_count_.load(std::memory_order_relaxed);
  // DLOG(INFO) << "counter:" << _option.counter << " LruHashMap:" << _option.name
  //            << " evict node_count:" << node_count << " cache_size:" << cache_size_;

  // use a two pass method
  // 1st pass, find the evicted timestamp
  std::vector<uint64_t> nodes_ts;
  for (auto & bucket : buckets_) {
    auto* node = bucket.load(std::memory_order_relaxed);
    while (node != nullptr) {
      auto ts = node->ts.load(std::memory_order_relaxed);
      if (ts != 2) {
        nodes_ts.push_back(ts);
      }
      node = node->next;
    }
  }
  if (nodes_ts.empty()) {
    return;
  }
  std::sort(nodes_ts.begin(), nodes_ts.end());

  size_t evict_ts = 10;   // erase expired node
  if (_option.strict_evict) {
    evict_ts = get_usec_ts() -  ttl_us_;
  }
  int evict_count = -1;
  size_t traversed_node_count = nodes_ts.size();
  if (traversed_node_count > cache_size_) {
    evict_count = traversed_node_count - cache_size_;
    evict_ts = std::max(evict_ts, nodes_ts[evict_count]);
    //evict_ts = nodes_ts[evict_count];
  }
  else {
    if (nodes_ts[0] > evict_ts) {
      return;
    }
    // else exist expired node
  }
  // DLOG(INFO) << "counter:" << _option.counter << " LruHashMap:" << _option.name
  //            << " evict ts:" << evict_ts << " begin:" << nodes_ts[0]
  //            << " end:" << nodes_ts.back() << " count:" << evict_count;

  // 2nd pass, evict the node those ts < evict_ts
  for (auto & bucket : buckets_) {
    auto* head = bucket.load(std::memory_order_relaxed);
    auto* node = head;
    Node* prev = nullptr;
    while (node != nullptr) {
      auto ts = node->ts.load(std::memory_order_relaxed);
      // do not touch is-writing node
      if (ts != 2 && ts < evict_ts) {
        if (node->ts.compare_exchange_strong(ts, 0, std::memory_order_relaxed)) {
          if (node == head) {
            if (bucket.compare_exchange_strong(head, node->next, std::memory_order_relaxed)) {
              evicted_nodes_.push_back(node);
              head = node->next;
            } else {
              // find the node again
              auto* tmp = head;
              while (tmp != node) {
                prev = tmp;
                tmp = tmp->next;
              }
              continue;
            }
          } else {
            prev->next = node->next;
            evicted_nodes_.push_back(node);
            // keep prev unchanged
            node = node->next;
            continue;
          }
        }
      }

      prev = node;
      node = node->next;
    }
  }
  auto nc = node_count_.fetch_sub(evicted_nodes_.size(), std::memory_order_relaxed);
  _cached_size.set_value(nc);
}

template<typename K, typename V, typename Hasher, typename Deleter>
void LruHashMap<K, V, Hasher, Deleter>::Evict() {
  if (skip_evict_.load(std::memory_order_relaxed)) {
    LOG(INFO) << "LruHashMap:" << _option.name << " skip evict"
              << " node:" << node_count_.load(std::memory_order_relaxed)
              << " object:" << ObjectCounter<Node>::Count() << ", " << ObjectCounter<ValueNode>::Count();
    return;
  }

  _option.counter += 1;

  // release nodes
  EvictNodes();

  // release last epoch values
  auto* value_node = ready_to_release_value_;
  while (value_node) {
    auto* next = value_node->release_list_next;
    return_object(value_node);
    value_node = next;
  }
  // get current epoch values
  ready_to_release_value_ = delayed_release_value_.exchange(nullptr, std::memory_order_relaxed);

  LOG(INFO) << "counter:" << _option.counter << " LruHashMap:" << _option.name
            << " evict count:" << evicted_nodes_.size() << " node:" << node_count_.load(std::memory_order_relaxed)
            << " object:" << ObjectCounter<Node>::Count() << ", " << ObjectCounter<ValueNode>::Count();
}

class CacheRegistry {
 public:
  static CacheRegistry* instance() {
    static CacheRegistry singleton;
    return &singleton;
  }

  template <typename T>
  T* NewCache(const CacheOption& opt) {
    std::unique_ptr<T> cache(new T(opt));
    auto* c = cache.get();
    auto evict_func = [c]() {
      c->Evict();
    };
    auto delete_func = [c]() {
      delete c;
    };
    AddCache(c, evict_func, delete_func);
    return cache.release();
  }

  template <typename T>
  void DeleteCache(T* cache) {
    ReleaseCache(cache);
  }

 private:
  void AddCache(
      void* instance,
      const std::function<void()>& evict_func,
      const std::function<void()>& delete_func);
  void ReleaseCache(void* instance);
};

} // namespace base
