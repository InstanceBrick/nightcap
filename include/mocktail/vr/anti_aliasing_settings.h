#pragma once

#include <cstdint>

namespace mocktail::vr::internal {
// Retired multisample choices use the native profile's SMAA High default.
constexpr int NativeAntiAliasingMode(std::int64_t value) {
  return value >= 0 && value <= 3 ? static_cast<int>(value) : 3;
}
}  // namespace mocktail::vr::internal
