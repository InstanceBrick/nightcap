#pragma once

#include <cstdint>

namespace mocktail::vr {

struct NativeProjectionSample {
  // The actual engine camera projection before temporal jitter, row-major.
  // This is read from the native camera rather than rebuilt from tracking FOV.
  float projection[16] = {};
  // The full world-view-projection used by the engine, including player
  // locomotion. Supplied by the separately verified native combined getter.
  float view_projection[16] = {};
  float jitter_pixels[2] = {};
  std::uint32_t render_width = 0;
  std::uint32_t render_height = 0;
  float camera_near = 0.f;
  float camera_far = 0.f;
  std::uintptr_t camera_identity = 0;
  std::uint64_t frame = 0;
  bool depth_inverted = false;
  bool depth_infinite = false;
  bool jitter_applied = false;
  bool view_projection_valid = false;
  bool valid = false;
};

// True only when the exact native per-eye projection function was verified and
// hooked. Unsupported clients retain their original projections.
bool NativeProjectionBridgeReady();
// Copies only a projection from the requested frame. A stale or unsupported
// camera never supplies temporal reconstruction parameters.
bool ReadNativeProjectionSample(int eye, std::uint64_t expected_frame,
                                NativeProjectionSample* sample);

}  // namespace mocktail::vr
