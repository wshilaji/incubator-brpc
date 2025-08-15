#ifndef BASE_UNIQ_TYPE_H
#define BASE_UNIQ_TYPE_H

#define UNIQ_TYPE(name, ...) using name = base::impl::UniqType<__VA_ARGS__, base::impl::hash(#name)>

#include <chrono>
#include <functional>

namespace base {
namespace impl {
template <typename T, size_t n>
class UniqType {
public:
    // 统一类型标记
    static constexpr size_t kSign = n;
    using RealType = T;

    UniqType()
        : val(T()) {}
    explicit UniqType(T v)
        : val(v) {}
    bool operator==(const UniqType<T, n> &x) const noexcept {
        return val == x.val;
    }
    bool operator!=(const UniqType<T, n> &x) const noexcept {
        return val != x.val;
    }
    T operator()() const noexcept {
        return val;
    }

private:
    T val;
};
constexpr size_t hash(const char *str) {
    return (*str == '\0') ? 5381 : (hash(str + 1) * 33 + *str) % (1 << 25);
}
} // namespace impl
} // namespace base

namespace std {
template <typename T, size_t n>
struct hash<base::impl::UniqType<T, n>> {
    std::size_t operator()(const base::impl::UniqType<T, n> &t) const noexcept {
        return std::hash<T>()(t());
    }
};
}

#endif // BASE_UNIQ_TYPE_H
