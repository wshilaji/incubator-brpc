#include "sync.h"

namespace base {
std::mt19937 &randomGenerator() noexcept {
    thread_local std::mt19937 rg{std::random_device{}()};
    return rg;
}
} // namespace base