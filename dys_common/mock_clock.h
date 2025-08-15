#ifndef BASE_MOCK_CLOCK_H
#define BASE_MOCK_CLOCK_H

#include <chrono>
#include <cstdint>

namespace base {
namespace testing {
class mock_clock {
public:
    using rep = uint64_t;
    using period = std::ratio<1l, 1000000000l>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<mock_clock>;

    static void advance(duration d) noexcept;
    static void reset_to_epoch() noexcept;
    static time_point now() noexcept;

private:
    mock_clock() = delete;
    ~mock_clock() = delete;
    mock_clock(mock_clock const &) = delete;

    static time_point now_us_;
    static const bool is_steady;
};
mock_clock::time_point mock_clock::now_us_;
const bool mock_clock::is_steady = false;
void mock_clock::advance(duration d) noexcept {
    now_us_ += d;
}
void mock_clock::reset_to_epoch() noexcept {
    now_us_ = time_point();
}
mock_clock::time_point mock_clock::now() noexcept {
    return now_us_;
}
} // namespace testing
} // namespace base

#endif // BASE_MOCK_CLOCK_H