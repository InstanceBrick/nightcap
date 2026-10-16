#include "ffx_vk_private_loader.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace nightcap::fidelityfx {

const HostVulkanDispatch& PrivateHostVulkan() {
    static const HostVulkanDispatch dispatch = [] {
        HostVulkanDispatch value;
#if defined(_WIN32)
        const HMODULE library = LoadLibraryA("vulkan-1.dll");
        const auto resolve = [library](const char* name) -> void* {
            return library ? reinterpret_cast<void*>(GetProcAddress(library, name)) : nullptr;
        };
#else
        // Retain this private reference for the process lifetime: cached PFNs
        // and host physical-device handles must never outlive their loader.
        void* const library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        const auto resolve = [library](const char* name) -> void* {
            return library ? dlsym(library, name) : nullptr;
        };
#endif
#define RESOLVE(name) value.name = reinterpret_cast<PFN_##name>(resolve(#name))
        RESOLVE(vkGetDeviceProcAddr);
        RESOLVE(vkEnumerateDeviceExtensionProperties);
        RESOLVE(vkGetPhysicalDeviceFeatures);
        RESOLVE(vkGetPhysicalDeviceFeatures2);
        RESOLVE(vkGetPhysicalDeviceProperties);
        RESOLVE(vkGetPhysicalDeviceProperties2);
        RESOLVE(vkGetPhysicalDeviceMemoryProperties);
#undef RESOLVE
        if (!value.vkGetPhysicalDeviceFeatures2)
            value.vkGetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
                resolve("vkGetPhysicalDeviceFeatures2KHR"));
        if (!value.vkGetPhysicalDeviceProperties2)
            value.vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                resolve("vkGetPhysicalDeviceProperties2KHR"));
        return value;
    }();
    return dispatch;
}

}  // namespace nightcap::fidelityfx
