#ifndef BASE_LOADER_H
#define BASE_LOADER_H

#include <stdint.h>
#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "dual_buffer.h"
#include "log.h"
#include "map_util.h"
#include "redis.h"
#include "result.h"
#include "sync.h"

#include "butil/class_name.h"
#include "boost/iostreams/device/mapped_file.hpp"
#include "boost/optional.hpp"
#include "fmt/time.h"
#include "tbb/concurrent_hash_map.h"

namespace base {
std::pair<int64_t, int64_t> getMemoryUsed();
using LoadStatus = std::tuple<bool, std::string>;
using LoadContinuation = std::function<Result<LoadStatus>()>;
// status(bool, std::string) + vss used(int64_t) + rss used(int64_t)
using LoadMemStatus = std::tuple<bool, std::string, int64_t, int64_t>;
using LoadMemContinuation = std::function<Result<LoadMemStatus>()>;
using Versions = std::unordered_map<std::string, std::string>;
// struct T {
//   T(A);
//   LoadStatus status();
// };
// struct S {
//   bool load();
//   A get();
//   Versions versions();
// };

template <class Obj, class ...Objs>
class Dependencies{
public:
    Dependencies() {
        if (Dependencies<Obj>().exists && Dependencies<Objs...>().exists) {
            exists = true;
        }
    }
    bool exists = false;
};

template <class Obj>
class Dependencies<Obj>{
public:
    Dependencies() {
        exists = Obj().inited();
    }
    bool exists;
};

template <>
class Dependencies<void>{
public:
    bool exists = true;
};

template <typename T, typename S, typename L, typename D = Dependencies<void>, bool Retry = true>
class Loader {
public:
    using Ptr = std::shared_ptr<T>;
    Loader() = default;
    Ptr get() noexcept {
        return shared_.ptr.get();
    }
    bool inited() noexcept {
        return isInited_;
    }
    Result<LoadMemContinuation> load() noexcept {
        try {
            auto res = shared_.src->load();
            if (!res.ok()) {
                return res.error();
            }
            auto retry = (shared_.failed && Retry);
            if (!res.unwrap() && !retry) {
                return nullptr;
            }
        } catch (...) {
            return Error(std::current_exception());
        }
        return []() noexcept->Result<LoadMemStatus> {
            shared_.failed = true;
            shared_.load_time = 0;
            try {
                auto start_meminfo = getMemoryUsed();
                auto t = std::make_shared<T>(shared_.src->get());
                auto status = t->status();
                if (!std::get<0>(status)) {
                    return Error("checking data failed: " + std::get<1>(status));
                }
                auto end_meminfo = getMemoryUsed();

                shared_.failed = false;
                std::shared_ptr<T> hold(shared_.ptr.get());
                shared_.ptr.set(t);
                constexpr int32_t max_retry_count = 30;
                int32_t retry_count = 0;
                while (hold && hold.use_count() > 1 && retry_count++ < max_retry_count) {
                    constexpr int32_t safe_interval = 300;
                    std::this_thread::sleep_for(std::chrono::milliseconds(safe_interval));
                    ALOG(info, "swapping data ptr, waiting for worker to release. hold: %u, class name: %s", hold.use_count(), butil::class_name<T>());
                }
                if (retry_count >= max_retry_count) {
                    ALOG(warning, "dataloader release error, ref count: %u, class name: %s", hold.use_count(), butil::class_name<T>());
                }
                hold.reset();
                shared_.src->setVersion();
                shared_.src->release();
                
                using namespace std::chrono;
                shared_.load_time = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                isInited_ = true;
                
                auto vss_used = end_meminfo.first - start_meminfo.first;
                auto rss_used = end_meminfo.second - start_meminfo.second;
                return std::make_tuple(std::get<0>(status), std::get<1>(status), vss_used, rss_used);
            } catch (...) {
                return Error(std::current_exception());
            }
        };
    }
    void release() noexcept {
        shared_.ptr.set(nullptr);
    }
    template <typename... Args>
    static void setSource(Args &&... args) noexcept {
        shared_.src = std::make_unique<S>(std::forward<Args>(args)...);
    }
    Versions versions() const noexcept {
        auto verisons = shared_.src->versions();
        for (auto & it : verisons ) {
            using namespace std::chrono;
            auto tp = time_point<system_clock, seconds>(seconds(shared_.load_time));
            auto tt = system_clock::to_time_t(tp);
            it.second = it.second + "|" + fmt::format("{:%Y-%m-%d %H:%M:%S}", fmt::localtime(tt));
        }
        return verisons;
    }

