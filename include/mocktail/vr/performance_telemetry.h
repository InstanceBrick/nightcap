#ifndef MOCKTAIL_VR_PERFORMANCE_TELEMETRY_H_
#define MOCKTAIL_VR_PERFORMANCE_TELEMETRY_H_

#include "mocktail/vr/performance_stats.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mocktail::vr {

// The launcher owns a private tmpfs file for the lifetime of one application.
// Only lock-free stores run in the frame path: no file writes, GPU waits or
// frame history allocations. A separate noninteractive overlay reads it.
struct alignas(64) SharedPerformanceStats {
  static constexpr std::uint64_t kMagic = 0x4e43504600000002ULL;
  std::atomic<std::uint64_t> magic{0}, sequence{0};
  std::atomic<std::uint64_t> timestamp_ns{0}, frames{0}, last_frame_ns{0};
  std::atomic<std::uint64_t> process{0}, width{0}, height{0}, visible{0};
  std::atomic<std::uint64_t> render_width{0}, render_height{0};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

class PerformanceMapping {
 public:
  PerformanceMapping() = default;
  PerformanceMapping(const PerformanceMapping&) = delete;
  PerformanceMapping& operator=(const PerformanceMapping&) = delete;
  ~PerformanceMapping() {
    if (record_) munmap(record_, sizeof(SharedPerformanceStats));
  }
  bool Open(const char* path, bool writer) {
    if (record_) return true;
    if (!path || path[0] != '/') return false;
    const int fd = open(path, (writer ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat info{};
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 0077) || info.st_size < static_cast<off_t>(sizeof(SharedPerformanceStats))) {
      close(fd);
      return false;
    }
    void* mapped = mmap(nullptr, sizeof(SharedPerformanceStats),
                        writer ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) return false;
    record_ = static_cast<SharedPerformanceStats*>(mapped);
    if (writer) {
      new (record_) SharedPerformanceStats;
      record_->magic.store(SharedPerformanceStats::kMagic, std::memory_order_release);
    }
    return true;
  }
  void Publish(const PerformanceSnapshot& value) {
    if (!record_) return;
    record_->sequence.fetch_add(1, std::memory_order_acq_rel);
    record_->timestamp_ns.store(value.timestamp_ns, std::memory_order_relaxed);
    record_->frames.store(value.frames, std::memory_order_relaxed);
    record_->last_frame_ns.store(value.last_frame_ns, std::memory_order_relaxed);
    record_->process.store(value.process, std::memory_order_relaxed);
    record_->width.store(value.width, std::memory_order_relaxed);
    record_->height.store(value.height, std::memory_order_relaxed);
    record_->visible.store(value.visible, std::memory_order_relaxed);
    record_->render_width.store(value.render_width, std::memory_order_relaxed);
    record_->render_height.store(value.render_height, std::memory_order_relaxed);
    record_->sequence.fetch_add(1, std::memory_order_release);
  }
  bool Read(PerformanceSnapshot* value) const {
    if (!record_ || record_->magic.load(std::memory_order_acquire) != SharedPerformanceStats::kMagic)
      return false;
    for (int attempt = 0; attempt < 8; ++attempt) {
      const auto sequence = record_->sequence.load(std::memory_order_acquire);
      if (sequence & 1) continue;
      PerformanceSnapshot snapshot;
      snapshot.timestamp_ns = record_->timestamp_ns.load(std::memory_order_relaxed);
      snapshot.frames = record_->frames.load(std::memory_order_relaxed);
      snapshot.last_frame_ns = record_->last_frame_ns.load(std::memory_order_relaxed);
      snapshot.process = record_->process.load(std::memory_order_relaxed);
      snapshot.width = record_->width.load(std::memory_order_relaxed);
      snapshot.height = record_->height.load(std::memory_order_relaxed);
      snapshot.visible = record_->visible.load(std::memory_order_relaxed) != 0;
      snapshot.render_width = record_->render_width.load(std::memory_order_relaxed);
      snapshot.render_height = record_->render_height.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (sequence == record_->sequence.load(std::memory_order_relaxed)) {
        *value = snapshot;
        return true;
      }
    }
    return false;
  }
 private:
  SharedPerformanceStats* record_ = nullptr;
};

class PerformancePublisher {
 public:
  void Update(std::uint64_t now, std::uint64_t frames, bool visible,
              std::uint32_t width, std::uint32_t height,
              std::uint32_t render_width, std::uint32_t render_height) {
    if (!attempted_) {
      attempted_ = true;
      process_ = static_cast<std::uint64_t>(getpid());
      enabled_ = mapping_.Open(std::getenv("NIGHTCAP_PERFORMANCE_TELEMETRY"), true);
    }
    if (!enabled_) return;
    if (frames != previous_frames_) last_frame_ns_ = now;
    previous_frames_ = frames;
    mapping_.Publish({now, frames, last_frame_ns_, process_,
                      width, height, visible, render_width, render_height});
  }
 private:
  PerformanceMapping mapping_;
  bool attempted_ = false, enabled_ = false;
  std::uint64_t previous_frames_ = 0, last_frame_ns_ = 0, process_ = 0;
};

}  // namespace mocktail::vr
#endif
