#include <iostream>
#include <list>
#include <unordered_set>
#include <sys/prctl.h>
#include "lru_hashmap.h"

namespace base {

struct CacheRegistryImpl {
  static CacheRegistryImpl* instance() {
    static CacheRegistryImpl singleton;
    return &singleton;
  }

  CacheRegistryImpl()
      : _stop(false)
      , _thr([this]() { this->evict_main(); }) { }

  ~CacheRegistryImpl() {
    _stop = true;
    _thr.join();
    // delete all instance
    for (auto& instance : _instance_list) {
      instance.del_func();
    }
    _instance_list.clear();
    _release_list.clear();
  }

  using func_t = std::function<void()>;
  struct Node {
    void* instance;
    func_t evict_func;
    func_t del_func;
  };

  void Add(void* instance, const func_t& evict_func, const func_t& del_func) {
    Node node = {instance, evict_func, del_func};
    std::lock_guard<std::mutex> guard(_mtx);
    _instance_list.emplace_back(std::move(node));
  }

  void Release(void* instance) {
    std::lock_guard<std::mutex> guard(_mtx);
    _release_list.emplace(instance);
  }

 private:
  static const size_t EVICT_CYCLE = 60*1000*1000;
  void evict_main() {
    ::prctl(PR_SET_NAME, "LruEvict");
    LOG(INFO) << "LruEvict thread start";
    while (!_stop) {
      try {
        auto ts = get_usec_ts();
        std::list<func_t> evict_vec, del_vec;
        {
          std::lock_guard<std::mutex> guard(_mtx);
          for (auto it = _instance_list.begin(); it != _instance_list.end(); ) {
            if (_release_list.count(it->instance) == 0) {
              evict_vec.emplace_back(it->evict_func);
              ++it;
            }
            else {
              del_vec.emplace_back(it->del_func);
              it = _instance_list.erase(it);
            }
          }
          _release_list.clear();
        }

        LOG(INFO) << "ins delete:#" << del_vec.size() << " evict:#" << evict_vec.size();
        // do delete
        for (auto & func : del_vec) {
          func();
        }
        // do evict
        for (auto & func : evict_vec) {
          func();
          ::usleep(10*1000);
          if (_stop) break;
        }

        ts = get_usec_ts() - ts;
        if (ts < EVICT_CYCLE) {
          ts = EVICT_CYCLE - ts;
          while (!_stop && ts > 0) {
            auto s = std::min(ts, uint64_t(100*1000));
            ts -= s;
            ::usleep(s);
          }
        }
      }
      catch (const std::exception & ex) {
        LOG(ERROR) << "lru evict thread exception:" << ex.what();
      }
    }
    LOG(INFO) << "LruEvict thread end";
  }

 private:
  bool        _stop;
  std::mutex  _mtx;
  std::thread _thr;

  std::list<Node> _instance_list;
  std::unordered_set<void *> _release_list;
};

void CacheRegistry::AddCache(
    void* instance,
    const std::function<void()>& evict_func,
    const std::function<void()>& del_func) {
  CacheRegistryImpl::instance()->Add(instance, evict_func, del_func);
}

void CacheRegistry::ReleaseCache(void* instance) {
  CacheRegistryImpl::instance()->Release(instance);
}

} // namespace base
