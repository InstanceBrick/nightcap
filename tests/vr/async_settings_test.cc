#include "mocktail/vr/async_settings.h"

#include <gtest/gtest.h>
#include <atomic>
#include <future>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;
using mocktail::vr::internal::AsyncSettings;
struct Setting { int version = 0; int paired = 0; };

TEST(AsyncSettings, FirstReadIsAvailableBeforeRenderingStarts) {
  AsyncSettings<Setting> cache({}, [](const Setting&) { return Setting{7, -7}; }, 1h);
  EXPECT_EQ(cache.Get().version, 7);
  EXPECT_EQ(cache.Get().paired, -7);
}

TEST(AsyncSettings, SlowReadDoesNotHoldTheSnapshotLock) {
  std::promise<void> entered, release;
  auto proceed = release.get_future().share();
  std::atomic<int> calls{0};
  AsyncSettings<Setting> cache({}, [&](const Setting&) {
    const int version = ++calls;
    if (version == 2) { entered.set_value(); proceed.wait(); }
    return Setting{version, -version};
  }, 1ms);
  const auto status = entered.get_future().wait_for(2s);
  if (status != std::future_status::ready) release.set_value();
  ASSERT_EQ(status, std::future_status::ready);
  auto read = std::async(std::launch::async, [&] { return cache.Get(); });
  const bool ready = read.wait_for(100ms) == std::future_status::ready;
  release.set_value();
  EXPECT_TRUE(ready);
  if (ready) EXPECT_EQ(read.get().version, 1);
}

TEST(AsyncSettings, FailedRefreshPreservesThePreviousSnapshotAndCanRecover) {
  std::atomic<int> stage{0};
  std::promise<void> failed;
  AsyncSettings<Setting> cache({}, [&](const Setting& old) {
    if (stage.load() == 0) return Setting{1, -1};
    if (stage.load() == 1) {
      stage = 2;
      failed.set_value();
      throw std::runtime_error("temporary settings read failure");
    }
    if (stage.load() == 2) return old;
    return Setting{3, -3};
  }, 1ms);
  stage = 1;
  ASSERT_EQ(failed.get_future().wait_for(2s), std::future_status::ready);
  EXPECT_EQ(cache.Get().version, 1);
  // A new source can be observed after a parsing failure.
  stage = 3;
  const auto until = std::chrono::steady_clock::now() + 2s;
  while (cache.Get().version != 3 && std::chrono::steady_clock::now() < until)
    std::this_thread::yield();
  EXPECT_EQ(cache.Get().version, 3);
  EXPECT_EQ(cache.Get().paired, -3);
}

TEST(AsyncSettings, ShutdownWakesALongPollingInterval) {
  const auto start = std::chrono::steady_clock::now();
  { AsyncSettings<Setting> cache({}, [](const Setting& old) { return old; }, 1h); }
  EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}
}  // namespace
