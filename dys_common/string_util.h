#ifndef BASE_STRING_UTIL_H
#define BASE_STRING_UTIL_H

#include <boost/utility/string_view.hpp>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <vector>

namespace base {
class Splitter {
public:
    Splitter(const char *ptr, size_t size, char delm, size_t cnt = std::numeric_limits<size_t>::max())
        : ptr(ptr), size(size), delm(delm), cnt(cnt) {};
    class Iterator;
    Iterator begin() const;
    Iterator end() const;
    template <typename T, typename F>
    void transform(std::vector<T> &v, F f) const;
    void transform(std::vector<boost::string_view> &v) const;
    void transform(std::unordered_set<std::string> &set, size_t reserve_size) const;

private:
    const char *ptr;
    size_t size;
    char delm;
    size_t cnt;
};

class Splitter::Iterator {
public:
    friend Splitter;
    Iterator &operator++();
    boost::string_view operator*();
    bool operator==(const Iterator &x) const;
    bool operator!=(const Iterator &x) const;

private:
    Iterator(const char *st, const char *ed, char delm, size_t cnt);
    const char *st, *nxt, *ed;
    char delm;
    size_t cnt;
};

template <typename T, typename S>
T stoi(const S &s) {
    T t{};
    for (auto c : s) {
        if (!std::isdigit(c)) {
            return t;
        }
        t *= 10;
        t += c - '0';
    }
    return t;
}
template <typename T, typename F>
void Splitter::transform(std::vector<T> &v, F f) const {
    v.resize(0);
    for (const auto &s : *this) {
        v.push_back(f(s));
    }
}
template <class Container, class Function>
std::string join(const Container &vs, const std::string &sep, const Function &f) {
    bool first = true;
    std::string ret;
    for (auto &v : vs) {
        if (!first) {
            ret += sep;
        }
        first = false;
        ret += f(v);
    }
    return ret;
};
template <class Container>
std::string join(const Container &vs, const std::string &sep) {
    return join(vs, sep, [](const auto &x) {
        return x;
    });
};
} // namespace base

#endif // BASE_STRING_UTIL_H
