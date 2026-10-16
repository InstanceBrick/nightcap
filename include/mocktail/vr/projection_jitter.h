#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mocktail::vr {

inline bool ValidRasterJitter(bool enabled, const float (&jitter_pixels)[2],
                             std::uint32_t width, std::uint32_t height) {
  return enabled && width && height &&
         std::isfinite(jitter_pixels[0]) && std::isfinite(jitter_pixels[1]) &&
         std::abs(jitter_pixels[0]) <= 1.f &&
         std::abs(jitter_pixels[1]) <= 1.f;
}

// Adjusts the native projection's positive {up, down, left, right} tangent
// extents. Jitter is image displacement in render pixels: +X right, +Y down.
// With the existing right-handed camera looking down -Z, a ray projects to
// u=(x/-z+left)/(left+right), v=(up-y/-z)/(up+down). Translating both frustum
// bounds preserves its angular span and displaces every ray by the same pixels.
// Only the caller-owned projection changes; runtime FOV and tracking do not.
inline bool ApplyRasterProjectionJitter(float (&tangents)[4], bool enabled,
                                        const float (&jitter_pixels)[2],
                                        std::uint32_t render_width,
                                        std::uint32_t render_height) {
  if (!ValidRasterJitter(enabled, jitter_pixels, render_width, render_height))
    return false;
  for (float value : tangents)
    if (!std::isfinite(value) || value <= 0.f)
      return false;

  const double dx = static_cast<double>(jitter_pixels[0]) *
                    (static_cast<double>(tangents[2]) + tangents[3]) /
                    render_width;
  const double dy = static_cast<double>(jitter_pixels[1]) *
                    (static_cast<double>(tangents[0]) + tangents[1]) /
                    render_height;
  const std::array<float, 4> shifted = {
      static_cast<float>(tangents[0] + dy),
      static_cast<float>(tangents[1] - dy),
      static_cast<float>(tangents[2] + dx),
      static_cast<float>(tangents[3] - dx)};
  for (float value : shifted)
    if (!std::isfinite(value) || value <= 0.f)
      return false;
  for (int i = 0; i < 4; ++i)
    tangents[i] = shifted[i];
  return true;
}

// The verified native Camera getter mirrors M02 for the right eye. Apply jitter
// after that operation to give both eyes the same displacement. Vulkan image Y
// increases downward; the verified native scene shader negates clip Y before
// its Vulkan viewport, while camera-space Y and this projection increase up.
inline bool ApplyRasterMatrixProjectionJitter(float (&matrix)[16], bool enabled,
                                              const float (&jitter_pixels)[2],
                                              std::uint32_t render_width,
                                              std::uint32_t render_height) {
  if (!ValidRasterJitter(enabled, jitter_pixels, render_width, render_height))
    return false;
  for (float value : matrix)
    if (!std::isfinite(value))
      return false;
  if (matrix[0] <= 0.f || matrix[5] <= 0.f || matrix[14] != -1.f ||
      matrix[15] != 0.f)
    return false;
  const float x = matrix[2] - 2.f * jitter_pixels[0] / render_width;
  const float y = matrix[6] + 2.f * jitter_pixels[1] / render_height;
  if (!std::isfinite(x) || !std::isfinite(y))
    return false;
  matrix[2] = x;
  matrix[6] = y;
  return true;
}

inline bool ApplyRasterViewProjectionJitter(float (&matrix)[16], bool enabled,
                                           const float (&jitter_pixels)[2],
                                           std::uint32_t render_width,
                                           std::uint32_t render_height) {
  if (!ValidRasterJitter(enabled, jitter_pixels, render_width, render_height))
    return false;
  for (float value : matrix)
    if (!std::isfinite(value))
      return false;
  double denominator_norm = 0.;
  for (int i = 12; i < 16; ++i)
    denominator_norm += static_cast<double>(matrix[i]) * matrix[i];
  if (denominator_norm <= 0.)
    return false;
  const float dx = 2.f * jitter_pixels[0] / render_width;
  const float dy = -2.f * jitter_pixels[1] / render_height;
  float shifted[8];
  for (int column = 0; column < 4; ++column) {
    shifted[column] = matrix[column] + dx * matrix[12 + column];
    shifted[4 + column] = matrix[4 + column] + dy * matrix[12 + column];
  }
  for (float value : shifted)
    if (!std::isfinite(value))
      return false;
  std::copy_n(shifted, 8, matrix);
  return true;
}

// The observed native reverse-Z perspective builder writes M22=n/(f-n),
// M23=n*f/(f-n), M32=-1. Derive its actual clip planes without assuming a
// fixed near plane, which the client changes with its camera mode.
inline bool DecodeReverseProjectionDepth(const float (&matrix)[16],
                                         float* camera_near,
                                         float* camera_far,
                                         bool* infinite) {
  if (!camera_near || !camera_far || !infinite || matrix[14] != -1.f ||
      matrix[15] != 0.f)
    return false;
  for (float value : matrix)
    if (!std::isfinite(value))
      return false;
  const double a = matrix[10], b = matrix[11];
  if (a < 0. || b <= 0.)
    return false;
  const float near = static_cast<float>(b / (a + 1.));
  const bool is_infinite = a == 0.;
  const float far = is_infinite ? std::numeric_limits<float>::infinity()
                               : static_cast<float>(b / a);
  if (!std::isfinite(near) || near <= 0.f ||
      (!is_infinite && (!std::isfinite(far) || far <= near)))
    return false;
  *camera_near = near;
  *camera_far = far;
  *infinite = is_infinite;
  return true;
}

}  // namespace mocktail::vr
