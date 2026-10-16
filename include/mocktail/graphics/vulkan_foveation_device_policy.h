#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vulkan/vulkan.h>

namespace mocktail::graphics::internal {

struct FoveationDevicePolicy {
  bool density_offsets_enabled = true;
};

// Capture before invoking a runtime/loader device callback: its temporary
// feature-chain records must never be inspected after the call returns.
inline FoveationDevicePolicy SnapshotFoveationDevicePolicy(
    const VkDeviceCreateInfo* info) {
  if (!info || info->sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO ||
      info->enabledExtensionCount > 4096 ||
      (info->enabledExtensionCount && !info->ppEnabledExtensionNames)) return {};
  bool offsets = false;
  for (std::uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
    const char* name = info->ppEnabledExtensionNames[i];
    if (!name) return {};
    if (!std::strcmp(name, "VK_QCOM_fragment_density_map_offset") ||
        !std::strcmp(name, "VK_EXT_fragment_density_map_offset"))
      offsets = true;
  }
  std::array<const VkBaseInStructure*, 64> seen{};
  std::size_t count = 0;
  for (auto* node = static_cast<const VkBaseInStructure*>(info->pNext);
       node; node = node->pNext) {
    if (count == seen.size()) return {};
    for (std::size_t i = 0; i < count; ++i) if (seen[i] == node) return {};
    seen[count++] = node;
    if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_EXT)
      offsets |= reinterpret_cast<const VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT*>(node)
                     ->fragmentDensityMapOffset != VK_FALSE;
  }
  return {offsets};
}

}  // namespace mocktail::graphics::internal
