// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// bthread - An M:N threading library to make applications more concurrent.

// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BTHREAD_WORK_STEALING_QUEUE_H
#define BTHREAD_WORK_STEALING_QUEUE_H

#include "butil/macros.h"
#include "butil/atomicops.h"
#include "butil/logging.h"

namespace bthread {

template <typename T>
class WorkStealingQueue {
public:
    WorkStealingQueue()
        : _bottom(1)
        , _capacity(0)
        , _buffer(NULL)
        , _top(1) {
    }

    ~WorkStealingQueue() {
        delete [] _buffer;
        _buffer = NULL;
    }

    int init(size_t capacity) {
        if (_capacity != 0) {
            LOG(ERROR) << "Already initialized";
            return -1;
        }
        if (capacity == 0) {
            LOG(ERROR) << "Invalid capacity=" << capacity;
            return -1;
        }
        if (capacity & (capacity - 1)) {
            LOG(ERROR) << "Invalid capacity=" << capacity
                       << " which must be power of 2";
            return -1;
        }
        _buffer = new(std::nothrow) T[capacity];
        if (NULL == _buffer) {
            return -1;
        }
        _capacity = capacity;
        return 0;
    }

    // Push an item into the queue.
    // Returns true on pushed.
    // May run in parallel with steal().
    // Never run in parallel with pop() or another push().
    bool push(const T& x) {
        const size_t b = _bottom.load(butil::memory_order_relaxed);
        const size_t t = _top.load(butil::memory_order_acquire);
        if (b >= t + _capacity) { // Full queue.
            return false;
        }
        _buffer[b & (_capacity - 1)] = x;
        _bottom.store(b + 1, butil::memory_order_release);
        return true;
    }

    // Pop an item from the queue.
    // Returns true on popped and the item is written to `val'.
    // May run in parallel with steal().
    // Never run in parallel with push() or another pop().
    bool pop(T* val) {
        const size_t b = _bottom.load(butil::memory_order_relaxed);
        size_t t = _top.load(butil::memory_order_relaxed);
        if (t >= b) {
            // fast check since we call pop() in each sched.
            // Stale _top which is smaller should not enter this branch.
            return false;
        }
        const size_t newb = b - 1;
        _bottom.store(newb, butil::memory_order_relaxed);
        butil::atomic_thread_fence(butil::memory_order_seq_cst); 
        // 编译器的优化 指令顺序会变，但是只在单线程的角度去思考。如果多线程指令重拍会有问题
        // atomic_write_barrier()实现 __asm("":::"memory") 嵌入汇编代码的方式加了一个内存屏障
        // mfence  ; 全内存屏障（适用于 seq_cst）
        // __asm volatile ("mfence" ::: "memory")
        t = _top.load(butil::memory_order_relaxed);
        if (t > newb) {
            _bottom.store(b, butil::memory_order_relaxed);
            return false;
        }
        *val = _buffer[newb & (_capacity - 1)];
        if (t != newb) {
            return true;
        }
        // Single last element, compete with steal()
        const bool popped = _top.compare_exchange_strong(
            t, t + 1, butil::memory_order_seq_cst, butil::memory_order_relaxed);
        _bottom.store(b, butil::memory_order_relaxed);
        return popped;
    }

    // Steal one item from the queue.
    // Returns true on stolen.
    // May run in parallel with push() pop() or another steal().
    bool steal(T* val) {
        size_t t = _top.load(butil::memory_order_acquire);
        size_t b = _bottom.load(butil::memory_order_acquire);
        if (t >= b) {
            // Permit false negative for performance considerations.
            return false;
        }
        do {
            butil::atomic_thread_fence(butil::memory_order_seq_cst);
            b = _bottom.load(butil::memory_order_acquire);
            if (t >= b) {
                return false;
            }
            *val = _buffer[t & (_capacity - 1)];
        } while (!_top.compare_exchange_strong(t, t + 1,
                                               butil::memory_order_seq_cst,
                                               butil::memory_order_relaxed));
        return true;
    }

    size_t volatile_size() const {
        const size_t b = _bottom.load(butil::memory_order_relaxed);
        const size_t t = _top.load(butil::memory_order_relaxed);
        return (b <= t ? 0 : (b - t));
    }

    size_t capacity() const { return _capacity; }

private:
    // Copying a concurrent structure makes no sense.
    DISALLOW_COPY_AND_ASSIGN(WorkStealingQueue);

    butil::atomic<size_t> _bottom;
    size_t _capacity;
    T* _buffer;
    BAIDU_CACHELINE_ALIGNMENT butil::atomic<size_t> _top;
};

}  // namespace bthread

#endif  // BTHREAD_WORK_STEALING_QUEUE_H


/*
Qusetion? 
atomic_thread_fence(std::memory_order_release) 会将前面行x=42 全部缓存刷到内存里面然后同步给其他线程， 那么原子变量的release会把非原子变量的数据立刻同步给其他线程吗
内存序除了重排之外，还有一个功能个就是确保可见性。。最需要注意的细节问题。
. 标准规范角度
严格标准规定：C++ 标准不保证 release 操作会使非原子变量立即对其他线程可见。对非原子变量的并发访问本身就是未定义行为（UB）
标准只保证原子变量之间的同步。标准原文依据：C++20 [intro.races]：对非原子变量的数据竞争导致未定义行为
// 线程 A
x = 42 或者x.store(42, std::memory_order_relaxed);  // (1) // 或者x = 1;   
y.store(1, std::memory_order_release);  // (2)
// 线程 B
while (y.load(std::memory_order_acquire) != 1) {}  // (3)
assert(x==42);    // (4) ? 问题qusetion
线程 A 的执行：
(1) x.store(1) 执行，但 结果可能还在 CPU 缓存中（未刷到主存）。
(2) y.store(1) 执行，并且由于是 release，强制将之前的缓存刷新到主存（包括 x = 1）。
但 缓存刷新是异步的！y = 1 可能比 x = 1 先到达线程 B 的 CPU。
线程 B 的执行：
(3) 看到 y == 1（因为 y 的更新已全局可见）。
(4) 读取 x 时，可能 x = 1 的更新还未到达线程 B 的缓存，所以读到旧值 0。
relaxed也可能更新可能还在 当前核心的写缓冲区，未被其他核心看到。长时间停留在存储缓冲区或核心本地缓存
如果是原子变量release会强制将当前线程的缓存刷新到主内存（x=42也会刷到主内存, 使之前的写入对其他线程可见）。
但 不保证其他线程立即看到非UB（x=42) 操作（可能需要缓存一致性协议同步）。
用atomic_thread_fence可以将所有包括原子和非原子都刷入

那为什么有的里面说 assert(x==42);永远成功呢。标准答案是 在x86环境是对的。但是在非x86体系下比如ARM架构，可能会有问题
因为x86 ; x86汇编示例   x86的mov相当于自带release
mov [data], 42    ; 非原子存储
mov [flag], 1     ; release存储（实际是普通mov + 隐式屏障）
为什么x86上"看似有效"
TSO内存模型：
x86的存储缓冲区会按序提交. 普通存储和原子存储使用相同的提交机制存储转发：当CPU执行 release 存储时，会等待前面所有存储完成
包括非原子变量的存储; 现代x86 CPU内部处理mfence的简化流程
1. 排空存储缓冲区(stores buffer)
2. 失效其他核心的对应缓存行
3. 等待所有ACK响应
4. 继续后续指令
 * */