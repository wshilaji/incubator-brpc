#ifndef BASE_LIMITER_H
#define BASE_LIMITER_H

#include "result.h"
#include <atomic>

namespace base {
template <typename T = void>
class Limiter {
public:
    Limiter(size_t concurrency) {
        if (running_.load() > concurrency) {
            error_ = Error{"Limiter: concurrency limit reached"};
            return;
        }
        running_.fetch_add(1);
    }
    ~Limiter() {
        if (!ok()) {
            return;
        }
        running_.fetch_sub(1);
    }
    bool ok() const {
        if (error_ == boost::none) {
            return true;
        }
        return false;
    }
    Error error() const {
        if (ok()) {
            return {};
        }
        return error_.get();
    }
    std::string errorString() const {
        return error()();
    }

private:
    boost::optional<Error> error_;
    static std::atomic<size_t> running_;
};
template <typename T>
std::atomic<size_t> Limiter<T>::running_{};
} // namespace base

#endif // BASE_LIMITER_H