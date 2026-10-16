#include "mocktail/vr/fsr3_runtime_device.h"
#include <gtest/gtest.h>
#include <thread>

namespace mocktail::vr {
namespace {
TEST(Fsr3RuntimeDevice, FoldsRuntimeTimelineAndPreservesEveryBorrowedField) {
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
  timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  timeline.timelineSemaphore = VK_TRUE;
  VkPhysicalDeviceFeatures2 base{};
  base.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  base.features.robustBufferAccess = VK_TRUE;
  base.pNext = &timeline;
  VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr{};
  ycbcr.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
  ycbcr.samplerYcbcrConversion = VK_TRUE;
  ycbcr.pNext = &base;
  VkPhysicalDeviceVulkan12Features aggregate{};
  aggregate.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  aggregate.shaderFloat16 = VK_TRUE;
  aggregate.descriptorIndexing = VK_TRUE;
  aggregate.pNext = &ycbcr;
  VkPhysicalDeviceFragmentDensityMapFeaturesEXT density{};
  density.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT;
  density.fragmentDensityMap = VK_TRUE;
  density.pNext = &aggregate;
  const char* extensions[] = {"VK_KHR_timeline_semaphore"};
  VkDeviceQueueCreateInfo queue{};
  VkDeviceCreateInfo original{};
  original.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  original.pNext = &density;
  original.pQueueCreateInfos = &queue;
  original.queueCreateInfoCount = 1;
  original.enabledExtensionCount = 1;
  original.ppEnabledExtensionNames = extensions;
  Fsr3RuntimeTimelineFeatures normalized(original);
  ASSERT_TRUE(normalized.changed());
  const auto& result = normalized.create_info();
  EXPECT_EQ(result.pQueueCreateInfos, &queue);
  EXPECT_EQ(result.ppEnabledExtensionNames, extensions);
  auto* out_density = static_cast<const VkPhysicalDeviceFragmentDensityMapFeaturesEXT*>(result.pNext);
  EXPECT_NE(out_density, &density);
  EXPECT_EQ(out_density->fragmentDensityMap, VK_TRUE);
  auto* out_aggregate = static_cast<const VkPhysicalDeviceVulkan12Features*>(out_density->pNext);
  EXPECT_EQ(out_aggregate->timelineSemaphore, VK_TRUE);
  EXPECT_EQ(out_aggregate->shaderFloat16, VK_TRUE);
  EXPECT_EQ(out_aggregate->descriptorIndexing, VK_TRUE);
  auto* out_ycbcr = static_cast<const VkPhysicalDeviceSamplerYcbcrConversionFeatures*>(out_aggregate->pNext);
  EXPECT_EQ(out_ycbcr->samplerYcbcrConversion, VK_TRUE);
  auto* out_base = static_cast<const VkPhysicalDeviceFeatures2*>(out_ycbcr->pNext);
  EXPECT_EQ(out_base->features.robustBufferAccess, VK_TRUE);
  EXPECT_EQ(out_base->pNext, nullptr);
  EXPECT_EQ(aggregate.timelineSemaphore, VK_FALSE);
  EXPECT_EQ(aggregate.pNext, &ycbcr);
  EXPECT_EQ(base.pNext, &timeline);
}

TEST(Fsr3RuntimeDevice, PreservesGuestTimelineWhenRuntimeRecordIsFalse) {
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
  timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  VkPhysicalDeviceVulkan12Features aggregate{};
  aggregate.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  aggregate.timelineSemaphore = VK_TRUE;
  aggregate.pNext = &timeline;
  VkDeviceCreateInfo original{};
  original.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  original.pNext = &aggregate;
  Fsr3RuntimeTimelineFeatures normalized(original);
  ASSERT_TRUE(normalized.changed());
  auto* out = static_cast<const VkPhysicalDeviceVulkan12Features*>(normalized.create_info().pNext);
  EXPECT_EQ(out->timelineSemaphore, VK_TRUE);
  EXPECT_EQ(out->pNext, nullptr);
}

TEST(Fsr3RuntimeDevice, UnknownLoaderRecordsPassThroughWithoutRewritingMemory) {
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
  timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  VkPhysicalDeviceVulkan12Features aggregate{};
  aggregate.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  aggregate.pNext = &timeline;
  VkBaseInStructure unknown{};
  unknown.sType = static_cast<VkStructureType>(47);  // opaque loader record
  unknown.pNext = reinterpret_cast<const VkBaseInStructure*>(&aggregate);
  VkDeviceCreateInfo original{};
  original.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  original.pNext = &unknown;
  Fsr3RuntimeTimelineFeatures normalized(original);
  EXPECT_FALSE(normalized.changed());
  EXPECT_EQ(&normalized.create_info(), &original);
  EXPECT_EQ(unknown.pNext, reinterpret_cast<const VkBaseInStructure*>(&aggregate));
}

TEST(Fsr3RuntimeDevice, MissingPairAndConflictingOrCyclicChainsPassThrough) {
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
  timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  VkPhysicalDeviceVulkan12Features aggregate{};
  aggregate.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  VkPhysicalDeviceVulkan12Features duplicate = aggregate;
  VkDeviceCreateInfo original{};
  original.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  original.pNext = &aggregate;
  { Fsr3RuntimeTimelineFeatures result(original); EXPECT_FALSE(result.changed()); }
  aggregate.pNext = &timeline;
  timeline.pNext = &duplicate;
  { Fsr3RuntimeTimelineFeatures result(original); EXPECT_FALSE(result.changed()); }
  timeline.pNext = &aggregate;
  { Fsr3RuntimeTimelineFeatures result(original); EXPECT_FALSE(result.changed()); }
  timeline.pNext = nullptr;
  VkPhysicalDeviceShaderFloat16Int8Features conflicting{};
  conflicting.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
  timeline.pNext = &conflicting;
  { Fsr3RuntimeTimelineFeatures result(original); EXPECT_FALSE(result.changed()); }
}

const VkDeviceCreateInfo* seen_info = nullptr;
int calls_a = 0, calls_b = 0;
VkResult VKAPI_CALL RawCreateA(VkPhysicalDevice, const VkDeviceCreateInfo* info,
                             const VkAllocationCallbacks*, VkDevice*) {
  seen_info = info;
  ++calls_a;
  if (info) {
    auto* aggregate = static_cast<const VkPhysicalDeviceVulkan12Features*>(info->pNext);
    EXPECT_EQ(aggregate->timelineSemaphore, VK_TRUE);
    EXPECT_EQ(aggregate->pNext, nullptr);
  }
  return VK_SUCCESS;
}
VkResult VKAPI_CALL RawCreateB(VkPhysicalDevice, const VkDeviceCreateInfo* info,
                             const VkAllocationCallbacks*, VkDevice*) {
  seen_info = info;
  ++calls_b;
  return VK_ERROR_FEATURE_NOT_PRESENT;
}
void VKAPI_CALL OtherFunction() {}
PFN_vkVoidFunction VKAPI_CALL RawGipaA(VkInstance, const char* name) {
  return name && std::strcmp(name, "vkCreateDevice") == 0
      ? reinterpret_cast<PFN_vkVoidFunction>(&RawCreateA) : &OtherFunction;
}
PFN_vkVoidFunction VKAPI_CALL RawGipaB(VkInstance, const char* name) {
  return name && std::strcmp(name, "vkCreateDevice") == 0
      ? reinterpret_cast<PFN_vkVoidFunction>(&RawCreateB) : &OtherFunction;
}
PFN_vkVoidFunction VKAPI_CALL MissingGipa(VkInstance, const char*) { return nullptr; }

TEST(Fsr3RuntimeDevice, ScopedGipaInterceptsOnlyDeviceCreationAndOwnsCallData) {
  calls_a = 0;
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
  timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  timeline.timelineSemaphore = VK_TRUE;
  VkPhysicalDeviceVulkan12Features aggregate{};
  aggregate.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  aggregate.pNext = &timeline;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &aggregate;
  ScopedFsr3VulkanDeviceCreate scope(&RawGipaA, VK_NULL_HANDLE);
  ASSERT_TRUE(scope.active());
  auto gipa = scope.get_instance_proc_addr();
  EXPECT_EQ(gipa(VK_NULL_HANDLE, "vkDestroyDevice"), &OtherFunction);
  auto create = reinterpret_cast<PFN_vkCreateDevice>(gipa(VK_NULL_HANDLE, "vkCreateDevice"));
  ASSERT_NE(create, &RawCreateA);
  EXPECT_EQ(create(VK_NULL_HANDLE, &info, nullptr, nullptr), VK_SUCCESS);
  EXPECT_EQ(calls_a, 1);
  EXPECT_NE(seen_info, &info);
  EXPECT_EQ(aggregate.pNext, &timeline);
}

TEST(Fsr3RuntimeDevice, NestedScopesRestoreRawDispatchAndRetainedThunkUsesFallback) {
  calls_a = calls_b = 0;
  PFN_vkGetInstanceProcAddr gipa = nullptr;
  PFN_vkCreateDevice create = nullptr;
  {
    ScopedFsr3VulkanDeviceCreate outer(&RawGipaA, VK_NULL_HANDLE);
    gipa = outer.get_instance_proc_addr();
    create = reinterpret_cast<PFN_vkCreateDevice>(gipa(VK_NULL_HANDLE, "vkCreateDevice"));
    EXPECT_EQ(create(VK_NULL_HANDLE, nullptr, nullptr, nullptr), VK_SUCCESS);
    {
      ScopedFsr3VulkanDeviceCreate inner(&RawGipaB, VK_NULL_HANDLE);
      EXPECT_EQ(create(VK_NULL_HANDLE, nullptr, nullptr, nullptr), VK_ERROR_FEATURE_NOT_PRESENT);
    }
    EXPECT_EQ(create(VK_NULL_HANDLE, nullptr, nullptr, nullptr), VK_SUCCESS);
  }
  EXPECT_EQ(calls_a, 2);
  EXPECT_EQ(calls_b, 1);
  EXPECT_EQ(gipa(VK_NULL_HANDLE, "vkCreateDevice"), reinterpret_cast<PFN_vkVoidFunction>(&RawCreateB));
  EXPECT_EQ(create(VK_NULL_HANDLE, nullptr, nullptr, nullptr), VK_ERROR_FEATURE_NOT_PRESENT);
  EXPECT_EQ(calls_b, 2);
  ScopedFsr3VulkanDeviceCreate::ClearFallback();
  EXPECT_EQ(gipa(VK_NULL_HANDLE, "vkCreateDevice"), nullptr);
  EXPECT_EQ(create(VK_NULL_HANDLE, nullptr, nullptr, nullptr), VK_ERROR_INITIALIZATION_FAILED);
}

TEST(Fsr3RuntimeDevice, MissingRawFunctionUsesOriginalGipaAndOtherThreadsUseRawFallback) {
  ScopedFsr3VulkanDeviceCreate missing(&MissingGipa, VK_NULL_HANDLE);
  EXPECT_FALSE(missing.active());
  EXPECT_EQ(missing.get_instance_proc_addr(), &MissingGipa);
  ScopedFsr3VulkanDeviceCreate scope(&RawGipaA, VK_NULL_HANDLE);
  auto gipa = scope.get_instance_proc_addr();
  bool used_raw = false;
  std::thread worker([&] {
    used_raw = gipa(VK_NULL_HANDLE, "vkCreateDevice") ==
        reinterpret_cast<PFN_vkVoidFunction>(&RawCreateA);
  });
  worker.join();
  EXPECT_TRUE(used_raw);
  ScopedFsr3VulkanDeviceCreate::ClearFallback();
}

TEST(Fsr3RuntimeDevice, RetainedDeviceCallbackOnAnotherThreadPreservesOriginalChain) {
  PFN_vkCreateDevice create = nullptr;
  {
    ScopedFsr3VulkanDeviceCreate scope(&RawGipaB, VK_NULL_HANDLE);
    create = reinterpret_cast<PFN_vkCreateDevice>(
        scope.get_instance_proc_addr()(VK_NULL_HANDLE, "vkCreateDevice"));
  }
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
  timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  timeline.timelineSemaphore = VK_TRUE;
  VkPhysicalDeviceVulkan12Features aggregate{};
  aggregate.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  aggregate.pNext = &timeline;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &aggregate;
  VkResult result = VK_SUCCESS;
  std::thread worker([&] { result = create(VK_NULL_HANDLE, &info, nullptr, nullptr); });
  worker.join();
  EXPECT_EQ(result, VK_ERROR_FEATURE_NOT_PRESENT);
  EXPECT_EQ(seen_info, &info);
  EXPECT_EQ(aggregate.pNext, &timeline);
  EXPECT_EQ(aggregate.timelineSemaphore, VK_FALSE);
  ScopedFsr3VulkanDeviceCreate::ClearFallback();
}
}  // namespace
}  // namespace mocktail::vr
