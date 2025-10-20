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
                } // 如果状态是 LOCKED，尝试设置为 WAITED，第一个等待线程 ， 后续等待线程：看到状态已经是 WAITED，直接进入等待

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
    }//dysNote如果有一个锁，自己本线程wait()了。 然后其他线程没有wait直接调用 wake()  。岂不是直接释放了 ？
    //前实现中 wake() 仅检查 fetch_sub(1) != LOCKED 就释放锁，无法区分：
    //合法释放（锁的持有者调用） 非法释放（其他线程随意调用）
    // 没有持有者标识：标准锁（如 std::mutex）会记录持有者线程，防止非法释放 非常重要！！
    /*
链接：https://leetcode.cn/problems/print-in-order/solutions/445416/c-hu-chi-suo-tiao-jian-bian-liang-xin-hao-liang-yi/
但实际上这种使用 mutex 的方法是 错误的，因为根据 C++ 标准，在一个线程尝试对一个 mutex 对象进行 unlock 操作时，mutex 对象的所有权必须在这个线程上；
也就是说，应该 由同一个线程来对一个 mutex 对象进行 lock 和 unlock 操作，否则会产生未定义行为。
     */


private:
    uint32_t _lck = UNLOCK;
};

/*
1. 调用前的检查
在进入内核前，futex 系统调用会先进行 用户态的原子检查：
if (*uaddr != val) {  // 这里检查 _lck 是否仍等于 WAITED
    return EAGAIN;    // 如果不等于，直接返回失败（避免无效等待）
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