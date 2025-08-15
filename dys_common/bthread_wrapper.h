#ifndef BASE_BTHREAD_WRAPPER_H_
#define BASE_BTHREAD_WRAPPER_H_
#include <bthread/bthread.h>

namespace base {
class Bthread final {
  public:
    Bthread() {}
    ~Bthread() { join(); }
    Bthread(const Bthread& other) = delete;
    Bthread& operator=(const Bthread& other) = delete;

    template <typename Fn, typename... Args>
    void CreateBackGroundTask(const bthread_attr_t* attr, Fn&& fn,
                              Args&&... args) {
        auto p_wrap_fn = new auto([=] { fn(args...); });
        auto call_back = [](void* ar) -> void* {
            auto f = reinterpret_cast<decltype(p_wrap_fn)>(ar);
            (*f)();
            delete f;
            return nullptr;
        };
        bthread_start_background(&th_, attr, call_back, (void*)p_wrap_fn);
        joinable_ = true;
    }

    void join() {
        if (joinable_) {
            bthread_join(th_, NULL);
            joinable_ = false;
        }
    }

    bool joinable() const noexcept { return joinable_; }

    const bthread_t& get_id() { return th_; }

  private:
    bthread_t th_;
    bool joinable_ = false;
};

#if 0

int main() {
    std::vector<BThread> workers(10);

    auto fn = [](int b) {
        cout<<"b="<<b<<endl;
    }
    for(auto&th : workers) {
        th.CreateBackGroundTask(nullptr, fn, 12);
    }
    for(auto&th : workers) {
        th.join();
    }
}

#endif
}  // namespace ground

#endif
