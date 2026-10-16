#pragma once

#include <vulkan/vulkan.h>

namespace nightcap::fidelityfx {

// Physical handles come from the same private host Vulkan loader as OpenXR.
// Resolve these queries explicitly so loading FidelityFX never promotes the
// host loader into the Android guest's global Vulkan symbol namespace.
struct HostVulkanDispatch {
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
    PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = nullptr;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;

    bool available() const {
        return vkGetDeviceProcAddr && vkEnumerateDeviceExtensionProperties &&
               vkGetPhysicalDeviceFeatures && vkGetPhysicalDeviceFeatures2 &&
               vkGetPhysicalDeviceProperties && vkGetPhysicalDeviceProperties2 &&
               vkGetPhysicalDeviceMemoryProperties;
    }
};

#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
const HostVulkanDispatch& PrivateHostVulkan();

}  // namespace nightcap::fidelityfx
