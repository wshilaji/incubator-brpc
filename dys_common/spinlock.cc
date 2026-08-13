#pragma once
#include<atomic>
    
// CAS 直接写成宏不好写  compare_exchange_weak
class SpinLock {
    bool flag = false;
    void lock() {
        // 如果flag不是false会失败
        while(!lock_.compare_exchange_weak(flag, true)) { // flag必须是个左值
            flag = false; //expected会写成当前store里面都值 ，所以要在false一下
        } 
    }
    void unlock() noexcept {
        lock_.store(false);
    }
    std::atomic<bool>  lock_{false};
};


