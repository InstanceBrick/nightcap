#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace mocktail::vr {

struct Fsr3DeviceFeatureSupport {
  bool shader_float16 = false;
  bool shader_int16 = false;
  bool float16_extension_available = false;
  bool shader_storage_image_extended_formats = false;
  bool shader_storage_image_write_without_format = false;
  // The effective instance/device API version, not merely a header version.
  std::uint32_t api_version = VK_API_VERSION_1_0;
};

// A call-scoped, owned device-create feature chain. This object must remain
// alive until xrCreateVulkanDeviceKHR/vkCreateDevice returns. Queue descriptions
// and layer names stay borrowed and unmodified, as in the original call.
// Unsupported or unfamiliar chains return the original create info verbatim.
class Fsr3DeviceFeatures final {
  // The runtime normalizer reuses the same typed ownership/whitelist without
  // relaxing the guest feature-chain validation performed by this class.
  friend class Fsr3RuntimeTimelineFeatures;
 public:
  Fsr3DeviceFeatures(const VkDeviceCreateInfo& original,
                     const Fsr3DeviceFeatureSupport& support)
      : original_(original), info_(original) {
    Build(support);
  }
  Fsr3DeviceFeatures(const Fsr3DeviceFeatures&) = delete;
  Fsr3DeviceFeatures& operator=(const Fsr3DeviceFeatures&) = delete;
  Fsr3DeviceFeatures(Fsr3DeviceFeatures&&) = delete;
  Fsr3DeviceFeatures& operator=(Fsr3DeviceFeatures&&) = delete;

  const VkDeviceCreateInfo& create_info() const {
    return enabled_ ? info_ : original_;
  }
  bool fp16_enabled() const { return enabled_; }
  bool changed() const { return enabled_ && changed_; }
  const char* reason() const { return reason_; }

 private:
  struct Record {
    virtual ~Record() = default;
    virtual void* address() = 0;
    virtual void next(void*) = 0;
  };
  template<class T> struct TypedRecord final : Record {
    explicit TypedRecord(const T& source) : value(source) {}
    void* address() override { return &value; }
    void next(void* value_next) override { value.pNext = value_next; }
    T value;
  };
  template<class T> T* Clone(const T& value) {
    auto record = std::make_unique<TypedRecord<T>>(value);
    auto* result = &record->value;
    chain_.push_back(std::move(record));
    return result;
  }
  static constexpr std::uint64_t Bit(unsigned value) { return 1ull << value; }
  static constexpr std::uint64_t Range(unsigned first, unsigned last) {
    return ((1ull << (last + 1)) - 1) & ~((1ull << first) - 1);
  }

