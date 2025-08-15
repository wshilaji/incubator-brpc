// Copyright 2020 bilibili
// Author: guoyi02@bilibili.com

#ifndef BASE_BOUNDED_TIMED_BLOCKING_QUEUE_H_
#define BASE_BOUNDED_TIMED_BLOCKING_QUEUE_H_

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <boost/circular_buffer.hpp>

#include "builtin_expect.h"
#include "delete_copy_and_assign_macro.h"

namespace base {

template <typename ValueType>
class BoundedTimedBlockingQueue {
 public:
  BoundedTimedBlockingQueue(size_t max_size) : queue_(max_size) {}

  bool Enqueue(const ValueType& value) {
    Lock lock(mutex_);
    full_cond_.wait(lock, [this] { return !queue_.full(); });
    queue_.push_back(value);
    lock.unlock();
    empty_cond_.notify_one();
    return true;
  }
  bool Enqueue(ValueType&& value) {
    Lock lock(mutex_);
    full_cond_.wait(lock, [this] { return !queue_.full(); });
    queue_.push_back(std::move(value));
    lock.unlock();
    empty_cond_.notify_one();
    return true;
  }

  // milli_seconds = 0 means never block
  bool TimedEnqueue(int32_t milli_seconds, const ValueType& value) {
    if (milli_seconds < 0) {
      return false;
    }
    Lock lock(mutex_);
    if (!full_cond_.wait_for(lock,
                             std::chrono::milliseconds(milli_seconds),
                             [this] { return !queue_.full(); })) {
      return false; // timeout, lock will be unlocked.
    }
    queue_.push_back(value);
    lock.unlock();
    empty_cond_.notify_one();
    return true;
  }
  // milli_seconds = 0 means never block
  bool TimedEnqueue(int32_t milli_seconds, ValueType&& value) {
    if (milli_seconds < 0) {
      return false;
    }
    Lock lock(mutex_);
    if (!full_cond_.wait_for(lock,
                             std::chrono::milliseconds(milli_seconds),
                             [this] { return !queue_.full(); })) {
      return false; // timeout, lock will be unlocked.
    }
    queue_.push_back(std::move(value));
    lock.unlock();
    empty_cond_.notify_one();
    return true;
  }

  bool Dequeue(ValueType* value) {
    if (unlikely(!value)) {
      return false;
    }
    Lock lock(mutex_);
    empty_cond_.wait(lock, [this] { return !queue_.empty(); });
    ValueType tmp_value(std::move(queue_.front()));
    queue_.pop_front();
    lock.unlock();
    full_cond_.notify_one();
    *value = std::move(tmp_value);
    return true;
  }
  // milli_seconds = 0 means never block
  bool TimedDequeue(int32_t milli_seconds, ValueType* value) {
    if (unlikely(!value || milli_seconds < 0)) {
      return false;
    }
    Lock lock(mutex_);
    if(!empty_cond_.wait_for(lock,
                             std::chrono::milliseconds(milli_seconds),
                             [this] { return !queue_.empty(); })) {
      return false; // timeout
    }
    ValueType tmp_value(std::move(queue_.front()));
    queue_.pop_front();
    lock.unlock();
    full_cond_.notify_one();
    *value = std::move(tmp_value);
    return true;
  }

  bool Empty() {
    Lock lock(mutex_);
    return queue_.empty();
  }

  bool Full() {
    Lock lock(mutex_);
    return queue_.full();
  }

  size_t Size() {
    Lock lock(mutex_);
    return queue_.size();
  }

 private:
  typedef std::unique_lock<std::mutex> Lock;
  std::mutex mutex_;
  std::condition_variable empty_cond_;
  std::condition_variable full_cond_;
  boost::circular_buffer<ValueType> queue_;

  DELETE_COPY_AND_ASSIGN(BoundedTimedBlockingQueue);
};

} // namespace base

#endif // BASE_BOUNDED_TIMED_BLOCKING_QUEUE_H_

