#ifndef BASE_SERVICE_STATE_H
#define BASE_SERVICE_STATE_H

#include <atomic>
#include <functional>
#include <thread>

namespace base {
namespace impl {
class ServiceState {
public:
    ServiceState();
    bool isRunning() const;
    void shutdown();
    void wait();
    void shutdownAndWait();
    void increaseDetached(size_t n = 1);
    void decreaseDetached(size_t n = 1);
    template <class Function, class... Args>
    void wrapThread(Function &&func, Args &&... args) {
        auto f = std::bind(func, std::forward<Args>(args)...);
        increaseDetached();
        auto th = std::thread([this, f = std::move(f)] {
            f();
            decreaseDetached();
        });
        th.detach();
    }

private:
    std::atomic<size_t> detached_;
    std::atomic<bool> running_;
};
} // namespace impl
impl::ServiceState *state();
} // namespace base

#endif // BASE_SERVICE_STATE_H