    std::size_t getSourceSize() const noexcept {
        return shared_.src->getSize();
    }

    std::string getSourceType() const noexcept {
        return shared_.src->getType();
    }

private:
    struct SharedData {
        ThreadSafeSharePtr<T> ptr;
        std::unique_ptr<S> src;
        bool failed = false;
        int64_t load_time = 0;
    };
    static SharedData shared_;
    static std::atomic_bool isInited_;
};
template <typename T, typename S, typename L, typename D, bool R>
typename Loader<T, S, L, D, R>::SharedData Loader<T, S, L, D, R>::shared_{};

template <typename T, typename S, typename L, typename D, bool R>
std::atomic_bool Loader<T, S, L, D, R>::isInited_(false);

template <typename T, typename S, typename L, typename D = Dependencies<void>, bool Retry = true, int Interval = 1>
class DualBufferLoader {
public:
    DualBufferLoader() = default;
    const T *get() noexcept {
        return &data_.buffer.data();
    }
    const T &data() noexcept {
        return data_.buffer.data();
    }
    bool inited() noexcept {
        return data_.buffer.isInited();
    }
    Result<LoadMemContinuation> load() noexcept {
        if (!D().exists) {
            return Error("Dependencies file not found");
        }
        try {
            auto res = data_.src->load();
            if (!res.ok()) {
                return res.error();
            }
            auto retry = (data_.failed && Retry);
            if (!res.unwrap() && !retry) {
                return nullptr;
            }
        } catch (...) {
            return Error(std::current_exception());
        }
        return [this]() noexcept->Result<LoadMemStatus> {
            data_.failed = true;
            try {
                data_.buffer.mutableData()->reload(data_.src->get());
                auto status = data_.buffer.mutableData()->status();
                if (!std::get<0>(status)) {
                    return Error("checking data failed: " + std::get<1>(status));
                }
                data_.failed = false;
                data_.buffer.store();
                // TODO: add vss and rss info
                return std::make_tuple(std::get<0>(status), std::get<1>(status), 0, 0);
            } catch (...) {
                return Error(std::current_exception());
            }
        };
    }
    void release() noexcept {
        data_.buffer.release();
    }
    template <typename... Args>
    static void setSource(Args &&... args) noexcept {
        data_.src = std::make_unique<S>(std::forward<Args>(args)...);
    }
    Versions versions() const noexcept {
        return data_.src->versions();
    }

private:
    struct BufferData {
        impl::DualBuffer<T, L, Interval> buffer;
        std::unique_ptr<S> src;
        bool failed = false;
    };
    static BufferData data_;
};
template <typename T, typename S, typename L, typename D, bool R, int I>
typename DualBufferLoader<T, S, L, D, R, I>::BufferData DualBufferLoader<T, S, L, D, R, I>::data_{};

template <typename T, typename S>
class DynamicLoader {
    using ConcurrentMap = tbb::concurrent_hash_map<std::string, ThreadSafeSharePtr<T> *>;

public:
    using Ptr = std::shared_ptr<T>;
    DynamicLoader() = default;
    Ptr get(const std::string &name) const noexcept {
        typename ConcurrentMap::const_accessor it;
        if (!ptrs_.find(it, name)) {
            return nullptr;
        }
        return it->second->get();
    }
    std::vector<Ptr> get_all() const noexcept {
        std::vector<Ptr> data_list;
        for (auto& data : datas_) {
            data_list.emplace_back(data.ptr.get());
        }
        return data_list;
    }

