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

#ifndef BTHREAD_SYS_FUTEX_H
#define BTHREAD_SYS_FUTEX_H

#include "butil/build_config.h"         // OS_MACOSX
#include <unistd.h>                     // syscall
#include <time.h>                       // timespec
#if defined(OS_LINUX)
#include <syscall.h>                    // SYS_futex
#include <linux/futex.h>                // FUTEX_WAIT, FUTEX_WAKE

namespace bthread {
//备注 c++11的std::condition_variable是真阻塞。wait的时候塞到了内核的等待队列里面
// 但是c++20的 std::atomic::wait() 是自旋的。相当于自旋锁。并没有塞到内核的等待队列里面 估计是用futex实现的
#ifndef FUTEX_PRIVATE_FLAG
#define FUTEX_PRIVATE_FLAG 128
#endif

inline int futex_wait_private(
    void* addr1, int expected, const timespec* timeout) {
    return syscall(SYS_futex, addr1, (FUTEX_WAIT | FUTEX_PRIVATE_FLAG),
                   expected, timeout, NULL, 0);
}

inline int futex_wake_private(void* addr1, int nwake) {
    return syscall(SYS_futex, addr1, (FUTEX_WAKE | FUTEX_PRIVATE_FLAG),
                   nwake, NULL, NULL, 0);
}

inline int futex_requeue_private(void* addr1, int nwake, void* addr2) {
    return syscall(SYS_futex, addr1, (FUTEX_REQUEUE | FUTEX_PRIVATE_FLAG),
                   nwake, NULL, addr2, 0);
}

}  // namespace bthread
// mutex完全由内核决定要不要挂起。。这样有点费性能。 futex由用户态和内核态一块决定
//在无竞争的情况下，完全在用户空间进行操作，避免陷入内核的高成本系统调用；只有在真正需要等待或唤醒时（即发生竞争时），才求助内核。
/*假设我们用一个原子整数 futex_word 来表示锁的状态：0 表示未锁定，1 表示锁定。
加锁（Lock）操作
快速路径（无竞争）：线程尝试使用原子操作（如 CAS）将 futex_word 从 0 改为 1。如果成功，说明它成功获取了锁，整个过程都在用户空间完成，没有系统调用，速度极快。
慢速路径（有竞争）：如果 CAS 失败（值已经是 1），说明锁被其他线程持有。线程再次读取 futex_word，确认它仍然是 1（锁定状态）。
然后，它调用 futex_wait(&futex_word, 1) 系统调用。这个调用告诉内核：“如果 futex_word 的值仍然是 1，就让我在这个地址上睡眠吧；如果不是，我就不睡了直接返回”。
内核将线程挂起，放入等待队列。

解锁（Unlock）操作
快速路径（无等待者）：线程使用原子操作将 futex_word 从 1 设为 0。然后，它检查是否可能有线程在等待（这通常通过检查锁内部的一些记录来实现，但本质上是为了避免不必要的系统调用）。
慢速路径（有等待者）：如果认为有线程在等待，它就调用 futex_wake(&futex_word, 1) 系统调用。这个调用告诉内核：“请唤醒一个正在 futex_word 这个地址上等待的线程”。内核会从等待队列中唤醒一个线程，该线程会重新尝试获取锁。
 */
#elif defined(OS_MACOSX)

namespace bthread {

int futex_wait_private(void* addr1, int expected, const timespec* timeout);

int futex_wake_private(void* addr1, int nwake);

int futex_requeue_private(void* addr1, int nwake, void* addr2);

}  // namespace bthread

#else
#error "Unsupported OS"
#endif

#endif // BTHREAD_SYS_FUTEX_H
