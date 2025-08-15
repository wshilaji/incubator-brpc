#ifndef BASE_RESULT_H
#define BASE_RESULT_H

#include "uniq_type.h"
#include <boost/variant.hpp>
#include <string>
#include <utility>

namespace base {
std::string toString(const std::exception_ptr &e);
class Error {
public:
    explicit Error(std::string err)
        : err_(std::move(err)) {}
    explicit Error(const std::exception_ptr &e)
        : err_(toString(e)) {}
    Error()
        : err_("error: not initialized") {}
    std::string operator()() const {
        return err_;
    }

private:
    std::string err_;
};
namespace impl {
template <typename T>
class ResultImpl {
public:
    ResultImpl()
        : v{Error("Result: not initialized")} {}
    template <typename V>
    ResultImpl(const V &v)
        : v(v) {}
    bool operator==(const ResultImpl &r) const noexcept {
        return r.v == v;
    }
    bool operator!=(const ResultImpl &r) const noexcept {
        return r.v != v;
    }
    bool ok() const noexcept {
        return v.type() == typeid(T);
    }
    T const& unwrap() const noexcept {
        return boost::get<T>(v);
    }
    T& unwrap() noexcept {
        return boost::get<T>(v);
    }
    template <typename V>
    T unwrapOr(V &&d) const noexcept {
        if (!ok()) {
            return std::forward<V>(d);
        }
        return boost::get<T>(v);
    }
    Error error() const noexcept {
        return boost::get<Error>(v);
    }
    std::string errorString() const noexcept {
        return boost::get<Error>(v)();
    }

private:
    boost::variant<T, Error> v;
};
template <typename T>
bool operator==(const T &r1, const ResultImpl<T> &r2) noexcept {
    return r2 == r1;
}
template <typename T>
bool operator!=(const T &r1, const ResultImpl<T> &r2) noexcept {
    return r2 != r1;
}
template <typename T>
ResultImpl<T> wrapResult(T &&t);
template <typename T>
ResultImpl<T> wrapResult(ResultImpl<T> &&t);
} // namespace impl
// auto flatten ResultImpl<ResultImpl<T>> to ResultImpl<T>
template <typename T>
using Result = decltype(impl::wrapResult(std::declval<T>()));
} // namespace base

#endif // BASE_RESULT_H
