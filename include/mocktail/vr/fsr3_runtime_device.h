#pragma once

#include <atomic>
#include <cstring>
#include "mocktail/vr/fsr3_device_features.h"

namespace mocktail::vr {

// SteamVR can append a TimelineSemaphore feature record even when a guest
// already supplied Vulkan12Features. Fold that single promoted feature into
// its aggregate immediately before the host vkCreateDevice call. Every known
// record is cloned so no borrowed pNext pointer is rewritten. Unknown records,
// conflicting records, and malformed chains pass through verbatim.
class Fsr3RuntimeTimelineFeatures final {
 public:
  explicit Fsr3RuntimeTimelineFeatures(const VkDeviceCreateInfo& original)
      : owned_(original, Fsr3DeviceFeatureSupport{}) {
    if (original.sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO) return;
    const VkPhysicalDeviceVulkan12Features* aggregate = nullptr;
    const VkPhysicalDeviceTimelineSemaphoreFeatures* timeline = nullptr;
    std::vector<const VkBaseInStructure*> records;
    for (auto* record = static_cast<const VkBaseInStructure*>(original.pNext);
         record; record = record->pNext) {
      if (records.size() == 64 ||
          std::find(records.begin(), records.end(), record) != records.end()) return;
      if (record->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
        if (aggregate) return;
        aggregate = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(record);
      }
      if (record->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES) {
        if (timeline) return;
        timeline = reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(record);
      }
      records.push_back(record);
    }
    if (!aggregate || !timeline) return;
    std::uint64_t families = 0;
    VkPhysicalDeviceVulkan12Features* cloned_aggregate = nullptr;
    for (auto* record : records) {
      if (record == reinterpret_cast<const VkBaseInStructure*>(timeline)) continue;
      if (!owned_.CloneKnown(record, &families)) return;
      if (record == reinterpret_cast<const VkBaseInStructure*>(aggregate))
        cloned_aggregate = static_cast<VkPhysicalDeviceVulkan12Features*>(
            owned_.chain_.back()->address());
    }
    if (!cloned_aggregate || (owned_.features2_ && original.pEnabledFeatures)) return;
    cloned_aggregate->timelineSemaphore =
        aggregate->timelineSemaphore || timeline->timelineSemaphore ? VK_TRUE : VK_FALSE;
    for (std::size_t i = 0; i < owned_.chain_.size(); ++i)
      owned_.chain_[i]->next(i + 1 < owned_.chain_.size()
          ? owned_.chain_[i + 1]->address() : nullptr);
    owned_.info_.pNext = owned_.chain_.front()->address();
    changed_ = true;
  }
  Fsr3RuntimeTimelineFeatures(const Fsr3RuntimeTimelineFeatures&) = delete;
  Fsr3RuntimeTimelineFeatures& operator=(const Fsr3RuntimeTimelineFeatures&) = delete;
  const VkDeviceCreateInfo& create_info() const {
    return changed_ ? owned_.info_ : owned_.original_;
  }
  bool changed() const { return changed_; }

 private:
  Fsr3DeviceFeatures owned_;
  bool changed_ = false;
};

// Only calls made in this thread's scope normalize vkCreateDevice. Retained
// callbacks and calls on other threads use an atomic snapshot of the raw host
// dispatch, without normalization. The backend must clear that fallback before
// unloading its private host Vulkan library.
class ScopedFsr3VulkanDeviceCreate final {
 public:
  ScopedFsr3VulkanDeviceCreate(PFN_vkGetInstanceProcAddr raw, VkInstance instance)
      : raw_(raw), previous_(current_) {
    if (raw_)
      create_ = reinterpret_cast<PFN_vkCreateDevice>(raw_(instance, "vkCreateDevice"));
    if (create_ && create_ != &CreateDevice) {
      current_ = this;
      std::shared_ptr<const RawDispatch> dispatch =
          std::make_shared<RawDispatch>(RawDispatch{raw_, create_});
      std::atomic_store_explicit(&fallback_, std::move(dispatch), std::memory_order_release);
    } else create_ = nullptr;
  }
  ~ScopedFsr3VulkanDeviceCreate() {
    if (current_ == this) current_ = previous_;
  }
  ScopedFsr3VulkanDeviceCreate(const ScopedFsr3VulkanDeviceCreate&) = delete;
  ScopedFsr3VulkanDeviceCreate& operator=(const ScopedFsr3VulkanDeviceCreate&) = delete;
  PFN_vkGetInstanceProcAddr get_instance_proc_addr() const {
    return create_ ? &GetInstanceProcAddr : raw_;
  }
  bool active() const { return create_ != nullptr; }
  static void ClearFallback() {
    std::atomic_store_explicit(&fallback_, std::shared_ptr<const RawDispatch>{},
                               std::memory_order_release);
  }

 private:
  struct RawDispatch {
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkCreateDevice create = nullptr;
  };
  static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(
      VkInstance instance, const char* name) {
    auto* scope = current_;
    if (!scope || !scope->raw_) {
      const auto dispatch = std::atomic_load_explicit(&fallback_, std::memory_order_acquire);
      return dispatch && dispatch->gipa ? dispatch->gipa(instance, name) : nullptr;
    }
    if (name && std::strcmp(name, "vkCreateDevice") == 0)
      return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
    return scope->raw_(instance, name);
  }
  static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(
      VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
      const VkAllocationCallbacks* allocator, VkDevice* device) {
    auto* scope = current_;
    if (!scope || !scope->create_) {
      const auto dispatch = std::atomic_load_explicit(&fallback_, std::memory_order_acquire);
      return dispatch && dispatch->create
          ? dispatch->create(physical, info, allocator, device)
          : VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!info) return scope->create_(physical, info, allocator, device);
    Fsr3RuntimeTimelineFeatures normalized(*info);
    return scope->create_(physical, &normalized.create_info(), allocator, device);
  }
  inline static thread_local ScopedFsr3VulkanDeviceCreate* current_ = nullptr;
  inline static std::shared_ptr<const RawDispatch> fallback_;
  PFN_vkGetInstanceProcAddr raw_ = nullptr;
  PFN_vkCreateDevice create_ = nullptr;
  ScopedFsr3VulkanDeviceCreate* previous_ = nullptr;
};

}  // namespace mocktail::vr
