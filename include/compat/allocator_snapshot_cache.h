#pragma once
#include "compat/bionic_host_libc_runtime.h"
#include <cstdint>
#include <mutex>

namespace mocktail::compat {
// Heap accounting is a sampled statistic. A glibc scan walks free lists while
// holding arena locks, so back-to-back Android queries share a recent sample.
// The scan runs outside this mutex; concurrent callers can use the last complete
// sample without waiting for another allocator walk.
class AllocatorSnapshotCache {
 public:
  static constexpr std::uint64_t kRefreshNs=500'000'000ULL;
  template<class Reader>
  BionicMallinfoSnapshot Read(std::uint64_t now,Reader reader) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if(refreshing_ || (initialized_ && now<refresh_after_))return snapshot_;
    refreshing_=true;
    lock.unlock();
    const auto fresh=reader();
    lock.lock();
    snapshot_=fresh;
    initialized_=true;
    refresh_after_=now+kRefreshNs;
    refreshing_=false;
    return snapshot_;
  }
 private:
  std::mutex mutex_;
  BionicMallinfoSnapshot snapshot_{};
  bool initialized_=false,refreshing_=false;
  std::uint64_t refresh_after_=0;
};
}
