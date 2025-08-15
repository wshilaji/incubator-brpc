#ifndef BASE_OBJ_POOL_H
#define BASE_OBJ_POOL_H

#include "result.h"
#include <functional>
#include <limits>
#include <mutex>
#include <queue>
#include <memory>

namespace base {
template <typename T, typename Deleter = std::default_delete<T>>
class ObjectPool {
    using ObjectPtr = std::unique_ptr<T, Deleter>;

public:
    explicit ObjectPool(std::function<ObjectPtr()> f)
        : newObject{f} {}
    void SetLimit(size_t limit) noexcept {
        if (count != 0) {
            abort();
        }
        this->limit = limit;
    }
    void PreAllocate(size_t s) noexcept {
        if (s > limit) {
            s = limit;
        }
        std::vector<ObjectPtr> objVec;
        while (s--) {
            objVec.push_back(New());
        }
        for (auto &obj : objVec) {
            Delete(std::move(obj));
        }
    }
    ObjectPtr New() noexcept {
        std::unique_lock<std::mutex> lock(m);
        if (count > limit) {
            return nullptr;
        }
        ++count;
        if (q.empty()) {
            lock.unlock();
            return newObject();
        }
        auto obj = std::move(q.front());
        q.pop();
        return obj;
    }
    void Delete(ObjectPtr &&obj) noexcept {
        std::lock_guard<std::mutex> lock(m);
        q.push(std::move(obj));
        --count;
    }
    void Drop() noexcept {
        std::lock_guard<std::mutex> lock(m);
        --count;
    }
    size_t TotalCount() noexcept {
        std::lock_guard<std::mutex> lock(m);
        return count + q.size();
    }

private:
    std::function<ObjectPtr()> newObject;
    std::queue<ObjectPtr> q;
    size_t limit = std::numeric_limits<size_t>::max();
    size_t count = 0;
    std::mutex m;
};
} // namespace base

#endif // BASE_OBJ_POOL_H
