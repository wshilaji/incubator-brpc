#ifndef BASE_TASK_H
#define BASE_TASK_H

#include <atomic>
#include <array>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace base {
struct Task {
    template <class Function>
    Task(Function &&f)
        : task(std::move(f)),
          time(std::chrono::system_clock::now()) {}

    std::packaged_task<void()> task;
    std::chrono::system_clock::time_point time;
};
class TaskPool {
public:
    TaskPool(size_t workers, size_t limit)
        : limit_(limit) {
        running_.store(true);
        concurrency.store(0);
        auto w = [this]() {
            std::unique_lock<std::mutex> l(m_);
            while (running_.load()) {
                bool empty = true;
                for (auto &ts : tasks_) {
                    if (ts.empty()) {
                        continue;
                    }
                    empty = false;
                    auto t = std::move(const_cast<Task &>(ts.front()));
                    ts.pop();
                    l.unlock();
                    concurrency.fetch_add(1);
                    t.task();
                    concurrency.fetch_sub(1);
                    l.lock();
                    break;
                }
                if (empty) {
                    cond_.wait(l);
                }
            }
        };
        for (size_t i = 0; i < workers; ++i) {
            threads_.emplace_back(w);
        }
    }
    ~TaskPool() {
        running_.store(false);
        cond_.notify_all();
        for (auto &th : threads_) {
            th.join();
        }
    }
    template <class Function, class... Args>
    auto add(Function &&f, Args &&... args) {
        return addWithPriority(1, std::forward<Function>(f), std::forward<Args>(args)...);
    }
    template <class Function, class... Args>
    auto add(bool high, Function &&f, Args &&... args) {
        auto pri = high ? 0 : 1;
        return addWithPriority(pri, std::forward<Function>(f), std::forward<Args>(args)...);
    }

    int getConcurrency() {
        return concurrency.load();
    }

private:
    mutable std::mutex m_;
    std::condition_variable cond_;
    std::atomic<bool> running_;
    std::array<std::queue<Task>, 2> tasks_;
    std::vector<std::thread> threads_;
    size_t limit_;
    std::atomic<int> concurrency;

    // https://cplusplus.github.io/LWG/issue2021
    template <class Function, class... Args>
    auto addWithPriority(uint8_t p, Function &&f, Args &&... args) {
        using resType = std::result_of_t<std::decay_t<Function>(std::decay_t<Args>...)>;
        std::packaged_task<resType()> t(std::bind(f, std::forward<Args>(args)...));
        auto ret = t.get_future();
        if (push_task(std::move(t), p) == false) {
            return decltype(ret){};
        }
        return ret;
    }
    template <class T>
    bool push_task(T &&t, uint8_t p) {
        std::unique_lock<std::mutex> l(m_);
        auto &ts = tasks_[p];
        if (ts.size() >= limit_) {
            return false;
        }
        ts.emplace(std::forward<T>(t));
        l.unlock();
        cond_.notify_one();
        return true;
    }
};
} // namespace base

#endif // BASE_TASK_H