    // ...insertion and deletion at either end of a deque never
    // invalidates pointers or references to the rest of the elements.
    void add(const std::string &name, S &&src) noexcept {
        datas_.emplace_back(name, std::move(src));
        ptrs_.insert(std::make_pair(name, &datas_.back().ptr));
        offset_ = 0;
    }
    void remove(const std::string &name) noexcept {
        ptrs_.erase(name);
        auto pred = [&](const Data &d) -> bool {
            if (name == d.name) {
                return true;
            }
            return false;
        };
        auto it = std::find_if(std::begin(datas_), std::end(datas_), pred);
        datas_.erase(it);
        offset_ = 0;
    }
    Result<LoadMemContinuation> load() noexcept {
        for (size_t i = 0; i < datas_.size(); ++i) {
            offset_ = (offset_ + datas_.size() - 1) % datas_.size();
            auto &d = datas_[offset_];
            try {
                auto res = d.src->load();
                if (!res.ok()) {
                    return res.error();
                }
                if (!res.unwrap()) {
                    continue;
                }
            } catch (...) {
                return Error(std::current_exception());
            }
            return [&d]() noexcept->Result<LoadMemStatus> {
                try {
                    d.load_time = 0;
                    auto t = std::make_shared<T>(d.src->get());
                    auto status = t->status();
                    if (!std::get<0>(status)) {
                        return Error("checking data failed: " + std::get<1>(status));
                    }
                    d.ptr.set(t);
                    d.src->setVersion();
                    using namespace std::chrono;
                    d.load_time = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                    // TODO: add vss and rss info
                    return std::make_tuple(std::get<0>(status), std::get<1>(status), 0, 0);
                } catch (...) {
                    return Error(std::current_exception());
                }
            };
        }
        return nullptr;
    }
    void release() noexcept {
        ptrs_.clear();
        datas_.clear();
    }
    Versions versions() const noexcept {
        Versions ret;
        for (auto &d : datas_) {
            for (auto &it : d.src->versions()) {
                using namespace std::chrono;
                auto tp = time_point<system_clock, seconds>(seconds(d.load_time));
                auto tt = system_clock::to_time_t(tp);
                ret[it.first] = it.second + "|" + fmt::format("{:%Y-%m-%d %H:%M:%S}", fmt::localtime(tt));
                ret[d.name] = it.second + "|" + fmt::format("{:%Y-%m-%d %H:%M:%S}", fmt::localtime(tt));
            }
        }
        return ret;
    }

private:
    struct Data {
        Data(const std::string &name, S &&s)
            : name(name), src(std::move(s)),load_time(0) {}
        std::string name;
        int64_t load_time;
        ThreadSafeSharePtr<T> ptr;
        boost::optional<S> src;
    };
    tbb::concurrent_hash_map<std::string, ThreadSafeSharePtr<T> *> ptrs_;
    std::deque<Data> datas_;
    size_t offset_ = 0;
};

template <typename S, typename Config>
struct WithConfig {
    WithConfig(S &&s, Config c)
        : source_(s), config_(std::move(c)) {}
    Result<bool> load() noexcept {
        return source_.load();
    }
    auto get() const noexcept {
        return std::make_tuple(source_.get(), config_);
    }
    
    void setVersion() noexcept {source_.setVersion();}
    
    Versions versions() const noexcept {
        return source_.versions();
    }

    std::size_t getSize() const noexcept {
        return source_.getSize();
    }

    std::string getType() const noexcept {
        return source_.getType();
    }

