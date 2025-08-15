#ifndef BASE_CIRCUIT_BREAKER_H
#define BASE_CIRCUIT_BREAKER_H

#include "result.h"
#include <chrono>
#include <functional>
#include <mutex>
#include <queue>

namespace base {
namespace impl {
template <class Clock>
class CircuitBreakerImpl {
public:
    CircuitBreakerImpl(typename Clock::duration t, uint32_t n)
        : timeThreshhold(t), errorThreshhold(n) {}
    template <typename T, typename... Args>
    Result<T> run(std::function<T(Args...)> f, Args &&... args) noexcept {
        if (isClosed()) {
            return Error("CircuitBreaker: threshhold reached");
        }
        auto ret = f(std::forward<Args>(args)...);
        if (isOK(ret)) {
            success();
        } else {
            fail();
        }
        return ret;
    }

private:
    void fail() noexcept {
        std::lock_guard<std::mutex> lock(m);
        errors.push(Clock::now());
        recovering = false;
    }
    void success() noexcept {
        std::lock_guard<std::mutex> lock(m);
        auto now = Clock::now();
        while (!errors.empty() && now - errors.front() >= timeThreshhold) {
            errors.pop();
        }
        recovering = false;
    }
    bool isClosed() const noexcept {
        std::lock_guard<std::mutex> lock(m);
        if (errors.size() <= errorThreshhold) {
            return false;
        }
        if (Clock::now() - errors.back() < timeThreshhold || recovering) {
            return true;
        }
        recovering = true;
        return false;
    }

    std::queue<typename Clock::time_point> errors;
    template <typename T>
    static bool isOK(T &&t) {
        return t.ok();
    }
    template <typename T>
    static bool isOK(T *t) {
        return t->ok();
    }

    mutable std::mutex m;
    mutable bool recovering = false;
    const typename Clock::duration timeThreshhold;
    const uint32_t errorThreshhold;
};
} // namespace impl
using CircuitBreaker = impl::CircuitBreakerImpl<std::chrono::system_clock>;
} // namespace base

#endif // BASE_CIRCUIT_BREAKER_H