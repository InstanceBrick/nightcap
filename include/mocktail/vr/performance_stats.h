#ifndef MOCKTAIL_VR_PERFORMANCE_STATS_H_
#define MOCKTAIL_VR_PERFORMANCE_STATS_H_

#include <cstdint>
#include <deque>

namespace mocktail::vr {

struct PerformanceSnapshot {
  std::uint64_t timestamp_ns = 0;
  std::uint64_t frames = 0;
  std::uint64_t last_frame_ns = 0;
  std::uint64_t process = 0;
  std::uint64_t width = 0, height = 0;
  bool visible = false;
  std::uint64_t render_width = 0, render_height = 0;
};

// Count successful application projections over a rolling second. Neither
// the scheduler's cap nor compositor repeats count as new game frames.
class PerformanceRate {
 public:
  void Reset() { samples_.clear(); }
  double Sample(std::uint64_t now, const PerformanceSnapshot& snapshot) {
    if (!snapshot.visible || snapshot.timestamp_ns > now ||
        now - snapshot.timestamp_ns > kSecond ||
        (!samples_.empty() && (now <= samples_.back().time ||
                              snapshot.frames < samples_.back().frames))) {
      Reset();
      return -1.;
    }
    samples_.push_back({now, snapshot.frames});
    const auto cutoff = now > kSecond ? now - kSecond : 0;
    while (samples_.size() > 2 && samples_[1].time <= cutoff)
      samples_.pop_front();
    if (!snapshot.last_frame_ns || snapshot.last_frame_ns > now ||
        now - snapshot.last_frame_ns >= kSecond) return 0.;
    const auto elapsed = now - samples_.front().time;
    if (elapsed < kSecond / 2) return -1.;
    return double(snapshot.frames - samples_.front().frames) * 1.e9 /
           double(elapsed);
  }
 private:
  static constexpr std::uint64_t kSecond = 1'000'000'000;
  struct SamplePoint { std::uint64_t time, frames; };
  std::deque<SamplePoint> samples_;
};

}  // namespace mocktail::vr
#endif
