#include "service_state.h"
#include "log.h"

namespace base {
namespace impl {
ServiceState::ServiceState()
    : detached_{0}, running_{true} {}
bool ServiceState::isRunning() const {
    return running_.load();
}
void ServiceState::shutdown() {
    running_.store(false);
}
void ServiceState::wait() {
    while (detached_.load() != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
void ServiceState::shutdownAndWait() {
    ::base::stopPLogger();
    shutdown();
    wait();
}
void ServiceState::increaseDetached(size_t n) {
    detached_.fetch_add(n);
}
void ServiceState::decreaseDetached(size_t n) {
    detached_.fetch_sub(n);
}
} // namespace impl
impl::ServiceState *state() {
    static impl::ServiceState s;
    return &s;
}
} // namespace base
