// Manual hardware integration probe. Uses the production backend to repeat
// the menu-to-experience graphics lifetime without Roblox or account data.
#include "mocktail/vr/openxr_backend.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <vector>
#include <openxr/openxr.h>

namespace mocktail::vr {
struct OpenXrBackendTestAccess {
  static std::uint64_t FrameNumber(const OpenXrBackend& b) { return b.xr_begun_frames_; }
  static bool FoveationSmoke(OpenXrBackend& b) {
    if (!b.foveation_.ready() || !b.foveation_.width() || !b.foveation_.height()) return false;
    if (!b.foveation_.acquired() && !b.foveation_.BeginFrame()) return false;
    b.foveation_.EndFrame();
    return true;
  }
  static void BlockEyeSubmission(OpenXrBackend& b) {
    b.eyes_[0].valid = true;
    b.eyes_[0].frame = b.published_pose_.frame;
    b.eyes_[0].image = reinterpret_cast<VkImage>(0x123400);
    b.eye_render_queues_.clear();
    b.eye_render_frames_.clear();
  }
  static void ClearFakeEyes(OpenXrBackend& b) { b.eyes_[0] = {}; b.eyes_[1] = {}; }
  static void* Session(const OpenXrBackend& b) { return b.xr_session_; }
  static bool AllocateMirrorCopies(OpenXrBackend& b, VkDevice device, unsigned family) {
    std::string error;
    return b.EnsureCopyResources(device,family,&error);
  }
  static bool CopiesReleased(const OpenXrBackend& b) {
    return b.copy_pool_==VK_NULL_HANDLE && b.copy_command_==VK_NULL_HANDLE && b.copy_fence_==VK_NULL_HANDLE;
  }
};
}

