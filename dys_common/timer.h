#ifndef BASE_TIMER_UTIL_H
#define BASE_TIMER_UTIL_H

#include <base/log.h>            // for LOG_XXX
#include <boost/noncopyable.hpp> //
#include <chrono>                // for duration, clock::now
#include <memory>                // for memset
#include <stdarg.h> // for va_arg
#include <string>   // for std::string
#include <vector>

#if __cplusplus >= 201703L
#include <string_view>
#endif  // __cplusplus >= 201703L

namespace base {

template <size_t CAPACITY, bool STRICT = true>
class StringAppender final {
  static_assert(CAPACITY > 0, "Invalid `CAPACITY` parameter.");

 public:
  StringAppender() : next_(space_) { Advance(0); }

  size_t capacity() const { return CAPACITY; }

  const char* data() const { return space_; }

  size_t size() const { return next_ - space_; }

  int AppendV(const char* const format, va_list arguments) {
    const size_t available = capacity() - size();

    if (available == 0) {
      return 0;
    }

    int count = vsnprintf(next_, available, format, arguments);

    if (count < 0) {
      return count;
    }

    if (count >= available) {
      if (STRICT) {
        ALOG(fatal, "failed in `AppendV`: pending=%d; available=%lu.", count + 1, available);
      }

      count = available - 1;
    }

    return Advance(count);
  }

  int AppendF(const char* const format, ...) {
    va_list arguments;

    va_start(arguments, format);
    const int count = AppendV(format, arguments);
    va_end(arguments);

    return count;
  }

  int AppendS(const char* const space, const size_t count) {
    if (size() + count >= capacity()) {
      if (STRICT) {
        ALOG(fatal, "failed in `AppendS`: pending=%lu; available=%lu.", count + 1, capacity() - size());
      }

      return 0;
    }

    memcpy(next_, space, count);

    return Advance(count);
  }

  std::string ToString() const { return std::string(data(), size()); }

#if __cplusplus >= 201703L
  std::string_view ToStringView() const { return std::string_view(data(), size()); }
#endif  // __cplusplus >= 201703L

  // Aliases (keeping certain naming convention for compatibility).
  const char* c_str() const { return data(); }

  int appendf(const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    const int count = AppendV(format, arguments);
    va_end(arguments);

    return count;
  }

  int appendString(const char* space, size_t count) { return AppendS(space, count); }

  std::string toString() const { return ToString(); }

 private:
  size_t Advance(const size_t count) {
    next_ += count;

    *next_ = '\0';  // POST-CONDITION: `*next_ == '\0'`.

    return count;
  }

  char space_[CAPACITY];
  char* next_;
};

// to suggest compiler to optimize branch predictions
// but in fact cpu would also do the same thing
//
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

class TimeStatistics : boost::noncopyable {
public:
    TimeStatistics(size_t key_size) {
        _consumed.resize(key_size);
    }

    void record(size_t index, uint64_t time_consumed) {
        if (unlikely(index >= _consumed.size())) {
            ALOG(fatal, "timer_index[%d] exceeds count[%d]",
                 index, _consumed.size());
            return;
        }
        _consumed[index] += time_consumed;
    }

    uint64_t get(size_t index) const {
        if (unlikely(index >= _consumed.size())) {
            ALOG(fatal, "timer_index[%d] exceeds count[%d]",
                 index, _consumed.size());
            return 0;
        }
        return _consumed[index];
    }

    std::string toString(uint64_t measurement_us) const {
        StringAppender<1024> log_buf;
        for (size_t idx = 0; idx < _consumed.size(); ++idx) {
            // threshold is 1 ms
            if (_consumed[idx] > measurement_us) {
                log_buf.appendf("%d:%lu,", idx, _consumed[idx] / measurement_us);
            }
        }
        return log_buf.toString();
    }

private:
    std::vector<uint64_t> _consumed;
};

class TimerRecorder : boost::noncopyable {
public:
    using clk = std::chrono::system_clock;
    TimerRecorder(TimeStatistics &timer, uint64_t key)
        : _timer(timer), _key(key) {
        _cbegin = clk::now();
    }
    ~TimerRecorder() {
        // record by us, print by ms
        _cend = clk::now();
        _timer.record(_key, std::chrono::duration_cast<std::chrono::microseconds>(_cend - _cbegin).count());
    }

private:
    TimeStatistics &_timer;
    uint64_t _key;
    clk::system_clock::time_point _cbegin;
    clk::system_clock::time_point _cend;
};

} // namespace base
#define WITH_TIMER(timer, key, func) (base::TimerRecorder(timer, key), func)
#endif