    void release() noexcept {
        source_.release();
    }

private:
    Config config_;
    S source_;
};
class FileSource {
public:
    FileSource(const std::string &path)
        : path(path){
             _version_ptr.set(std::make_shared<std::string>(path));
        }
    Result<bool> load() noexcept;
    void setVersion() noexcept;
    boost::iostreams::mapped_file_source get() const noexcept;
    Versions versions() const noexcept;
    std::size_t getSize() const noexcept;
    std::string getType() const noexcept;
    void release() noexcept;

private:
    const std::string path;
    ThreadSafeSharePtr<std::string> _version_ptr;
    std::chrono::system_clock::time_point mtime;
    boost::iostreams::mapped_file_source file;
};
class StrictlyConsistentWDModelSource {
public:
    StrictlyConsistentWDModelSource(const std::string &path)
        : path(path) {
            _version_ptr.set(std::make_shared<std::string>(path));
            size_t last_underline_pos = path.find_last_of("_");
            std::string path_prefix = path.substr(0, last_underline_pos);
            depend_files.push_back(path);
            depend_files.push_back(path_prefix + "_delta_params");
            std::chrono::system_clock::time_point tmp_mtime;
            depend_mtimes.resize(depend_files.size(), tmp_mtime);
            loaded = 0;
        }
    Result<bool> load() noexcept;
    std::string get() const noexcept;
    void setVersion() noexcept;
    Versions versions() const noexcept;
    std::size_t getSize() const noexcept;
    std::string getType() const noexcept;
    void release() noexcept;

private:
    // in the order of params, delta
    std::vector<std::string> depend_files;
    std::vector<std::chrono::system_clock::time_point> depend_mtimes;
    std::string path;
    ThreadSafeSharePtr<std::string> _version_ptr;
    std::chrono::system_clock::time_point mtime;
    // indicate whether the path has been loaded, if true, skip the full checkpoint file
    int loaded;
};
class FilePathSource {
public:
    FilePathSource(const std::string &path)
        : path(path){
            _version_ptr.set(std::make_shared<std::string>(path));
        }
    Result<bool> load() noexcept;
    std::string get() const noexcept;
    void setVersion() noexcept;
    Versions versions() const noexcept;
    std::size_t getSize() const noexcept;
    std::string getType() const noexcept;
    void release() noexcept;

private:
    const std::string path;
    ThreadSafeSharePtr<std::string> _version_ptr;
    std::chrono::system_clock::time_point mtime;
};

// for model source in galaxy
class ModelSource {
public:
    ModelSource(const std::string &model_name)
        : model_name(model_name) {
            _params_path = model_name + "_params";
            _feature_path = model_name + "_feature";
            _symbol_path = model_name + "_symbol";
            _params_version.set(std::make_shared<std::string>(_params_path));
            _feature_version.set(std::make_shared<std::string>(_feature_path));
            _symbol_version.set(std::make_shared<std::string>(_symbol_path));
        }
    Result<bool> load() noexcept;
    std::string get() const noexcept;
    void setVersion() noexcept;
    Versions versions() const noexcept;
    std::size_t getSize() const noexcept;
    std::string getType() const noexcept;
    void release() noexcept;

private:
    const std::string model_name;
    std::string _params_path, _feature_path, _symbol_path;
    ThreadSafeSharePtr<std::string> _params_version, _feature_version, _symbol_version;
    std::chrono::system_clock::time_point mtime; // use params mtime to update
};

// for model source in galaxy
class TFModelSource {
public:
    TFModelSource(const std::string &model_name)
        : model_name(model_name) {
            _params_path = model_name + "/wdctr.index";
            _feature_path = model_name + "/feature";
            _symbol_path = model_name + "/wdctr.meta";
            _model_version.set(std::make_shared<std::string>(model_name));
        }
    Result<bool> load() noexcept;
    std::string get() const noexcept;
    void setVersion() noexcept;
    Versions versions() const noexcept;
    std::size_t getSize() const noexcept;
    std::string getType() const noexcept;
    void release() noexcept;

private:
    const std::string model_name;
    std::string _params_path, _feature_path, _symbol_path;
    ThreadSafeSharePtr<std::string> _model_version;
    std::chrono::system_clock::time_point mtime; // use params mtime to update
};

// for local table source in galaxy
class LocalTableSource {
public:
    LocalTableSource(const std::string &table_name)
        : table_name(table_name) {
            _meta_path = table_name + "/meta.json";
            _table_version.set(std::make_shared<std::string>(table_name));
        }
    Result<bool> load() noexcept;
    std::string get() const noexcept;
    void setVersion() noexcept;
    Versions versions() const noexcept;
    std::size_t getSize() const noexcept;
    std::string getType() const noexcept;
    void release() noexcept;

private:
    const std::string table_name;
    std::string _meta_path;
    ThreadSafeSharePtr<std::string> _table_version;
    std::chrono::system_clock::time_point mtime; // use params mtime to update
};

class RedisSource {
    using clk = std::chrono::system_clock;

public:
    RedisSource(const std::string &, const std::string &db,
                const std::string &cmd, const clk::duration &timeout)
        : db(db), cmd(cmd), timeout(timeout) {}
    Result<bool> load() noexcept;
    RedisReply get() const noexcept;
    void setVersion() noexcept;
    Versions versions() const noexcept;
    std::size_t getSize() const noexcept;
    std::string getType() const noexcept;
    void release() noexcept;

private:
    const std::string db;
    const std::string cmd;
    const clk::duration timeout;
    RedisReply reply{Error{"RedisSource: not initialized"}};
    clk::time_point mtime;
};
class RegularSource {
    using clk = std::chrono::system_clock;

public:
    RegularSource(const std::string &resource, const clk::duration &timeout)
        : resource(resource), timeout(timeout) {}
    Result<bool> load() noexcept;
    bool get() const noexcept;
    Versions versions() const noexcept;
    void setVersion() noexcept;
    std::size_t getSize() const noexcept;
    std::string getType() const noexcept;
    void release() noexcept;

private:
    const std::string resource;
    const clk::duration timeout;
    clk::time_point mtime;
};

// "LoaderContext" encloses context information in loader registration phase,
// in order to allow individual registrations (in multiple CUs).
template <typename Manager>
class LoaderContext {
 public:
  // Getters.

