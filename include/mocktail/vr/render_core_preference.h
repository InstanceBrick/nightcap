#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace mocktail::vr::internal {
struct CpuCapacity { unsigned id; unsigned capacity; };

// Input contains only CPUs already allowed for this thread. Keep near-peer
// performance cores together, and retain the original mask if information is
// incomplete or a preference would leave fewer than two available cores.
inline std::vector<unsigned> PerformanceCpuSubset(
    const std::vector<CpuCapacity>& allowed) {
  std::vector<unsigned> original;
  unsigned maximum = 0;
  bool complete = true;
  for (const auto& cpu : allowed) {
    original.push_back(cpu.id);
    maximum = std::max(maximum, cpu.capacity);
    complete = complete && cpu.capacity > 0;
  }
  if (!complete || !maximum || allowed.size() < 2) return original;
  std::vector<unsigned> selected;
  for (const auto& cpu : allowed)
    if (std::uint64_t(cpu.capacity) * 5 >= std::uint64_t(maximum) * 4)
      selected.push_back(cpu.id);
  return selected.size() >= 2 ? selected : original;
}
}  // namespace mocktail::vr::internal
