#include "mocktail/vr/xr_foveation_maps.h"
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace mocktail::vr {
namespace {
template<class T> T Info(XrStructureType type) { T result{}; result.type = type; return result; }
void Check(XrResult result, const char* operation) {
  if (XR_FAILED(result)) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
template<class T> T Function(XrInstance instance, const char* name) {
  PFN_xrVoidFunction fn = nullptr;
  Check(xrGetInstanceProcAddr(instance, name, &fn), name);
  if (!fn) throw std::runtime_error(std::string("Missing ") + name);
  return reinterpret_cast<T>(fn);
}
}
struct XrFoveationMaps::Impl {
  XrInstance instance = XR_NULL_HANDLE;
  XrSession session = XR_NULL_HANDLE;
  XrSwapchain swapchain = XR_NULL_HANDLE;
  XrFoveationProfileFB profile = XR_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  PFN_vkCreateImageView create_view = nullptr;
  PFN_vkDestroyImageView destroy_view = nullptr;
  PFN_xrDestroyFoveationProfileFB destroy_profile = nullptr;
  PFN_xrGetFoveationEyeTrackedStateMETA get_tracking = nullptr;
  std::vector<std::array<VkImageView, 2>> views;
  std::uint32_t width = 0, height = 0, index = 0;
  bool ready = false, acquired = false, tracking = false;
  bool tracking_reported = false;
  std::array<XrVector2f,2> centers{};
};
XrFoveationMaps::XrFoveationMaps() : impl_(std::make_unique<Impl>()) {}
XrFoveationMaps::~XrFoveationMaps() { Destroy(); }

Status XrFoveationMaps::Initialize(void* opaque_instance, std::uint64_t system,
    void* opaque_session, VkDevice device, PFN_vkGetDeviceProcAddr get,
    std::uint32_t width, std::uint32_t height, VkFormat format) {
  Destroy();
  auto& p = *impl_;
  try {
    p.instance = static_cast<XrInstance>(opaque_instance);
    p.session = static_cast<XrSession>(opaque_session);
    p.device = device;
    if (!p.instance || !p.session || !device || !get || !width || !height)
      throw std::runtime_error("Incomplete foveation graphics binding");
    auto eye_support = Info<XrSystemFoveationEyeTrackedPropertiesMETA>(XR_TYPE_SYSTEM_FOVEATION_EYE_TRACKED_PROPERTIES_META);
    auto properties = Info<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
    properties.next = &eye_support;
    Check(xrGetSystemProperties(p.instance, system, &properties), "xrGetSystemProperties(foveation)");
    if (!eye_support.supportsFoveationEyeTracked)
      throw std::runtime_error("Runtime system has no eye-tracked foveation");
    p.create_view = reinterpret_cast<PFN_vkCreateImageView>(get(device, "vkCreateImageView"));
    p.destroy_view = reinterpret_cast<PFN_vkDestroyImageView>(get(device, "vkDestroyImageView"));
    if (!p.create_view || !p.destroy_view) throw std::runtime_error("Missing density-map image-view functions");
    const auto create_profile = Function<PFN_xrCreateFoveationProfileFB>(p.instance, "xrCreateFoveationProfileFB");
    p.destroy_profile = Function<PFN_xrDestroyFoveationProfileFB>(p.instance, "xrDestroyFoveationProfileFB");
    p.get_tracking = Function<PFN_xrGetFoveationEyeTrackedStateMETA>(p.instance, "xrGetFoveationEyeTrackedStateMETA");
    const auto update = Function<PFN_xrUpdateSwapchainFB>(p.instance, "xrUpdateSwapchainFB");

    auto eye = Info<XrFoveationEyeTrackedProfileCreateInfoMETA>(XR_TYPE_FOVEATION_EYE_TRACKED_PROFILE_CREATE_INFO_META);
    auto level = Info<XrFoveationLevelProfileCreateInfoFB>(XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB);
    level.next = &eye;
    level.level = XR_FOVEATION_LEVEL_MEDIUM_FB;
    level.dynamic = XR_FOVEATION_DYNAMIC_DISABLED_FB;
    auto profile = Info<XrFoveationProfileCreateInfoFB>(XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB);
    profile.next = &level;
    Check(create_profile(p.session, &profile, &p.profile), "xrCreateFoveationProfileFB(eye-tracked)");

    auto foveation = Info<XrSwapchainCreateInfoFoveationFB>(XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB);
    foveation.flags = XR_SWAPCHAIN_CREATE_FOVEATION_FRAGMENT_DENSITY_MAP_BIT_FB;
    auto create = Info<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
    create.next = &foveation;
    create.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    create.format = format;
    create.sampleCount = 1;
    create.width = width; create.height = height;
    create.faceCount = 1; create.arraySize = 2; create.mipCount = 1;
    Check(xrCreateSwapchain(p.session, &create, &p.swapchain), "xrCreateSwapchain(foveation control)");
    auto state = Info<XrSwapchainStateFoveationFB>(XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB);
    state.profile = p.profile;
    Check(update(p.swapchain, reinterpret_cast<const XrSwapchainStateBaseHeaderFB*>(&state)), "xrUpdateSwapchainFB(foveation)");

    std::uint32_t count = 0;
    Check(xrEnumerateSwapchainImages(p.swapchain, 0, &count, nullptr), "xrEnumerateSwapchainImages(foveation count)");
    if (!count || count > 32) throw std::runtime_error("Invalid foveation control image count");
    std::vector<XrSwapchainImageVulkan2KHR> color(count, Info<XrSwapchainImageVulkan2KHR>(XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR));
    std::vector<XrSwapchainImageFoveationVulkanFB> maps(count, Info<XrSwapchainImageFoveationVulkanFB>(XR_TYPE_SWAPCHAIN_IMAGE_FOVEATION_VULKAN_FB));
    for (unsigned i = 0; i < count; ++i) color[i].next = &maps[i];
    Check(xrEnumerateSwapchainImages(p.swapchain, count, &count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(color.data())), "xrEnumerateSwapchainImages(foveation maps)");
    p.views.resize(count, {VK_NULL_HANDLE, VK_NULL_HANDLE});
    for (unsigned i = 0; i < count; ++i) {
      if (!maps[i].image || !maps[i].width || !maps[i].height)
        throw std::runtime_error("Runtime returned an empty fragment-density map");
      if (i && (maps[i].width != p.width || maps[i].height != p.height))
        throw std::runtime_error("Runtime density-map extents differ");
      p.width = maps[i].width; p.height = maps[i].height;
      for (unsigned eye_index = 0; eye_index < 2; ++eye_index) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = maps[i].image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8G8_UNORM;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, eye_index, 1};
        if (p.create_view(device, &view, nullptr, &p.views[i][eye_index]) != VK_SUCCESS)
          throw std::runtime_error("Cannot create runtime density-map eye view");
      }
    }
    p.ready = true;
    std::fprintf(stderr, "  [vr-foveation] runtime eye-tracked density maps ready: %ux%u, %u stereo images\n", p.width, p.height, count);
    return Status::Ok();
  } catch (const std::exception& error) {
    Destroy();
    return Status::Error(StatusCode::kUnavailable, error.what());
  }
}
bool XrFoveationMaps::BeginFrame() {
  auto& p = *impl_;
  if (!p.ready || p.acquired) return false;
  auto acquire = Info<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
  if (XR_FAILED(xrAcquireSwapchainImage(p.swapchain, &acquire, &p.index))) return false;
  auto wait = Info<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
  wait.timeout = XR_INFINITE_DURATION;
  if (XR_FAILED(xrWaitSwapchainImage(p.swapchain, &wait))) return false;
  p.acquired = true;
  auto eye = Info<XrFoveationEyeTrackedStateMETA>(XR_TYPE_FOVEATION_EYE_TRACKED_STATE_META);
  const bool valid = XR_SUCCEEDED(p.get_tracking(p.session, &eye)) &&
      (eye.flags & XR_FOVEATION_EYE_TRACKED_STATE_VALID_BIT_META) != 0;
  if (valid != p.tracking || !p.tracking_reported) {
    std::fprintf(stderr, "  [vr-foveation] gaze %s; %s\n", valid ? "tracked" : "unavailable",
                 valid ? "eye-tracked maps selected" : "full-detail fallback selected");
    p.tracking_reported = true;
  }
  p.tracking = valid;
  if(valid) {p.centers[0]=eye.foveationCenter[0];p.centers[1]=eye.foveationCenter[1];}
  return true;
}
void XrFoveationMaps::EndFrame() {
  auto& p = *impl_;
  if (!p.acquired) return;
  auto release = Info<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
  (void)xrReleaseSwapchainImage(p.swapchain, &release);
  p.acquired = false;
}
void XrFoveationMaps::Destroy() {
  if (!impl_) return;
  auto& p = *impl_;
  EndFrame();
  if (p.destroy_view && p.device)
    for (auto& pair : p.views)
      for (auto view : pair) if (view) p.destroy_view(p.device, view, nullptr);
  if (p.swapchain) (void)xrDestroySwapchain(p.swapchain);
  if (p.profile && p.destroy_profile) (void)p.destroy_profile(p.profile);
  p = Impl{};
}
bool XrFoveationMaps::ready() const { return impl_->ready; }
bool XrFoveationMaps::acquired() const { return impl_->acquired; }
bool XrFoveationMaps::tracking() const { return impl_->acquired && impl_->tracking; }
VkImageView XrFoveationMaps::view(int eye) const {
  const auto& p = *impl_;
  return tracking() && eye >= 0 && eye < 2 && p.index < p.views.size() ? p.views[p.index][eye] : VK_NULL_HANDLE;
}
std::uint32_t XrFoveationMaps::width() const { return impl_->width; }
std::uint32_t XrFoveationMaps::height() const { return impl_->height; }
bool XrFoveationMaps::center(int eye,float* x,float* y) const {
  if(!tracking() || eye<0 || eye>1 || !x || !y)return false;
  *x=impl_->centers[eye].x;*y=impl_->centers[eye].y;
  return true;
}
}  // namespace mocktail::vr