  bool CloneKnown(const VkBaseInStructure* source, std::uint64_t* families) {
    std::uint64_t used = 0;
#define FEATURE(type, tag, mask) \
    case tag: { \
      used = (mask); \
      if (*families & used) return false; \
      Clone(*reinterpret_cast<const type*>(source)); \
      break; \
    }
    switch (source->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: {
        used = Bit(0);
        if (*families & used) return false;
        auto* value = Clone(*reinterpret_cast<const VkPhysicalDeviceFeatures2*>(source));
        base_features_ = &value->features;
        features2_ = true;
        break;
      }
      FEATURE(VkPhysicalDeviceVulkan11Features,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, Range(1, 6))
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
        used = Range(7, 19);
        if (*families & used) return false;
        auto* value = Clone(*reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(source));
        float16_ = &value->shaderFloat16;
        break;
      }
      FEATURE(VkPhysicalDeviceVulkan13Features,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, Range(20, 32))
      FEATURE(VkPhysicalDevice16BitStorageFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES, Bit(1))
      FEATURE(VkPhysicalDeviceMultiviewFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES, Bit(2))
      FEATURE(VkPhysicalDeviceVariablePointersFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES, Bit(3))
      FEATURE(VkPhysicalDeviceProtectedMemoryFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES, Bit(4))
      FEATURE(VkPhysicalDeviceSamplerYcbcrConversionFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES, Bit(5))
      FEATURE(VkPhysicalDeviceShaderDrawParametersFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES, Bit(6))
      FEATURE(VkPhysicalDevice8BitStorageFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES, Bit(7))
      FEATURE(VkPhysicalDeviceShaderAtomicInt64Features,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES, Bit(8))
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: {
        used = Bit(9);
        if (*families & used) return false;
        auto* value = Clone(*reinterpret_cast<const VkPhysicalDeviceShaderFloat16Int8Features*>(source));
        float16_ = &value->shaderFloat16;
        break;
      }
      FEATURE(VkPhysicalDeviceDescriptorIndexingFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, Bit(10))
      FEATURE(VkPhysicalDeviceScalarBlockLayoutFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES, Bit(11))
      FEATURE(VkPhysicalDeviceImagelessFramebufferFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES, Bit(12))
      FEATURE(VkPhysicalDeviceUniformBufferStandardLayoutFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES, Bit(13))
      FEATURE(VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES, Bit(14))
      FEATURE(VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES, Bit(15))
      FEATURE(VkPhysicalDeviceHostQueryResetFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES, Bit(16))
      FEATURE(VkPhysicalDeviceTimelineSemaphoreFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, Bit(17))
      FEATURE(VkPhysicalDeviceBufferDeviceAddressFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, Bit(18))
      FEATURE(VkPhysicalDeviceVulkanMemoryModelFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES, Bit(19))
      FEATURE(VkPhysicalDeviceImageRobustnessFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES, Bit(20))
      FEATURE(VkPhysicalDeviceInlineUniformBlockFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES, Bit(21))
      FEATURE(VkPhysicalDevicePipelineCreationCacheControlFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES, Bit(22))
      FEATURE(VkPhysicalDevicePrivateDataFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES, Bit(23))
      FEATURE(VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES, Bit(24))
      FEATURE(VkPhysicalDeviceShaderTerminateInvocationFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES, Bit(25))
      FEATURE(VkPhysicalDeviceSubgroupSizeControlFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES, Bit(26))
      FEATURE(VkPhysicalDeviceSynchronization2Features,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES, Bit(27))
      FEATURE(VkPhysicalDeviceTextureCompressionASTCHDRFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES, Bit(28))
      FEATURE(VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES, Bit(29))
      FEATURE(VkPhysicalDeviceDynamicRenderingFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, Bit(30))
      FEATURE(VkPhysicalDeviceShaderIntegerDotProductFeatures,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES, Bit(31))
      FEATURE(VkPhysicalDeviceMaintenance4Features,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES, Bit(32))
      FEATURE(VkPhysicalDeviceFragmentDensityMapFeaturesEXT,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT, Bit(33))
      FEATURE(VkPhysicalDeviceFragmentDensityMap2FeaturesEXT,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT, Bit(34))
      FEATURE(VkPhysicalDeviceHostImageCopyFeaturesEXT,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT, Bit(35))
      FEATURE(VkPhysicalDeviceFragmentShadingRateFeaturesKHR,
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR, Bit(36))
      default: return false;
    }
#undef FEATURE
    *families |= used;
    return true;
  }

  bool CopyExtensionNames() {
    if (original_.enabledExtensionCount && !original_.ppEnabledExtensionNames)
      return false;
    for (std::uint32_t i = 0; i < original_.enabledExtensionCount; ++i) {
      const char* name = original_.ppEnabledExtensionNames[i];
      if (!name) return false;
      std::size_t length = 0;
      while (length < VK_MAX_EXTENSION_NAME_SIZE && name[length]) ++length;
      if (!length || length == VK_MAX_EXTENSION_NAME_SIZE) return false;
      const std::string value(name, length);
      if (std::find(extension_names_.begin(), extension_names_.end(), value) == extension_names_.end())
        extension_names_.push_back(value);
      else changed_ = true;
    }
    return true;
  }

  void Build(const Fsr3DeviceFeatureSupport& support) {
    if (!support.shader_float16 || !support.shader_int16) {
      reason_ = "shaderFloat16 or shaderInt16 unsupported";
      return;
    }
    if (original_.sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO) {
      reason_ = "invalid device create info";
      return;
    }
    std::uint64_t families = 0;
    std::vector<const void*> seen;
    for (auto* next = static_cast<const VkBaseInStructure*>(original_.pNext);
         next; next = next->pNext) {
      if (seen.size() == 64 || std::find(seen.begin(), seen.end(), next) != seen.end() ||
          !CloneKnown(next, &families)) {
        reason_ = "unknown, cyclic, or duplicate feature chain";
        return;
      }
      if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES)
        // The Vulkan11 aggregate structure itself was introduced in1.2.
        minimum_api_ = std::max(minimum_api_, VK_API_VERSION_1_2);
      if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
        minimum_api_ = std::max(minimum_api_, VK_API_VERSION_1_2);
      if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES)
        minimum_api_ = std::max(minimum_api_, VK_API_VERSION_1_3);
      seen.push_back(next);
    }
    if (features2_ && original_.pEnabledFeatures) {
      reason_ = "duplicate base feature records";
      return;
    }
    if (support.api_version < minimum_api_) {
      reason_ = "core feature record exceeds effective Vulkan API";
      return;
    }
    if (support.api_version < VK_API_VERSION_1_2 && !support.float16_extension_available) {
      reason_ = "shaderFloat16 extension unavailable";
      return;
    }
    if (!CopyExtensionNames()) {
      reason_ = "invalid device extension list";
      return;
    }
    if (support.api_version < VK_API_VERSION_1_2) {
      const std::string extension = VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME;
      if (std::find(extension_names_.begin(), extension_names_.end(), extension) == extension_names_.end()) {
        extension_names_.push_back(extension);
        changed_ = true;
      }
    }
    if (!float16_) {
      VkPhysicalDeviceShaderFloat16Int8Features value{};
      value.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
      float16_ = &Clone(value)->shaderFloat16;
      changed_ = true;
    }
    if (!base_features_) {
      if (original_.pEnabledFeatures) features_ = *original_.pEnabledFeatures;
      base_features_ = &features_;
      info_.pEnabledFeatures = base_features_;
    }
    changed_ |= *float16_ != VK_TRUE || base_features_->shaderInt16 != VK_TRUE;
    *float16_ = VK_TRUE;
    base_features_->shaderInt16 = VK_TRUE;
    if (support.shader_storage_image_extended_formats) {
      changed_ |= base_features_->shaderStorageImageExtendedFormats != VK_TRUE;
      base_features_->shaderStorageImageExtendedFormats = VK_TRUE;
    }
    if (support.shader_storage_image_write_without_format) {
      changed_ |= base_features_->shaderStorageImageWriteWithoutFormat != VK_TRUE;
      base_features_->shaderStorageImageWriteWithoutFormat = VK_TRUE;
    }
    for (std::size_t i = 0; i < chain_.size(); ++i)
      chain_[i]->next(i + 1 < chain_.size() ? chain_[i + 1]->address() : nullptr);
    info_.pNext = chain_.empty() ? nullptr : chain_[0]->address();
    for (const auto& name : extension_names_) extension_pointers_.push_back(name.c_str());
    info_.enabledExtensionCount = static_cast<std::uint32_t>(extension_pointers_.size());
    info_.ppEnabledExtensionNames = extension_pointers_.empty() ? nullptr : extension_pointers_.data();
    enabled_ = true;
    reason_ = "enabled";
  }

  const VkDeviceCreateInfo& original_;
  VkDeviceCreateInfo info_;
  std::vector<std::unique_ptr<Record>> chain_;
  std::vector<std::string> extension_names_;
  std::vector<const char*> extension_pointers_;
  VkPhysicalDeviceFeatures features_{};
  VkPhysicalDeviceFeatures* base_features_ = nullptr;
  VkBool32* float16_ = nullptr;
  bool features2_ = false;
  std::uint32_t minimum_api_ = VK_API_VERSION_1_0;
  bool enabled_ = false;
  bool changed_ = false;
  const char* reason_ = "disabled";
};

}  // namespace mocktail::vr
