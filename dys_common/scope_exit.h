#ifndef BASE_SCOPE_EXIT_H
#define BASE_SCOPE_EXIT_H

namespace base {
class ScopeExit {
public:
    ScopeExit(std::function<void(void)> f)
        : f_(f) {}
    ~ScopeExit(void) {
        f_();
    }

private:
    std::function<void(void)> f_;
};
} // namespace base

#endif // BASE_SCOPE_EXIT_H