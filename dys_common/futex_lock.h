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