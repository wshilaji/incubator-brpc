#ifndef BASE_SYNC_H
#define BASE_SYNC_H

#include <atomic>
#include <boost/utility.hpp>
#include <future>
#include <memory>
#include <random>

namespace base {
template <typename T>
class ThreadSafeSharePtr {
public:
    ThreadSafeSharePtr() = default;
    ThreadSafeSharePtr(std::shared_ptr<T> p)
        : ptr{std::move(p)} {}
    std::shared_ptr<T> get() const noexcept {
        return std::atomic_load_explicit(&ptr, std::memory_order_relaxed);
    }
    void set(std::shared_ptr<T> p) noexcept {
        std::atomic_store_explicit(&ptr, p, std::memory_order_relaxed);
    }

private:
    std::shared_ptr<T> ptr;
};
template <typename T>
class SharedPromise {
public:
    SharedPromise()
        : data_{std::make_shared<Data>()} {}
    template <typename V>
    bool setValue(V &&val) {
        bool satisfied = false;
        if (!data_->satisfied_.compare_exchange_strong(satisfied, true)) {
            return false;
        }
        data_->promise_.set_value(std::forward<V>(val));
        return true;
    }
    std::future<T> getFuture() {
        return data_->promise_.get_future();
    }

private:
    struct Data {
        std::promise<T> promise_;
        std::atomic<bool> satisfied_;
    };
    std::shared_ptr<Data> data_;
};
template <typename T, typename Function>
void async(SharedPromise<T> p, Function f) {
    auto cb = [p, f]() mutable {
        p.setValue(f());
    };
    std::thread{cb}.detach();
}
std::mt19937 &randomGenerator() noexcept;
} // namespace base

#endif // BASE_SYNC_H