template<class T> T Load(PFN_vkGetInstanceProcAddr get, VkInstance instance, const char* name) {
  std::fprintf(stderr,"LOAD %s instance=%p dispatch=%p\n",name,static_cast<void*>(instance),*reinterpret_cast<void**>(instance));
  auto fn = reinterpret_cast<T>(get(instance,name));
  if (!fn) { std::fprintf(stderr,"Missing %s\n",name); std::exit(2); }
  return fn;
}
int main() {
  std::uint32_t extension_count = 0;
  if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr))) {
    std::vector<XrExtensionProperties> extensions(extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count, extensions.data())))
      for (const auto& extension : extensions)
        if (std::string(extension.extensionName).find("foveation") != std::string::npos ||
            std::string(extension.extensionName).find("eye_gaze") != std::string::npos ||
            std::string(extension.extensionName).find("swapchain_update") != std::string::npos)
          std::fprintf(stderr, "RUNTIME_CAPABILITY %s\n", extension.extensionName);
  }
  void* lib = dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);
  if (!lib) return 2;
  auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib,"vkGetInstanceProcAddr"));
  mocktail::vr::OpenXrBackend backend;
  const auto status=backend.Arm();
  if (!status.ok()) { std::fprintf(stderr,"%s\n",status.message().c_str()); return 2; }
  const bool defer_first=std::getenv("MOCKTAIL_VR_DEFER_INITIAL_SESSION") != nullptr;
  void* first_session = nullptr;
  VkDevice first_device = VK_NULL_HANDLE;
  for (int cycle=0; cycle<(defer_first ? 2 : 3); ++cycle) {
    std::fprintf(stderr,"CYCLE %d CREATE\n",cycle);
    VkApplicationInfo app{}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo=&app;
    VkInstance instance=VK_NULL_HANDLE; VkResult result=VK_ERROR_INITIALIZATION_FAILED;
#if defined(__aarch64__)
    if (cycle == 1 && backend.runtime_name() == "SteamVR/OpenXR") {
      auto incompatible = info;
      incompatible.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
      if (!backend.CreateVulkanInstance(&incompatible,nullptr,&instance,&result) ||
          result != VK_ERROR_INITIALIZATION_FAILED || instance != VK_NULL_HANDLE) return 5;
      std::fputs("INCOMPATIBLE_INSTANCE_REQUEST_REJECTED\n",stderr);
    }
#endif
    if (!backend.CreateVulkanInstance(&info,nullptr,&instance,&result) || result!=VK_SUCCESS) return 3;
    std::fprintf(stderr,"CREATED instance=%p dispatch=%p result=%d\n",static_cast<void*>(instance),*reinterpret_cast<void**>(instance),result);
    auto destroy = Load<PFN_vkDestroyInstance>(get,instance,"vkDestroyInstance");
    auto enumerate=Load<PFN_vkEnumeratePhysicalDevices>(get,instance,"vkEnumeratePhysicalDevices");
    std::uint32_t count=0; if (enumerate(instance,&count,nullptr)!=VK_SUCCESS || !count) return 3;
    std::vector<VkPhysicalDevice> devices(count); enumerate(instance,&count,devices.data());
    auto families=Load<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get,instance,"vkGetPhysicalDeviceQueueFamilyProperties");
    count=0; families(devices[0],&count,nullptr); std::vector<VkQueueFamilyProperties> props(count); families(devices[0],&count,props.data());
    std::uint32_t family=0; while (family<count && !(props[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family==count) return 3;
    float priority=1;
    VkDeviceQueueCreateInfo queue{}; queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; queue.queueFamilyIndex=family; queue.queueCount=1; queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo di{}; di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&queue;
    VkDevice device=VK_NULL_HANDLE;
    if (cycle == 1 && !defer_first) {
      auto incompatible = di;
      VkPhysicalDeviceFeatures feature{};
      feature.robustBufferAccess = VK_TRUE;
      incompatible.pEnabledFeatures = &feature;
      if (!backend.CreateVulkanDevice(devices[0], &incompatible, nullptr, &device, &result) ||
          result != VK_ERROR_INITIALIZATION_FAILED || device != VK_NULL_HANDLE) return 10;
      std::fputs("INCOMPATIBLE_DEVICE_REQUEST_REJECTED\n", stderr);
    }
    if (!backend.CreateVulkanDevice(devices[0],&di,nullptr,&device,&result) || result!=VK_SUCCESS) return 4;
    if (!cycle) {
      first_session = mocktail::vr::OpenXrBackendTestAccess::Session(backend);
      first_device = device;
    } else if (!defer_first) {
      if (device != first_device || first_session != mocktail::vr::OpenXrBackendTestAccess::Session(backend)) return 11;
      std::fputs("NATIVE_DEVICE_AND_LIVE_XR_SESSION_PRESERVED\n", stderr);
    }
    auto gdpa=Load<PFN_vkGetDeviceProcAddr>(get,instance,"vkGetDeviceProcAddr");
    auto destroy_device=reinterpret_cast<PFN_vkDestroyDevice>(gdpa(device,"vkDestroyDevice"));
    auto get_queue=reinterpret_cast<PFN_vkGetDeviceQueue>(gdpa(device,"vkGetDeviceQueue"));
    VkQueue render_queue=VK_NULL_HANDLE;
    get_queue(device,family,0,&render_queue);
    backend.NoteQueue(device,render_queue,family,0);
    if (!mocktail::vr::OpenXrBackendTestAccess::AllocateMirrorCopies(backend,device,family)) return 8;
    const auto previous_frame=mocktail::vr::OpenXrBackendTestAccess::FrameNumber(backend);
    if (!defer_first) mocktail::vr::OpenXrBackendTestAccess::BlockEyeSubmission(backend);
    for (int frame=0; frame<12; ++frame) {
      backend.NoteHostPresent(render_queue,device);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    mocktail::vr::OpenXrBackendTestAccess::ClearFakeEyes(backend);
    if (std::getenv("MOCKTAIL_VR_FOVEATION")) {
      if (!mocktail::vr::OpenXrBackendTestAccess::FoveationSmoke(backend)) return 12;
      std::fputs("EYE_TRACKED_FOVEATION_RUNTIME_MAPS_PASSED\n", stderr);
    }
    if (defer_first && cycle==0) {
      if (mocktail::vr::OpenXrBackendTestAccess::FrameNumber(backend) != previous_frame) return 7;
      std::fputs("LAUNCHER_XR_SESSION_CORRECTLY_DEFERRED\n",stderr);
    } else {
      if (mocktail::vr::OpenXrBackendTestAccess::FrameNumber(backend) <= previous_frame) return 6;
      std::fprintf(stderr,"CYCLE %d XR_FRAMES_ADVANCED\n",cycle);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::fprintf(stderr,"CYCLE %d DESTROY\n",cycle);
    const bool retained_device = backend.NoteDeviceDestroyed(device);
    if (!mocktail::vr::OpenXrBackendTestAccess::CopiesReleased(backend)) {
      std::fputs("MIRROR_COPY_RESOURCES_SURVIVED_DEVICE_TEARDOWN\n",stderr);
      return 9;
    }
    std::fputs("MIRROR_COPY_RESOURCES_RELEASED\n",stderr);
    if (!retained_device) destroy_device(device,nullptr);
    if (!backend.NoteInstanceDestroyed(instance)) destroy(instance,nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  backend.Disarm();
  std::puts(defer_first ? "DESKTOP_LAUNCHER_TO_LIVE_XR_GAME_CYCLE_PASSED" : "THREE_NATIVE_XR_GRAPHICS_CYCLES_PASSED");
}
