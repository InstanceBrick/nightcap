#include "mocktail/graphics/vulkan_foveation_device_policy.h"

#include <gtest/gtest.h>
#include <type_traits>

namespace mocktail::graphics::internal {
namespace {

TEST(FoveationDevicePolicy, NoOffsetsUsesTheQualifiedContract) {
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  EXPECT_FALSE(SnapshotFoveationDevicePolicy(&info).density_offsets_enabled);
  EXPECT_TRUE(SnapshotFoveationDevicePolicy(nullptr).density_offsets_enabled);
}

TEST(FoveationDevicePolicy, EitherExtensionSpellingKeepsConservativeGuard) {
  for (const char* name : {"VK_QCOM_fragment_density_map_offset",
                            "VK_EXT_fragment_density_map_offset"}) {
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &name;
    EXPECT_TRUE(SnapshotFoveationDevicePolicy(&info).density_offsets_enabled);
  }
}

TEST(FoveationDevicePolicy, EnabledFeatureKeepsConservativeGuardWithoutExtension) {
  VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT offsets{};
  offsets.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_EXT;
  VkPhysicalDeviceFeatures2 root{};
  root.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  root.pNext = &offsets;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &root;
  EXPECT_FALSE(SnapshotFoveationDevicePolicy(&info).density_offsets_enabled);
  offsets.fragmentDensityMapOffset = VK_TRUE;
  EXPECT_TRUE(SnapshotFoveationDevicePolicy(&info).density_offsets_enabled);
}

TEST(FoveationDevicePolicy, RegistrationSnapshotOwnsNoRuntimeBorrowedPointers) {
  static_assert(std::is_trivially_copyable_v<FoveationDevicePolicy>);
  VkPhysicalDeviceFeatures2 root{};
  root.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &root;
  const auto snapshot = SnapshotFoveationDevicePolicy(&info);
  // Model a loader inserting a temporary node into a borrowed input chain.
  root.pNext = reinterpret_cast<void*>(1);
  info.pNext = reinterpret_cast<void*>(1);
  info.ppEnabledExtensionNames = reinterpret_cast<const char* const*>(1);
  EXPECT_FALSE(snapshot.density_offsets_enabled);
}

TEST(FoveationDevicePolicy, MalformedChainsAndExtensionArraysStayConservative) {
  VkBaseInStructure cyclic{};
  cyclic.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  cyclic.pNext = &cyclic;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &cyclic;
  EXPECT_TRUE(SnapshotFoveationDevicePolicy(&info).density_offsets_enabled);
  info.pNext = nullptr;
  info.enabledExtensionCount = 1;
  EXPECT_TRUE(SnapshotFoveationDevicePolicy(&info).density_offsets_enabled);
  const char* missing = nullptr;
  info.ppEnabledExtensionNames = &missing;
  EXPECT_TRUE(SnapshotFoveationDevicePolicy(&info).density_offsets_enabled);
}

}  // namespace
}  // namespace mocktail::graphics::internal
