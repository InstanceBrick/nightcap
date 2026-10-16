#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace mocktail::graphics::internal {

// Only transfers between two concrete queue families can be converted for a
// desktop swapchain. External/foreign ownership must reach the driver intact.
inline bool NeedsDesktopQueueNormalization(std::uint32_t source,
                                            std::uint32_t destination) {
  return source != destination && source < VK_QUEUE_FAMILY_FOREIGN_EXT &&
         destination < VK_QUEUE_FAMILY_FOREIGN_EXT;
}

template <class Barrier>
inline bool HasDesktopQueueTransfers(std::uint32_t count,
                                    const Barrier* barriers) {
  if (!barriers) return false;
  for (std::uint32_t i = 0; i < count; ++i)
    if (NeedsDesktopQueueNormalization(barriers[i].srcQueueFamilyIndex,
                                       barriers[i].dstQueueFamilyIndex))
      return true;
  return false;
}

// Keep the caller's array for the common no-ownership-transfer path. A batch
// callback can inspect a mixed array under one backend lock; the original
// array and every structure chained through pNext remain untouched.
template <class Barrier, class Normalize, class Forward>
inline void WithDesktopQueueBarriers(std::uint32_t count,
                                     const Barrier* barriers,
                                     Normalize&& normalize, Forward&& forward) {
  if (!HasDesktopQueueTransfers(count, barriers)) {
    forward(barriers);
    return;
  }
  constexpr std::size_t kStackCount = 8;
  if (count <= kStackCount) {
    std::array<Barrier, kStackCount> adjusted;
    std::copy_n(barriers, count, adjusted.data());
    normalize(count, adjusted.data());
    forward(adjusted.data());
  } else {
    std::vector<Barrier> adjusted(barriers, barriers + count);
    normalize(count, adjusted.data());
    forward(adjusted.data());
  }
}

}  // namespace mocktail::graphics::internal
