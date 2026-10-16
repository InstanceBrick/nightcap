#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "mocktail/vr/fsr3_device_features.h"

namespace mocktail::vr {
namespace {

Fsr3DeviceFeatureSupport Supported(std::uint32_t api = VK_API_VERSION_1_3) {
  Fsr3DeviceFeatureSupport value;
  value.shader_float16 = true;
  value.shader_int16 = true;
  value.float16_extension_available = true;
  value.api_version = api;
  return value;
}

template<class T> const T* Find(const VkDeviceCreateInfo& info, VkStructureType type) {
  for (auto* node = static_cast<const VkBaseInStructure*>(info.pNext); node; node = node->pNext)
    if (node->sType == type) return reinterpret_cast<const T*>(node);
  return nullptr;
}

std::vector<std::string> Extensions(const VkDeviceCreateInfo& info) {
  std::vector<std::string> result;
  for (std::uint32_t i = 0; i < info.enabledExtensionCount; ++i)
    result.emplace_back(info.ppEnabledExtensionNames[i]);
  return result;
}

TEST(Fsr3DeviceFeatures, ClonesCoreAndFrameFeaturesWithoutMutatingGuestFields) {
  VkPhysicalDeviceHostImageCopyFeaturesEXT host{};
  host.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT;
  host.hostImageCopy = VK_TRUE;
  VkPhysicalDeviceFragmentDensityMapFeaturesEXT density{};
  density.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT;
  density.pNext = &host;
  density.fragmentDensityMap = VK_TRUE;
  density.fragmentDensityMapDynamic = VK_TRUE;
  VkPhysicalDeviceVulkan13Features v13{};
  v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  v13.pNext = &density;
  v13.synchronization2 = VK_TRUE;
  v13.dynamicRendering = VK_TRUE;
  VkPhysicalDeviceVulkan12Features v12{};
  v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  v12.pNext = &v13;
  v12.timelineSemaphore = VK_TRUE;
  v12.hostQueryReset = VK_TRUE;
  v12.shaderInt8 = VK_TRUE;
  VkPhysicalDeviceVulkan11Features v11{};
  v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
  v11.pNext = &v12;
  v11.samplerYcbcrConversion = VK_TRUE;
  v11.storageBuffer16BitAccess = VK_TRUE;
  VkPhysicalDeviceFeatures2 base{};
  base.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  base.pNext = &v11;
  base.features.robustBufferAccess = VK_TRUE;
  base.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
  const auto old_base = base;
  const auto old_v11 = v11;
  const auto old_v12 = v12;
  const auto old_v13 = v13;
  const auto old_density = density;
  const auto old_host = host;
  VkDeviceQueueCreateInfo queue{};
  queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue.flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
  queue.queueFamilyIndex = 4;
  queue.queueCount = 1;
  const char* names[] = {VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME,
                         VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME};
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &base;
  info.queueCreateInfoCount = 1;
  info.pQueueCreateInfos = &queue;
  info.enabledExtensionCount = 2;
  info.ppEnabledExtensionNames = names;
  Fsr3DeviceFeatures helper(info, Supported());
  ASSERT_TRUE(helper.fp16_enabled()) << helper.reason();
  EXPECT_TRUE(helper.changed());
  const auto& out = helper.create_info();
  const auto* out_base = Find<VkPhysicalDeviceFeatures2>(out, base.sType);
  const auto* out_v11 = Find<VkPhysicalDeviceVulkan11Features>(out, v11.sType);
  const auto* out_v12 = Find<VkPhysicalDeviceVulkan12Features>(out, v12.sType);
  const auto* out_v13 = Find<VkPhysicalDeviceVulkan13Features>(out, v13.sType);
  const auto* out_density = Find<VkPhysicalDeviceFragmentDensityMapFeaturesEXT>(out, density.sType);
  const auto* out_host = Find<VkPhysicalDeviceHostImageCopyFeaturesEXT>(out, host.sType);
  ASSERT_NE(out_base, nullptr);
  ASSERT_NE(out_v11, nullptr);
  ASSERT_NE(out_v12, nullptr);
  ASSERT_NE(out_v13, nullptr);
  ASSERT_NE(out_density, nullptr);
  ASSERT_NE(out_host, nullptr);
  EXPECT_NE(out_base, &base);
  EXPECT_NE(out_v12, &v12);
  EXPECT_EQ(out_base->features.shaderInt16, VK_TRUE);
  EXPECT_EQ(out_base->features.robustBufferAccess, VK_TRUE);
  EXPECT_EQ(out_v12->shaderFloat16, VK_TRUE);
  EXPECT_EQ(out_v12->shaderInt8, VK_TRUE);
  EXPECT_EQ(out_v12->timelineSemaphore, VK_TRUE);
  EXPECT_EQ(out_v12->hostQueryReset, VK_TRUE);
  EXPECT_EQ(out_v11->storageBuffer16BitAccess, VK_TRUE);
  EXPECT_EQ(out_v11->samplerYcbcrConversion, VK_TRUE);
  EXPECT_EQ(out_v13->dynamicRendering, VK_TRUE);
  EXPECT_EQ(out_v13->synchronization2, VK_TRUE);
  EXPECT_EQ(out_density->fragmentDensityMapDynamic, VK_TRUE);
  EXPECT_EQ(out_host->hostImageCopy, VK_TRUE);
  EXPECT_EQ(out.flags, info.flags);
  EXPECT_EQ(out.pQueueCreateInfos, info.pQueueCreateInfos);
  EXPECT_EQ(out.queueCreateInfoCount, info.queueCreateInfoCount);
  EXPECT_EQ(out.pEnabledFeatures, nullptr);
  EXPECT_EQ(Extensions(out), Extensions(info));
  EXPECT_EQ(std::memcmp(&base, &old_base, sizeof(base)), 0);
  EXPECT_EQ(std::memcmp(&v11, &old_v11, sizeof(v11)), 0);
  EXPECT_EQ(std::memcmp(&v12, &old_v12, sizeof(v12)), 0);
  EXPECT_EQ(std::memcmp(&v13, &old_v13, sizeof(v13)), 0);
  EXPECT_EQ(std::memcmp(&density, &old_density, sizeof(density)), 0);
  EXPECT_EQ(std::memcmp(&host, &old_host, sizeof(host)), 0);
}

TEST(Fsr3DeviceFeatures, AlreadyEnabledStaysEnabledWithoutAdditionalFeatures) {
  VkPhysicalDeviceShaderFloat16Int8Features fp{};
  fp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
  fp.shaderFloat16 = VK_TRUE;
  VkPhysicalDeviceFeatures base{};
  base.shaderInt16 = VK_TRUE;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &fp;
  info.pEnabledFeatures = &base;
  auto support = Supported(VK_API_VERSION_1_2);
  support.float16_extension_available = false;
  Fsr3DeviceFeatures helper(info, support);
  ASSERT_TRUE(helper.fp16_enabled());
  EXPECT_FALSE(helper.changed());
  const auto* out = Find<VkPhysicalDeviceShaderFloat16Int8Features>(helper.create_info(), fp.sType);
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(out->shaderFloat16, VK_TRUE);
  EXPECT_EQ(out->shaderInt8, VK_FALSE);
  EXPECT_EQ(helper.create_info().pEnabledFeatures->shaderInt16, VK_TRUE);
}

TEST(Fsr3DeviceFeatures, AppendsFloat16AndRequiredExtensionOnVulkan11) {
  VkPhysicalDeviceFeatures base{};
  base.shaderFloat64 = VK_TRUE;
  const auto snapshot = base;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pEnabledFeatures = &base;
  Fsr3DeviceFeatures helper(info, Supported(VK_API_VERSION_1_1));
  ASSERT_TRUE(helper.fp16_enabled());
  const auto& out = helper.create_info();
  const auto* fp = Find<VkPhysicalDeviceShaderFloat16Int8Features>(
      out, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES);
  ASSERT_NE(fp, nullptr);
  EXPECT_EQ(fp->shaderFloat16, VK_TRUE);
  EXPECT_EQ(fp->shaderInt8, VK_FALSE);
  EXPECT_EQ(out.pEnabledFeatures->shaderFloat64, VK_TRUE);
  EXPECT_EQ(out.pEnabledFeatures->shaderInt16, VK_TRUE);
  EXPECT_NE(out.pEnabledFeatures, &base);
  EXPECT_EQ(Extensions(out), (std::vector<std::string>{VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME}));
  EXPECT_EQ(std::memcmp(&base, &snapshot, sizeof(base)), 0);
}

TEST(Fsr3DeviceFeatures, CoreVulkan12DoesNotRequireThePromotedExtensionName) {
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  auto support = Supported(VK_API_VERSION_1_2);
  support.float16_extension_available = false;
  Fsr3DeviceFeatures helper(info, support);
  ASSERT_TRUE(helper.fp16_enabled());
  EXPECT_TRUE(Extensions(helper.create_info()).empty());
  EXPECT_EQ(helper.create_info().pEnabledFeatures->shaderInt16, VK_TRUE);
}

TEST(Fsr3DeviceFeatures, CopiesAndDeduplicatesOwnedExtensionNames) {
  char mutable_name[] = "VK_EXT_host_image_copy";
  const char* names[] = {mutable_name, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
                        VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME, mutable_name};
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.enabledExtensionCount = 4;
  info.ppEnabledExtensionNames = names;
  Fsr3DeviceFeatures helper(info, Supported(VK_API_VERSION_1_1));
  ASSERT_TRUE(helper.fp16_enabled());
  mutable_name[0] = 'X';
  EXPECT_EQ(Extensions(helper.create_info()),
            (std::vector<std::string>{"VK_EXT_host_image_copy", VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME}));
  EXPECT_EQ(info.enabledExtensionCount, 4u);
  EXPECT_EQ(info.ppEnabledExtensionNames, names);
}

TEST(Fsr3DeviceFeatures, UnsupportedCapabilitiesKeepOriginalCreateInfoVerbatim) {
  VkPhysicalDeviceVulkan12Features fp{};
  fp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &fp;
  for (int missing = 0; missing < 2; ++missing) {
    auto support = Supported();
    if (missing == 0) support.shader_float16 = false;
    else support.shader_int16 = false;
    Fsr3DeviceFeatures helper(info, support);
    EXPECT_FALSE(helper.fp16_enabled());
    EXPECT_FALSE(helper.changed());
    EXPECT_EQ(&helper.create_info(), &info);
    EXPECT_EQ(fp.shaderFloat16, VK_FALSE);
  }
  auto old_api = Supported(VK_API_VERSION_1_1);
  old_api.float16_extension_available = false;
  VkDeviceCreateInfo plain{};
  plain.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  Fsr3DeviceFeatures helper(plain, old_api);
  EXPECT_FALSE(helper.fp16_enabled());
  EXPECT_EQ(&helper.create_info(), &plain);
}

TEST(Fsr3DeviceFeatures, UnknownRecordAfterKnownRecordRejectsWholeEdit) {
  VkBaseOutStructure unknown{};
  unknown.sType = VK_STRUCTURE_TYPE_MAX_ENUM;
  VkPhysicalDeviceShaderFloat16Int8Features fp{};
  fp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
  fp.pNext = &unknown;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &fp;
  Fsr3DeviceFeatures helper(info, Supported());
  EXPECT_FALSE(helper.fp16_enabled());
  EXPECT_EQ(&helper.create_info(), &info);
  EXPECT_EQ(fp.shaderFloat16, VK_FALSE);
  EXPECT_EQ(fp.pNext, &unknown);
}

TEST(Fsr3DeviceFeatures, DuplicateEquivalentAggregateRecordsFailClosed) {
  VkPhysicalDeviceShaderFloat16Int8Features fp{};
  fp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
  VkPhysicalDeviceVulkan12Features v12{};
  v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  v12.pNext = &fp;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &v12;
  Fsr3DeviceFeatures duplicate_float(info, Supported());
  EXPECT_FALSE(duplicate_float.fp16_enabled());
  EXPECT_EQ(&duplicate_float.create_info(), &info);
  VkPhysicalDevice16BitStorageFeatures storage{};
  storage.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
  VkPhysicalDeviceVulkan11Features v11{};
  v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
  v11.pNext = &storage;
  info.pNext = &v11;
  Fsr3DeviceFeatures duplicate_storage(info, Supported());
  EXPECT_FALSE(duplicate_storage.fp16_enabled());
  VkPhysicalDeviceSynchronization2Features synchronization{};
  synchronization.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
  VkPhysicalDeviceVulkan13Features v13{};
  v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  v13.pNext = &synchronization;
  info.pNext = &v13;
  Fsr3DeviceFeatures duplicate_sync(info, Supported());
  EXPECT_FALSE(duplicate_sync.fp16_enabled());
}

TEST(Fsr3DeviceFeatures, DuplicateSameTypeAndCyclicChainsFailClosed) {
  VkPhysicalDeviceShaderFloat16Int8Features first{}, second{};
  first.sType = second.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
  first.pNext = &second;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &first;
  Fsr3DeviceFeatures duplicate(info, Supported());
  EXPECT_FALSE(duplicate.fp16_enabled());
  first.pNext = &first;
  Fsr3DeviceFeatures cycle(info, Supported());
  EXPECT_FALSE(cycle.fp16_enabled());
  EXPECT_EQ(first.pNext, &first);
}

TEST(Fsr3DeviceFeatures, RejectsDuplicateBaseFeaturesAndInvalidExtensionArrays) {
  VkPhysicalDeviceFeatures2 base{};
  base.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  VkPhysicalDeviceFeatures legacy{};
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &base;
  info.pEnabledFeatures = &legacy;
  Fsr3DeviceFeatures duplicate(info, Supported());
  EXPECT_FALSE(duplicate.fp16_enabled());
  info.pNext = nullptr;
  info.enabledExtensionCount = 1;
  Fsr3DeviceFeatures missing_array(info, Supported());
  EXPECT_FALSE(missing_array.fp16_enabled());
  const char* null_name[] = {nullptr};
  info.ppEnabledExtensionNames = null_name;
  Fsr3DeviceFeatures missing_name(info, Supported());
  EXPECT_FALSE(missing_name.fp16_enabled());
}

TEST(Fsr3DeviceFeatures, PreservesStandaloneFeatureChainAndDoesNotEnableStorage16) {
  VkPhysicalDeviceSynchronization2Features sync{};
  sync.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
  sync.synchronization2 = VK_TRUE;
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{};
  timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  timeline.pNext = &sync;
  timeline.timelineSemaphore = VK_TRUE;
  VkPhysicalDevice16BitStorageFeatures storage{};
  storage.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
  storage.pNext = &timeline;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &storage;
  Fsr3DeviceFeatures helper(info, Supported());
  ASSERT_TRUE(helper.fp16_enabled());
  const auto& out = helper.create_info();
  const auto* out_storage = Find<VkPhysicalDevice16BitStorageFeatures>(out, storage.sType);
  ASSERT_NE(out_storage, nullptr);
  EXPECT_EQ(out_storage->storageBuffer16BitAccess, VK_FALSE);
  EXPECT_EQ(out_storage->uniformAndStorageBuffer16BitAccess, VK_FALSE);
  EXPECT_EQ(out_storage->storagePushConstant16, VK_FALSE);
  EXPECT_EQ(out_storage->storageInputOutput16, VK_FALSE);
  EXPECT_EQ(Find<VkPhysicalDeviceTimelineSemaphoreFeatures>(out, timeline.sType)->timelineSemaphore, VK_TRUE);
  EXPECT_EQ(Find<VkPhysicalDeviceSynchronization2Features>(out, sync.sType)->synchronization2, VK_TRUE);
}

TEST(Fsr3DeviceFeatures, CoreAggregateVersionCannotExceedEffectiveApi) {
  VkPhysicalDeviceVulkan11Features v11{};
  v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &v11;
  Fsr3DeviceFeatures old_instance(info, Supported(VK_API_VERSION_1_0));
  EXPECT_FALSE(old_instance.fp16_enabled());
  EXPECT_EQ(&old_instance.create_info(), &info);
  Fsr3DeviceFeatures pre_aggregate_api(info, Supported(VK_API_VERSION_1_1));
  EXPECT_FALSE(pre_aggregate_api.fp16_enabled());
  VkPhysicalDeviceVulkan13Features v13{};
  v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  info.pNext = &v13;
  Fsr3DeviceFeatures old_device(info, Supported(VK_API_VERSION_1_2));
  EXPECT_FALSE(old_device.fp16_enabled());
  EXPECT_EQ(&old_device.create_info(), &info);
}

TEST(Fsr3DeviceFeatures, SupportedStorageImageFeaturesEnableOnlyOwnedBaseClone) {
  VkPhysicalDeviceFeatures2 base{};
  base.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  base.features.geometryShader = VK_TRUE;
  const auto original = base;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pNext = &base;
  auto support = Supported();
  support.shader_storage_image_extended_formats = true;
  support.shader_storage_image_write_without_format = true;
  Fsr3DeviceFeatures helper(info, support);
  ASSERT_TRUE(helper.fp16_enabled());
  const auto* clone = Find<VkPhysicalDeviceFeatures2>(helper.create_info(), base.sType);
  ASSERT_NE(clone, nullptr);
  EXPECT_EQ(clone->features.shaderStorageImageExtendedFormats, VK_TRUE);
  EXPECT_EQ(clone->features.shaderStorageImageWriteWithoutFormat, VK_TRUE);
  EXPECT_EQ(clone->features.geometryShader, VK_TRUE);
  EXPECT_EQ(std::memcmp(&base, &original, sizeof(base)), 0);
}

TEST(Fsr3DeviceFeatures, UnsupportedStorageImageFeaturesAreNeverAdded) {
  VkPhysicalDeviceFeatures base{};
  base.robustBufferAccess = VK_TRUE;
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.pEnabledFeatures = &base;
  for (int supported = 0; supported < 3; ++supported) {
    auto support = Supported();
    support.shader_storage_image_extended_formats = supported == 1;
    support.shader_storage_image_write_without_format = supported == 2;
    Fsr3DeviceFeatures helper(info, support);
    ASSERT_TRUE(helper.fp16_enabled());
    const auto& out = *helper.create_info().pEnabledFeatures;
    EXPECT_EQ(out.shaderStorageImageExtendedFormats, supported == 1 ? VK_TRUE : VK_FALSE);
    EXPECT_EQ(out.shaderStorageImageWriteWithoutFormat, supported == 2 ? VK_TRUE : VK_FALSE);
    EXPECT_EQ(out.robustBufferAccess, VK_TRUE);
    EXPECT_EQ(base.shaderStorageImageExtendedFormats, VK_FALSE);
    EXPECT_EQ(base.shaderStorageImageWriteWithoutFormat, VK_FALSE);
  }
}

}  // namespace
}  // namespace mocktail::vr