  Manager* GetManager() const {
    return manager_;
  }

  const std::string& GetDataDir() const {
    return data_dir_;
  }

  // Setters.

  void SetManager(Manager* manager) {
    manager_ = manager;
  }

  void SetDataDir(const std::string& data_dir) {
    data_dir_ = data_dir;
  }

  // Proxies.

  std::chrono::seconds AsSeconds(ssize_t count) const {
    return std::chrono::seconds(count);
  }

  std::chrono::seconds AsMinutes(ssize_t count) const {
    // NOTE: converted to `std::chrono::seconds` intentionally to reduce
    //       unnecessary variants during template instantiation.
    return std::chrono::minutes(count);
  }

  // Others.

  void Unused() const {
    // NOTE: provided for better readability while eliminating warnings about
    //       unused parameters.
  }

  const char* CStr(const char* s) const {
    // NOTE: converting string literals to `const char*` explicitly to reduce
    //       unnecessary variants during template instantiation (otherwise,
    //       e.g. "orz" as a direct argument to deduce corresponding template
    //       parameter type results in an instantiation of something like
    //       `const char[4]` rather than `const char*` --- tons of different
    //       instantiations would be generated when there are multiple
    //       template parameters expecting string literals in various lengths,
    //       including implicit templating such as generic lambda expressions).
    //
    // NOTE: could be `static` technically.
    return s;
  }

 protected:
  Manager* manager_;  // NOTE: NOT OWNED.

  std::string data_dir_;
};

} // namespace base

#endif // BASE_LOADER_H
