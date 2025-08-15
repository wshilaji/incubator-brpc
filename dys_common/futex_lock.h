#ifndef _FUTEX_LOCK_H_
#define _FUTEX_LOCK_H_

#include <atomic>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>


class FutexLock final {
    enum: uint32_t {
        UNLOCK = 0,
        LOCKED = 1,
        WAITED = 2
    };
public:
    void wait() {
        uint32_t expect_val = UNLOCK;
        auto* atomic_lck = reinterpret_cast<std::atomic<uint32_t>*>(&_lck);
        if (!atomic_lck->compare_exchange_strong(expect_val, LOCKED)) {
            while (true) {
                if (expect_val == WAITED
                        || atomic_lck->compare_exchange_strong(expect_val, WAITED)) {
                    syscall(SYS_futex, &_lck, FUTEX_WAIT, WAITED, nullptr, nullptr, 0);
                }

                expect_val = UNLOCK;
                if (atomic_lck->compare_exchange_strong(expect_val, WAITED)) {
                    break;
                }
            }
        }
    }

    void wake() {
        auto* atomic_lck = reinterpret_cast<std::atomic<uint32_t>*>(&_lck);
        if (atomic_lck->fetch_sub(1) != LOCKED) {
            atomic_lck->store(UNLOCK);
            syscall(SYS_futex, &_lck, FUTEX_WAKE, 1, nullptr, nullptr, 0);
        }
    }

private:
    uint32_t _lck = UNLOCK;
};

/*
1. 调用前的检查
在进入内核前，futex 系统调用会先进行 用户态的原子检查：
if (*uaddr != val) {  // 这里检查 _lck 是否仍等于 WAITED
    return EAGAIN;    // 如果不等于，直接返回失败（避免无效等待）
}
如果 _lck 的值 不等于 WAITED，系统调用会 立即返回 EAGAIN，线程不会阻塞。
这是 futex 的 关键优化：避免不必要的内核切换。
2. 进入内核阻塞
如果检查通过（_lck == WAITED），内核会：将当前线程挂起：线程状态从 RUNNING 变为 SLEEPING。
线程被移出 CPU 调度队列。将线程加入等待队列：内核维护一个与 &_lck 地址关联的 等待队列。
当前线程被添加到这个队列中，等待唤醒。
3. 唤醒条件
线程会在以下情况下被唤醒：显式唤醒：
其他线程调用 FUTEX_WAKE（如 syscall(SYS_futex, &_lck, FUTEX_WAKE, 1)）。
内核从等待队列中移除线程，重新放入调度队列。
超时唤醒（如果指定了 timeout）：在 timespec 指定的时间到达后，线程会被自动唤醒。
信号中断：如果线程收到信号（如 SIGINT），系统调用可能提前返回 EINTR。
 */
class FutexCond final {
public:
    void lock() {
        _flck.wait();
        _fcnd.wait();
        _fcnd.wake();
    }
    void unlock() {
        _flck.wake();
    }
    void wait() {
        _flck.wait();
        _fcnd.wait();
        _flck.wake();
    }
    void wake() {
        _fcnd.wake();
    }

private:
    FutexLock _flck;
    FutexLock _fcnd;
};

template <class FX>
class FXGuard final {
public:
    explicit FXGuard(FX& fx)
        : _fx(fx) {
        _fx.wait();
    }
    ~FXGuard() {
        _fx.wake();
    }
private:
    FX& _fx;
};

#endif