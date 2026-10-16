#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>

namespace mocktail::vr::internal {

// Load once during startup, then keep file I/O and parsing on a worker. The
// render thread only copies a completed snapshot under a short-held lock.
template <typename T>
class AsyncSettings {
 public:
  using Reader = std::function<T(const T&)>;
  AsyncSettings(T initial, Reader reader, std::chrono::milliseconds interval)
      : value_(initial), reader_(std::move(reader)), interval_(interval) {
    static_assert(std::is_trivially_copyable_v<T>);
    Refresh();
    worker_ = std::thread([this] {
      std::unique_lock<std::mutex> stop_lock(stop_mutex_);
      while (!stopped_) {
        if (wake_.wait_for(stop_lock, interval_, [this] { return stopped_; }))
          break;
        stop_lock.unlock();
        Refresh();
        stop_lock.lock();
      }
    });
  }
  ~AsyncSettings() {
    {
      std::lock_guard<std::mutex> lock(stop_mutex_);
      stopped_ = true;
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
  }
  AsyncSettings(const AsyncSettings&) = delete;
  AsyncSettings& operator=(const AsyncSettings&) = delete;

  T Get() const {
    std::lock_guard<std::mutex> lock(value_mutex_);
    return value_;
  }

 private:
  void Refresh() {
    try {
      const T next = reader_(Get());
      std::lock_guard<std::mutex> lock(value_mutex_);
      value_ = next;
    } catch (...) {
      // A failed read retains the last completed settings. In particular,
      // parsing must never throw through a guest rendering callback.
    }
  }
  mutable std::mutex value_mutex_;
  T value_;
  Reader reader_;
  std::chrono::milliseconds interval_;
  std::mutex stop_mutex_;
  std::condition_variable wake_;
  bool stopped_ = false;
  std::thread worker_;
};

}  // namespace mocktail::vr::internal
