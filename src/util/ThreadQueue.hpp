#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace llmcli {

// A simple thread-safe, multi-producer / multi-consumer queue used to hand
// events from the network worker thread back to the UI loop (T9).
//
// `pop()` blocks until an item is available or the queue is shut down; after
// shutdown it drains any remaining items and then returns nullopt, so a
// consumer loop terminates cleanly.
template <typename T>
class ThreadQueue {
 public:
  void push(T value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(std::move(value));
    }
    cv_.notify_one();
  }

  // Blocks until an item is available, or returns nullopt once the queue is
  // closed and empty.
  std::optional<T> pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return std::nullopt;  // closed and drained
    T value = std::move(queue_.front());
    queue_.pop();
    return value;
  }

  // Non-blocking. Returns false if the queue is currently empty.
  bool try_pop(T& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  // Wakes all blocked consumers. Already-queued items remain poppable.
  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<T> queue_;
  bool closed_ = false;
};

}  // namespace llmcli
