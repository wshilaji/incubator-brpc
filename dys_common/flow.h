#ifndef BASE_FLOW_H
#define BASE_FLOW_H

#include <functional>
#include <utility>
#include <vector>

namespace base {
template <typename Context, typename Return>
class Then {
    using R = typename std::remove_reference<Return>::type;

public:
    Then(Context &ctx, R &&ret)
        : ctx(ctx), ret(std::forward<R>(ret)) {}
    template <typename F, typename... Args>
    auto then(F f, Args... args)
        -> Then<Context, typename std::result_of<F(Context &, R &&, Args...)>::type> {
        return {ctx, f(ctx, std::move(ret), std::forward<Args>(args)...)};
    }
    R result() {
        return ret;
    }

private:
    Context &ctx;
    R ret;
};

template <typename F, typename Context, typename... Args>
auto flow(F f, Context &ctx, Args... args)
    -> Then<Context, typename std::result_of<F(Context &, Args...)>::type> {
    return {ctx, f(ctx, std::forward<Args>(args)...)};
}

struct forEach {
    // if some Args is rvalue, use std::forward will cause multiple move
    // on same arg, which will be undefined behavior.
    template <typename Context, typename V, typename F, typename... Args>
    auto operator()(Context &ctx, std::vector<V> &&vs, F f, Args... args) {
        std::vector<decltype(f(ctx, std::declval<V>(), args...))> rs;
        for (auto &v : vs) {
            rs.emplace(std::end(rs), f(ctx, std::move(v), args...));
        }
        return rs;
    }
};
} // namespace base

#endif // BASE_FLOW_H