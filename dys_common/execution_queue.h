#ifndef BASE_EXECUTION_QUEUE_H
#define BASE_EXECUTION_QUEUE_H

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <bthread/execution_queue.h>

namespace base {

template <typename T>
class ExecutionQueue {
  using Ptr = std::shared_ptr<T>;
public:
  static int Execute(void *, bthread::TaskIterator<Ptr> &iter) {
      for (; iter; ++iter) {
        // 任务类中需要实现一个execute的执行接口
        iter->get()->execute();
      }
      return 0;
  }

  // 初始化
  static void Init() {
    std::call_once(init_flag_, [](){
      bthread::execution_queue_start(&q_, nullptr, Execute, nullptr);
    });
  }

  // 提交任务
  static void Push(Ptr item) {
    bthread::execution_queue_execute(q_, item);
  }

public:
  static std::once_flag init_flag_;
  static bthread::ExecutionQueueId<Ptr> q_;
};

template <typename T>
std::once_flag ExecutionQueue<T>::init_flag_;
template <typename T>
bthread::ExecutionQueueId<std::shared_ptr<T>> ExecutionQueue<T>::q_;

} // namespace base

#endif // BASE_EXECUTION_QUEUE_H