#include "mocktail/vr/render_core_preference.h"
#include "mocktail/vr/foveation_focus.h"
#include <dlfcn.h>
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <openxr/openxr.h>

#include <algorithm>
#include <set>
#include <string>
#include "mocktail/vr/openxr_backend.h"
#include "mocktail/vr/roblox_vr_device_bridge.h"

namespace mocktail::vr {
namespace internal {
struct VrBridgeTestAccess {
  static void Enable(RobloxVrDeviceBridge& bridge) { bridge.active_ = true; }
};
}
struct OpenXrBackendTestAccess {
  static std::string DeviceKey(const VkDeviceCreateInfo* info) {
    return OpenXrBackend::VulkanDeviceKey(info);
  }
  static size_t DesktopCount(const OpenXrBackend& b) { return b.desktop_swapchains_.size(); }
  template<class T> static T Handle(std::uintptr_t n) { return reinterpret_cast<T>(n); }
  static void Configure(OpenXrBackend& backend) {
    backend.vk_instance_ = Handle<VkInstance>(1);
    backend.vk_device_ = Handle<VkDevice>(2);
    backend.recording_ = true;
    backend.published_pose_.frame = 7;
    backend.published_pose_.valid = true;
  }
  static VkInstance Instance(const OpenXrBackend& b) { return b.vk_instance_; }
  static void GraphicsFamily(OpenXrBackend& b, unsigned family) {
    b.vk_queue_family_ = family;
    b.vk_graphics_queue_ = Handle<VkQueue>(30);
    b.device_queue_families_ = {family, family + 1};
  }
  static VkQueue CopyQueue(const OpenXrBackend& b, VkQueue present) {
    return b.GraphicsQueueForPresentLocked(present);
  }
  static VkQueue FrameQueue(const OpenXrBackend& b, VkQueue present) {
    return b.FrameQueueForPresentLocked(present);
  }
  static void NextFrame(OpenXrBackend& b) { ++b.published_pose_.frame; }
  static bool SupportsQueue(const OpenXrBackend& b, VkQueue queue) {
    return b.SupportsPresentQueueLocked(queue);
  }
  static void Attachment(OpenXrBackend& b, int id, void* owner, VkDevice device) {
    auto image = Handle<VkImage>(id);
    auto& r = b.images_[image];
    r.image = image; r.device = device; r.owner = owner;
    r.width = r.height = 500; r.format = VK_FORMAT_R8G8B8A8_UNORM;
    r.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    b.image_views_[Handle<VkImageView>(id)] = image;
    b.framebuffer_views_[Handle<VkFramebuffer>(id)] = {Handle<VkImageView>(id)};
  }
  static void Pass(
      OpenXrBackend& b, RobloxVrDeviceBridge& bridge, int eye, int id,
      void* owner,
      VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VkCommandBuffer command = VK_NULL_HANDLE) {
    bridge.OnEyeGetterCall(owner, eye, Handle<void*>(id));
    VkRenderPassBeginInfo info{};
    info.framebuffer = Handle<VkFramebuffer>(id);
    info.renderPass = Handle<VkRenderPass>(99);
    b.render_pass_final_layouts_[info.renderPass] = {layout};
    b.NoteRenderPassBegin(command, &info);
  }
  static VkImage Image(const OpenXrBackend& b, int eye) { return b.eyes_[eye].image; }
  static int SceneEye(const OpenXrBackend& b, VkDevice device, unsigned width=500, unsigned height=500) {
    return b.FoveationSceneEyeLocked(device,width,height);
  }
  static void AddMsaaAttachment(OpenXrBackend& b, int framebuffer, int msaa) {
    b.images_[Handle<VkImage>(msaa)].samples = VK_SAMPLE_COUNT_4_BIT;
    b.framebuffer_views_[Handle<VkFramebuffer>(framebuffer)].insert(
        b.framebuffer_views_[Handle<VkFramebuffer>(framebuffer)].begin(),
        Handle<VkImageView>(msaa));
  }
  static void ResolvedPass(OpenXrBackend& b, RobloxVrDeviceBridge& bridge,
                           void* owner, int framebuffer) {
    bridge.OnEyeGetterCall(owner, 0, Handle<void*>(framebuffer));
    VkRenderPassBeginInfo info{};
    info.framebuffer = Handle<VkFramebuffer>(framebuffer);
    info.renderPass = Handle<VkRenderPass>(99);
    b.render_pass_final_layouts_[info.renderPass] = {
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    b.NoteRenderPassBegin(VK_NULL_HANDLE, &info);
  }
  static VkImageLayout Layout(const OpenXrBackend& b, int eye) { return b.eyes_[eye].final_layout; }
  static void Invalidate(OpenXrBackend& b) {
    b.InvalidateInFlightPose("test");
  }
  static void SetFrameOpen(OpenXrBackend& b, bool open) { b.frame_open_ = open; }
  static void SetVisibilityLost(OpenXrBackend& b, bool lost) { b.visibility_lost_ = lost; }
  static bool VisibilityLost(const OpenXrBackend& b) { return b.visibility_lost_; }
  static void FrameState(OpenXrBackend& b,bool running,bool should_render) {
    b.session_running_=running;b.frame_should_render_=should_render;
  }
  static bool IdlePacing(const OpenXrBackend& b) { return b.NeedsIdlePacingLocked(); }

  static void NotePose(OpenXrBackend& b, void* owner) {
    b.NotePoseApplied(owner, b.published_pose_.frame);
  }
  static std::size_t AppliedPoses(const OpenXrBackend& b) {
    return b.applied_poses_.size();
  }
  static void State(OpenXrBackend& b, XrSessionState state) { b.HandleSessionState(state); }
  static void ReferenceChange(OpenXrBackend& b, std::int64_t time) { b.HandleReferenceSpaceChange(time); }
  static void FrameAt(OpenXrBackend& b, std::uint64_t time) {
    b.frame_open_ = true; b.frame_views_valid_ = true; b.frame_display_time_ = time;
    b.published_pose_.valid = true;
  }
  static bool Recovering(OpenXrBackend& b) { return b.session_recovery_pending_; }
  static VkDevice Device(OpenXrBackend& b) { return b.vk_device_; }
  static std::size_t Resources(OpenXrBackend& b) { return b.images_.size(); }
  static void SeedResource(OpenXrBackend& b) { b.images_[Handle<VkImage>(99)] = {}; }
  static void LatchProjectionRejection(OpenXrBackend& b) {
    b.projection_rejection_logged_ = true;
  }
  static bool ProjectionRejectionLogged(const OpenXrBackend& b) {
    return b.projection_rejection_logged_;
  }
};
using Access = OpenXrBackendTestAccess;
namespace {
struct DeviceRequest {
  float priority = 1.f;
  VkDeviceQueueCreateInfo queue{};
  VkDeviceCreateInfo info{};
  DeviceRequest() {
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
  }
};
}
TEST(OpenXrBackendDeviceContract, FeaturePointersAndExtensionOrderDoNotAffectCompatibility) {
  DeviceRequest a, b;
  VkPhysicalDeviceVulkan12Features fa{}, fb{};
  fa.sType = fb.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  fa.timelineSemaphore = fb.timelineSemaphore = VK_TRUE;
  a.info.pNext = &fa;
  b.info.pNext = &fb;
  const char* ea[] = {"VK_KHR_swapchain", "VK_KHR_timeline_semaphore"};
  const char* eb[] = {ea[1], ea[0]};
  a.info.enabledExtensionCount = b.info.enabledExtensionCount = 2;
  a.info.ppEnabledExtensionNames = ea;
  b.info.ppEnabledExtensionNames = eb;
  ASSERT_FALSE(Access::DeviceKey(&a.info).empty());
  EXPECT_EQ(Access::DeviceKey(&a.info), Access::DeviceKey(&b.info));
  fb.subgroupBroadcastDynamicId = VK_TRUE;
  EXPECT_NE(Access::DeviceKey(&a.info), Access::DeviceKey(&b.info));
  VkPhysicalDeviceSamplerYcbcrConversionFeatures ya{}, yb{};
  ya.sType = yb.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
  ya.samplerYcbcrConversion = yb.samplerYcbcrConversion = VK_TRUE;
  fa.pNext = &ya;
  yb.pNext = &fb;
  b.info.pNext = &yb;
  fb.subgroupBroadcastDynamicId = VK_FALSE;
  ASSERT_FALSE(Access::DeviceKey(&a.info).empty());
  EXPECT_EQ(Access::DeviceKey(&a.info), Access::DeviceKey(&b.info));
  yb.samplerYcbcrConversion = VK_FALSE;
  EXPECT_NE(Access::DeviceKey(&a.info), Access::DeviceKey(&b.info));
}
TEST(OpenXrBackendDeviceContract, CoreFeaturesAndQueuesMustMatch) {
  DeviceRequest a, b;
  EXPECT_EQ(Access::DeviceKey(&a.info), Access::DeviceKey(&b.info));
  VkPhysicalDeviceFeatures2 features{};
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  b.info.pNext = &features;
  const auto before = Access::DeviceKey(&b.info);
  features.features.inheritedQueries = VK_TRUE;
  EXPECT_NE(before, Access::DeviceKey(&b.info));
  b.info.pNext = nullptr;
  b.queue.queueFamilyIndex = 1;
  EXPECT_NE(Access::DeviceKey(&a.info), Access::DeviceKey(&b.info));
  b.queue.queueFamilyIndex = 0;
  b.priority = .5f;
  EXPECT_NE(Access::DeviceKey(&a.info), Access::DeviceKey(&b.info));
}
TEST(OpenXrBackendDeviceContract, UnsupportedAndCyclicChainsFailClosed) {
  DeviceRequest a;
  VkBaseInStructure unknown{};
  unknown.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  a.info.pNext = &unknown;
  EXPECT_TRUE(Access::DeviceKey(&a.info).empty());
  VkPhysicalDeviceVulkan12Features features{};
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features.pNext = &features;
  a.info.pNext = &features;
  EXPECT_TRUE(Access::DeviceKey(&a.info).empty());
  a.info.pNext = nullptr;
  a.queue.pNext = &unknown;
  EXPECT_TRUE(Access::DeviceKey(&a.info).empty());
}
TEST(OpenXrBackendLifecycle, DeviceDestructionPreservesLiveInstance) {
  OpenXrBackend b;
  Access::Configure(b);
  b.NoteDeviceDestroyed(Access::Handle<VkDevice>(3));
  EXPECT_TRUE(b.WantsResourceRecords());
  b.NoteDeviceDestroyed(Access::Handle<VkDevice>(2));
  EXPECT_EQ(Access::Instance(b), Access::Handle<VkInstance>(1));
  EXPECT_FALSE(b.WantsResourceRecords());
  EXPECT_FALSE(b.PublishedHeadPose().valid);
  b.NoteInstanceDestroyed(Access::Handle<VkInstance>(1));
  EXPECT_EQ(Access::Instance(b), VK_NULL_HANDLE);
}
TEST(OpenXrBackendBinding, SwitchesOwnerWithoutWaitingForOldResourcesToDie) {
  OpenXrBackend b;
  RobloxVrDeviceBridge bridge;
  internal::VrBridgeTestAccess::Enable(bridge);
  Access::Configure(b);
  auto* first = Access::Handle<void*>(100);
  auto* second = Access::Handle<void*>(200);
  for (int id = 10; id < 14; ++id)
    Access::Attachment(b, id, id < 12 ? first : second, Access::Handle<VkDevice>(2));
  Access::Pass(b, bridge, 0, 10, first);
  Access::Pass(b, bridge, 1, 11, first);
  ASSERT_TRUE(b.EyeBindingComplete());
  Access::Pass(b, bridge, 0, 12, second);
  EXPECT_FALSE(b.EyeBindingComplete());
  Access::Pass(b, bridge, 1, 13, second);
  EXPECT_TRUE(b.EyeBindingComplete());
  EXPECT_EQ(Access::Image(b, 0), Access::Handle<VkImage>(12));
  EXPECT_EQ(Access::Image(b, 1), Access::Handle<VkImage>(13));
  Access::Pass(b, bridge, 0, 12, second, VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(Access::Layout(b, 0), VK_IMAGE_LAYOUT_GENERAL);
}
TEST(OpenXrBackendBinding, RejectsAnotherDeviceImages) {
  OpenXrBackend b;
  RobloxVrDeviceBridge bridge;
  internal::VrBridgeTestAccess::Enable(bridge);
  Access::Configure(b);
  auto* owner = Access::Handle<void*>(100);
  Access::Attachment(b, 10, owner, Access::Handle<VkDevice>(3));
  Access::Pass(b, bridge, 0, 10, owner);
  EXPECT_EQ(Access::Image(b, 0), VK_NULL_HANDLE);
  EXPECT_FALSE(b.EyeBindingComplete());
}

class OpenXrSceneFoveation : public testing::Test {
 protected:
  OpenXrBackend backend;
  RobloxVrDeviceBridge bridge;
  VkDevice device=Access::Handle<VkDevice>(2);
  void* owner=Access::Handle<void*>(100);
  void SetUp() override {
    internal::VrBridgeTestAccess::Enable(bridge);
    Access::Configure(backend);
    Access::Attachment(backend,10,owner,device);
    Access::Attachment(backend,11,owner,device);
    Access::Pass(backend,bridge,0,10,owner);
    Access::Pass(backend,bridge,1,11,owner);
  }
};
TEST_F(OpenXrSceneFoveation, SharedIntermediateUsesTheLiveEyeNotItsHandle) {
  EXPECT_EQ(Access::SceneEye(backend,device),1);
  bridge.OnEyeGetterCall(owner,0,Access::Handle<void*>(10));
  EXPECT_EQ(Access::SceneEye(backend,device),0);
  Access::NextFrame(backend);
  EXPECT_EQ(Access::SceneEye(backend,device),0);
}
TEST_F(OpenXrSceneFoveation, WrongOwnerAndStaleEyeGetterFailClosed) {
  bridge.OnEyeGetterCall(Access::Handle<void*>(200),0,Access::Handle<void*>(10));
  EXPECT_EQ(Access::SceneEye(backend,device),-1);
  bridge.OnEyeGetterCall(owner,0,Access::Handle<void*>(11));
  EXPECT_EQ(Access::SceneEye(backend,device),-1);
  bridge.OnEyeGetterCall(owner,0,nullptr);
  EXPECT_EQ(Access::SceneEye(backend,device),-1);
}
TEST_F(OpenXrSceneFoveation, SmallerEffectsAndOtherDevicesAreNotEyeScenes) {
  EXPECT_EQ(Access::SceneEye(backend,device,250,500),-1);
  EXPECT_EQ(Access::SceneEye(backend,device,500,250),-1);
  EXPECT_EQ(Access::SceneEye(backend,device,0,0),-1);
  EXPECT_EQ(Access::SceneEye(backend,Access::Handle<VkDevice>(3)),-1);
}
TEST_F(OpenXrSceneFoveation, StaleFramesAndDestroyedEyeBindingsFailClosed) {
  Access::NextFrame(backend);Access::NextFrame(backend);
  EXPECT_EQ(Access::SceneEye(backend,device),-1);
  Access::Pass(backend,bridge,1,11,owner);
  EXPECT_EQ(Access::SceneEye(backend,device),1);
  backend.NoteDestroyFramebuffer(Access::Handle<VkFramebuffer>(11));
  EXPECT_EQ(Access::SceneEye(backend,device),-1);
}
TEST_F(OpenXrSceneFoveation, RendererReplacementRequiresBothNewEyes) {
  auto* next=Access::Handle<void*>(200);
  Access::Attachment(backend,12,next,device);Access::Attachment(backend,13,next,device);
  Access::Pass(backend,bridge,0,12,next);
  EXPECT_EQ(Access::SceneEye(backend,device),-1);
  Access::Pass(backend,bridge,1,13,next);
  EXPECT_EQ(Access::SceneEye(backend,device),1);
  bridge.OnEyeGetterCall(owner,0,Access::Handle<void*>(10));
  EXPECT_EQ(Access::SceneEye(backend,device),-1);
}

TEST(OpenXrBackendLifecycle, FutureRecenterPreservesEarlierFrameAndLateChangeDropsIt) {
  OpenXrBackend b;
  Access::Configure(b);
  Access::FrameAt(b, 100);
  Access::ReferenceChange(b, 200);
  EXPECT_TRUE(b.PublishedHeadPose().valid);
  Access::ReferenceChange(b, 100);
  EXPECT_FALSE(b.PublishedHeadPose().valid);
  Access::FrameAt(b, 300);
  Access::ReferenceChange(b, 200);
  EXPECT_FALSE(b.PublishedHeadPose().valid);
}

TEST(OpenXrBackendLifecycle, ActiveFramesAreNeverIdlePaced) {
  OpenXrBackend b;
  OpenXrBackendTestAccess::FrameState(b,true,true);
  OpenXrBackendTestAccess::State(b,XR_SESSION_STATE_VISIBLE);
  EXPECT_FALSE(OpenXrBackendTestAccess::IdlePacing(b));
  OpenXrBackendTestAccess::State(b,XR_SESSION_STATE_FOCUSED);
  EXPECT_FALSE(OpenXrBackendTestAccess::IdlePacing(b));
}
TEST(OpenXrBackendLifecycle, VisibleRuntimeRenderSuppressionDoesNotImposeAnIdleCap) {
  OpenXrBackend b;
  OpenXrBackendTestAccess::FrameState(b,true,false);
  OpenXrBackendTestAccess::State(b,XR_SESSION_STATE_VISIBLE);
  EXPECT_FALSE(OpenXrBackendTestAccess::IdlePacing(b));
  OpenXrBackendTestAccess::FrameState(b,true,true);
  EXPECT_FALSE(OpenXrBackendTestAccess::IdlePacing(b));
  OpenXrBackendTestAccess::FrameState(b,false,true);
  EXPECT_TRUE(OpenXrBackendTestAccess::IdlePacing(b));
}
TEST(OpenXrBackendLifecycle, InvisibleSessionIsIdlePacedWithoutCappingVisibleFrames) {
  OpenXrBackend b;
  OpenXrBackendTestAccess::FrameState(b,true,true);
  OpenXrBackendTestAccess::State(b,XR_SESSION_STATE_SYNCHRONIZED);
  EXPECT_TRUE(OpenXrBackendTestAccess::IdlePacing(b));
  OpenXrBackendTestAccess::State(b,XR_SESSION_STATE_VISIBLE);
  EXPECT_FALSE(OpenXrBackendTestAccess::IdlePacing(b));
}
TEST(OpenXrBackendLifecycle, SynchronizedVisibilityTransitionUsesActualStateHandler) {
  OpenXrBackend b;
  Access::Configure(b);
  Access::State(b, XR_SESSION_STATE_VISIBLE);
  Access::State(b, XR_SESSION_STATE_SYNCHRONIZED);
  EXPECT_TRUE(Access::VisibilityLost(b));
  EXPECT_FALSE(b.PublishedHeadPose().valid);
  Access::FrameAt(b, 100);
  Access::State(b, XR_SESSION_STATE_VISIBLE);
  EXPECT_FALSE(Access::VisibilityLost(b));
  EXPECT_FALSE(b.PublishedHeadPose().valid);
}

TEST(OpenXrBackendLifecycle, SessionLossKeepsLiveDeviceProvenanceButDeviceDestructionCancelsRecovery) {
  OpenXrBackend b;
  Access::Configure(b);
  Access::SeedResource(b);
  Access::State(b, XR_SESSION_STATE_LOSS_PENDING);
  EXPECT_TRUE(Access::Recovering(b));
  EXPECT_EQ(Access::Device(b), Access::Handle<VkDevice>(2));
  EXPECT_EQ(Access::Resources(b), 1u);
  EXPECT_TRUE(b.WantsResourceRecords());
  EXPECT_FALSE(b.PublishedHeadPose().valid);
  b.NoteDestroyImage(Access::Handle<VkImage>(99));
  EXPECT_EQ(Access::Resources(b), 0u);
  b.NoteDeviceDestroyed(Access::Handle<VkDevice>(2));
  EXPECT_FALSE(Access::Recovering(b));
  EXPECT_EQ(Access::Device(b), VK_NULL_HANDLE);
}

TEST(OpenXrBackendLifecycle, InvalidationDropsPoseAndAppliedRecord) {
  OpenXrBackend b;
  Access::Configure(b);
  Access::SetFrameOpen(b, true);
  auto* owner = Access::Handle<void*>(100);
  Access::NotePose(b, owner);
  ASSERT_EQ(Access::AppliedPoses(b), 1u);
  ASSERT_TRUE(b.PublishedHeadPose().valid);
  // A reference-space change / visibility transition must drop the pose and
  // the per-owner application record so the frame cycle cannot retag the
  // in-flight images with a pose from the previous origin.
  Access::Invalidate(b);
  EXPECT_FALSE(b.PublishedHeadPose().valid);
  EXPECT_EQ(Access::AppliedPoses(b), 0u);
}

TEST(OpenXrBackendLifecycle, VisibilityLostStateSurvivesUntilSessionReset) {
  OpenXrBackend b;
  Access::Configure(b);
  Access::SetVisibilityLost(b, true);
  EXPECT_TRUE(Access::VisibilityLost(b));
  // Teardown (device destroyed) resets the flag so a recreated session starts
  // visible; it must not inherit IDLE state from the previous session.
  b.NoteDeviceDestroyed(Access::Handle<VkDevice>(2));
  EXPECT_FALSE(Access::VisibilityLost(b));
}

TEST(OpenXrBackendLifecycle, TeardownClearsProjectionRejectionLatch) {
  OpenXrBackend b;
  Access::Configure(b);
  Access::LatchProjectionRejection(b);
  ASSERT_TRUE(Access::ProjectionRejectionLogged(b));
  b.NoteDeviceDestroyed(Access::Handle<VkDevice>(2));
  EXPECT_FALSE(Access::ProjectionRejectionLogged(b));
}
}  // namespace mocktail::vr

namespace mocktail::vr {
TEST(OpenXrDesktopMirror, RequiresTransferUsageAndDropsDestroyedSwapchain) {
  OpenXrBackend b;
  OpenXrBackendTestAccess::Configure(b);
  const auto device = OpenXrBackendTestAccess::Handle<VkDevice>(2);
  const auto chain = OpenXrBackendTestAccess::Handle<VkSwapchainKHR>(20);
  const auto image = OpenXrBackendTestAccess::Handle<VkImage>(21);
  VkSwapchainCreateInfoKHR info{};
  info.imageArrayLayers = 1;
  info.imageExtent = {800, 600};
  info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
  b.RecordDesktopSwapchain(device, chain, &info, &image, 1);
  EXPECT_EQ(OpenXrBackendTestAccess::DesktopCount(b), 0u);
  info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  b.RecordDesktopSwapchain(device, chain, &info, &image, 1);
  EXPECT_EQ(OpenXrBackendTestAccess::DesktopCount(b), 1u);
  b.RecordDesktopSwapchain(device, chain, nullptr, nullptr, 0);
  EXPECT_EQ(OpenXrBackendTestAccess::DesktopCount(b), 0u);
  b.RecordDesktopSwapchain(OpenXrBackendTestAccess::Handle<VkDevice>(3), chain, &info, &image, 1);
  EXPECT_EQ(OpenXrBackendTestAccess::DesktopCount(b), 0u);
  VkSemaphore ready = VK_NULL_HANDLE;
  EXPECT_EQ(b.MirrorDesktop(VK_NULL_HANDLE, nullptr, &ready), VK_NOT_READY);
  EXPECT_EQ(ready, VK_NULL_HANDLE);
}
}

namespace mocktail::vr {
namespace {
VkResult reset_result, submit_result, wait_result;
int resets, submits, waits;
VkQueue expected_queue;
VkSemaphore expected_semaphores[2];
VKAPI_ATTR VkResult VKAPI_CALL ResetPresentFence(VkDevice, uint32_t count,
                                                 const VkFence*) {
  EXPECT_EQ(count, 1u);
  ++resets;
  return reset_result;
}
VKAPI_ATTR VkResult VKAPI_CALL SubmitPresentWait(VkQueue queue, uint32_t count,
                                                 const VkSubmitInfo* info,
                                                 VkFence fence) {
  ++submits;
  EXPECT_EQ(queue, expected_queue);
  EXPECT_EQ(count, 1u);
  EXPECT_NE(fence, VK_NULL_HANDLE);
  EXPECT_EQ(info->commandBufferCount, 0u);
  EXPECT_EQ(info->waitSemaphoreCount, 2u);
  EXPECT_EQ(info->signalSemaphoreCount, 0u);
  for (unsigned i = 0; i < 2; ++i) {
    EXPECT_EQ(info->pWaitSemaphores[i], expected_semaphores[i]);
    EXPECT_EQ(info->pWaitDstStageMask[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  }
  return submit_result;
}
VKAPI_ATTR VkResult VKAPI_CALL WaitPresentFence(VkDevice, uint32_t count,
                                                const VkFence*, VkBool32 all,
                                                uint64_t timeout) {
  ++waits;
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(all, VK_TRUE);
  EXPECT_EQ(timeout, UINT64_MAX);
  return wait_result;
}
class OpenXrPresentDependencies : public testing::Test {
 protected:
  VkPresentInfoKHR info{};
  void SetUp() override {
    reset_result = submit_result = wait_result = VK_SUCCESS;
    resets = submits = waits = 0;
    expected_queue = Access::Handle<VkQueue>(30);
    expected_semaphores[0] = Access::Handle<VkSemaphore>(31);
    expected_semaphores[1] = Access::Handle<VkSemaphore>(32);
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 2;
    info.pWaitSemaphores = expected_semaphores;
  }
  VkResult Run() {
    return internal::WaitForPresentDependencies(
        Access::Handle<VkDevice>(2), expected_queue, info,
        Access::Handle<VkFence>(33), ResetPresentFence, SubmitPresentWait,
        WaitPresentFence);
  }
};
TEST_F(OpenXrPresentDependencies, ConsumesDependenciesBeforePermittingCopies) {
  EXPECT_EQ(Run(), VK_SUCCESS);
  EXPECT_EQ(resets, 1);
  EXPECT_EQ(submits, 1);
  EXPECT_EQ(waits, 1);
  EXPECT_EQ(info.waitSemaphoreCount, 2u);
}
TEST_F(OpenXrPresentDependencies, MirrorConsumedWaitsNeedNoSecondSubmission) {
  info.waitSemaphoreCount = 0;
  info.pWaitSemaphores = nullptr;
  EXPECT_EQ(Run(), VK_SUCCESS);
  EXPECT_EQ(resets + submits + waits, 0);
}
TEST_F(OpenXrPresentDependencies, SubmitFailureDoesNotWaitForUnsignalledFence) {
  submit_result = VK_ERROR_DEVICE_LOST;
  EXPECT_EQ(Run(), VK_ERROR_DEVICE_LOST);
  EXPECT_EQ(submits, 1);
  EXPECT_EQ(waits, 0);
}
TEST_F(OpenXrPresentDependencies, FailedSubmitLeavesThePresentToTheGuest) {
  submit_result = VK_ERROR_OUT_OF_HOST_MEMORY;
  EXPECT_EQ(Run(), VK_NOT_READY);
  EXPECT_EQ(submits, 1);
  EXPECT_EQ(waits, 0);
}
TEST_F(OpenXrPresentDependencies,
       ConsumedWaitsNeverAuthorizeFallbackOnTimeout) {
  wait_result = VK_TIMEOUT;
  EXPECT_LT(Run(), VK_SUCCESS);
  EXPECT_EQ(submits, 1);
  EXPECT_EQ(waits, 1);
}
TEST_F(OpenXrPresentDependencies, InvalidDependenciesDoNotSubmit) {
  info.pWaitSemaphores = nullptr;
  EXPECT_LT(Run(), VK_SUCCESS);
  EXPECT_EQ(resets + submits + waits, 0);
}

VkSemaphore chained_signal;
VKAPI_ATTR VkResult VKAPI_CALL SubmitChainedPresent(VkQueue queue,
                                                    uint32_t count,
                                                    const VkSubmitInfo* info,
                                                    VkFence fence) {
  ++submits;
  EXPECT_EQ(queue, expected_queue);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(fence, VK_NULL_HANDLE);
  EXPECT_EQ(info->commandBufferCount, 0u);
  EXPECT_EQ(info->waitSemaphoreCount, 2u);
  for (unsigned i = 0; i < 2; ++i) {
    EXPECT_EQ(info->pWaitSemaphores[i], expected_semaphores[i]);
    EXPECT_EQ(info->pWaitDstStageMask[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  }
  EXPECT_EQ(info->signalSemaphoreCount, 1u);
  EXPECT_EQ(info->pSignalSemaphores[0], chained_signal);
  return submit_result;
}
class OpenXrChainedPresent : public OpenXrPresentDependencies {
 protected:
  VkResult Chain() {
    chained_signal = Access::Handle<VkSemaphore>(34);
    return internal::ChainPresentDependencies(Access::Handle<VkDevice>(2),
                                              expected_queue, info,
                                              chained_signal,
                                              SubmitChainedPresent);
  }
};
TEST_F(OpenXrChainedPresent, HandsDependenciesToTheGpuWithoutWaiting) {
  EXPECT_EQ(Chain(), VK_SUCCESS);
  EXPECT_EQ(submits, 1);
  EXPECT_EQ(resets + waits, 0);
}
TEST_F(OpenXrChainedPresent, FailedSubmitLeavesThePresentToTheGuest) {
  submit_result = VK_ERROR_OUT_OF_HOST_MEMORY;
  EXPECT_EQ(Chain(), VK_NOT_READY);
  submit_result = VK_ERROR_DEVICE_LOST;
  EXPECT_EQ(Chain(), VK_ERROR_DEVICE_LOST);
}
TEST_F(OpenXrChainedPresent, NothingToChainDoesNotSubmit) {
  info.waitSemaphoreCount = 0;
  EXPECT_EQ(Chain(), VK_NOT_READY);
  info.waitSemaphoreCount = 2;
  info.pWaitSemaphores = nullptr;
  EXPECT_EQ(Chain(), VK_NOT_READY);
  EXPECT_EQ(submits, 0);
}
TEST(OpenXrPresentQueue, RoutesSameAndSplitFamilyPresentationToGraphicsQueue) {
  OpenXrBackend backend;
  Access::Configure(backend);
  Access::GraphicsFamily(backend, 3);
  const auto device = Access::Handle<VkDevice>(2);
  const auto graphics = Access::Handle<VkQueue>(30);
  const auto same_family = Access::Handle<VkQueue>(31);
  const auto split_family = Access::Handle<VkQueue>(32);
  backend.NoteQueue(device, graphics, 3, 0);
  backend.NoteQueue(device, same_family, 3, 1);
  backend.NoteQueue(device, split_family, 4, 0);
  EXPECT_TRUE(Access::SupportsQueue(backend, graphics));
  EXPECT_TRUE(Access::SupportsQueue(backend, same_family));
  EXPECT_TRUE(Access::SupportsQueue(backend, split_family));
  EXPECT_EQ(Access::CopyQueue(backend, split_family), graphics);
  EXPECT_FALSE(Access::SupportsQueue(backend, Access::Handle<VkQueue>(99)));
}
}  // namespace
}  // namespace mocktail::vr

namespace mocktail::vr {
TEST(OpenXrBackendBinding,
     SelectsSingleSampleResolveAlongsideMultisampleColor) {
  OpenXrBackend backend;
  RobloxVrDeviceBridge bridge;
  internal::VrBridgeTestAccess::Enable(bridge);
  Access::Configure(backend);
  auto* owner = Access::Handle<void*>(100);
  const auto device = Access::Handle<VkDevice>(2);
  Access::Attachment(backend, 10, owner, device);
  Access::Attachment(backend, 11, owner, device);
  Access::AddMsaaAttachment(backend, 10, 11);
  Access::ResolvedPass(backend, bridge, owner, 10);
  EXPECT_EQ(Access::Image(backend, 0), Access::Handle<VkImage>(10));
  EXPECT_EQ(Access::Layout(backend, 0),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
TEST(OpenXrBackendBinding, RejectsMultisampleImageWithoutResolve) {
  OpenXrBackend backend;
  RobloxVrDeviceBridge bridge;
  internal::VrBridgeTestAccess::Enable(bridge);
  Access::Configure(backend);
  auto* owner = Access::Handle<void*>(100);
  Access::Attachment(backend, 10, owner, Access::Handle<VkDevice>(2));
  Access::AddMsaaAttachment(backend, 10, 10);
  Access::ResolvedPass(backend, bridge, owner, 10);
  EXPECT_EQ(Access::Image(backend, 0), VK_NULL_HANDLE);
}
}  // namespace mocktail::vr

namespace mocktail::vr {
namespace {
class OpenXrSplitPresentation : public testing::Test {
 protected:
  OpenXrBackend backend;
  RobloxVrDeviceBridge bridge;
  const VkDevice device = Access::Handle<VkDevice>(2);
  const VkQueue graphics = Access::Handle<VkQueue>(30);
  const VkQueue present = Access::Handle<VkQueue>(31);
  const VkQueue graphics_second = Access::Handle<VkQueue>(32);
  const VkCommandPool graphics_pool = Access::Handle<VkCommandPool>(40);
  const VkCommandPool present_pool = Access::Handle<VkCommandPool>(41);
  const VkCommandBuffer draw = Access::Handle<VkCommandBuffer>(50);
  const VkCommandBuffer acquire = Access::Handle<VkCommandBuffer>(51);
  const VkSwapchainKHR chain = Access::Handle<VkSwapchainKHR>(60);
  const VkImage desktop = Access::Handle<VkImage>(61);
  void* const owner = Access::Handle<void*>(100);
  void SetUp() override {
    Access::Configure(backend);
    Access::GraphicsFamily(backend, 3);
    internal::VrBridgeTestAccess::Enable(bridge);
    backend.NoteQueue(device, graphics, 3, 0);
    backend.NoteQueue(device, present, 4, 0);
    backend.NoteQueue(device, graphics_second, 3, 1);
    backend.RecordCommandPool(device, graphics_pool, 3);
    backend.RecordCommandPool(device, present_pool, 4);
    backend.RecordCommandBuffers(device, graphics_pool, 1, &draw);
    backend.RecordCommandBuffers(device, present_pool, 1, &acquire);
    RegisterDesktop(true);
  }
  void RegisterDesktop(bool converted) {
    const unsigned families[] = {3, 4};
    VkSwapchainCreateInfoKHR info{};
    info.imageArrayLayers = 1;
    info.imageExtent = {800, 600};
    info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    info.queueFamilyIndexCount = 2;
    info.pQueueFamilyIndices = families;
    backend.RecordDesktopSwapchain(device, chain, &info, &desktop, 1,
                                   converted);
  }
  void Draw() {
    Access::Attachment(backend, 10, owner, device);
    Access::Pass(backend, bridge, 0, 10, owner,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, draw);
  }
};
TEST_F(OpenXrSplitPresentation, ActualSubmittedGraphicsQueueOwnsTheEyeCopy) {
  Draw();
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
  backend.NoteCommandBuffersSubmitted(present, 1, &draw);  // Wrong pool family.
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
  backend.NoteCommandBuffersSubmitted(graphics_second, 1, &draw);
  EXPECT_EQ(Access::CopyQueue(backend, present), graphics_second);
  Access::NextFrame(backend);
  backend.ResetCommandBuffer(draw);
  Draw();
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
  backend.NoteCommandBuffersSubmitted(graphics, 1, &draw);
  EXPECT_EQ(Access::CopyQueue(backend, present), graphics);
}
TEST_F(OpenXrSplitPresentation, MissingEyeSubmissionStillAllowsEmptyXrFrames) {
  Draw();
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
  EXPECT_EQ(Access::FrameQueue(backend, present), graphics);
  EXPECT_EQ(Access::FrameQueue(backend, Access::Handle<VkQueue>(999)), VK_NULL_HANDLE);
  backend.NoteCommandBuffersSubmitted(graphics_second, 1, &draw);
  EXPECT_EQ(Access::FrameQueue(backend, present), graphics_second);
  Access::NextFrame(backend);
  Draw();
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
  EXPECT_EQ(Access::FrameQueue(backend, present), graphics);
}
TEST_F(OpenXrSplitPresentation,
       ResetAndFreeDoNotResurrectUnsubmittedEyeCommands) {
  Draw();
  backend.ResetCommandBuffer(draw);
  backend.NoteCommandBuffersSubmitted(graphics, 1, &draw);
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
  Draw();
  backend.ForgetCommandPool(graphics_pool);
  backend.NoteCommandBuffersSubmitted(graphics, 1, &draw);
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
}
TEST_F(OpenXrSplitPresentation, ConcurrentSwapchainTracksBothFamilies) {
  EXPECT_EQ(Access::DesktopCount(backend), 1u);
  EXPECT_EQ(backend.DesktopQueueFamilies(device),
            (std::vector<unsigned>{3, 4}));
  EXPECT_TRUE(
      backend.DesktopQueueFamilies(Access::Handle<VkDevice>(9)).empty());
}
TEST_F(OpenXrSplitPresentation, PoolResetAndReusedImageRequireNewSubmission) {
  Draw();
  backend.NoteCommandBuffersSubmitted(graphics, 1, &draw);
  ASSERT_EQ(Access::CopyQueue(backend, present), graphics);
  backend.ResetCommandPool(graphics_pool);
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
  Draw();
  backend.NoteCommandBuffersSubmitted(graphics, 1, &draw);
  ASSERT_EQ(Access::CopyQueue(backend, present), graphics);
  backend.NoteDestroyImage(Access::Handle<VkImage>(10));
  Draw();
  EXPECT_EQ(Access::CopyQueue(backend, present), VK_NULL_HANDLE);
  backend.NoteCommandBuffersSubmitted(graphics_second, 1, &draw);
  EXPECT_EQ(Access::CopyQueue(backend, present), graphics_second);
}
TEST_F(OpenXrSplitPresentation,
       ReleaseTransitionsOnceAndMatchingAcquireDoesNotRepeat) {
  unsigned source = 3, destination = 4;
  VkImageLayout old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  backend.NormalizeDesktopBarrier(draw, desktop, &source, &destination,
                                  &old_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(source, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(destination, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(old_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  source = 3;
  destination = 4;
  backend.NormalizeDesktopBarrier(acquire, desktop, &source, &destination,
                                  &old_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(source, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(destination, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(old_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
TEST_F(OpenXrSplitPresentation, WsiAcquirePreservesPresentToColorTransition) {
  unsigned source = 4, destination = 3;
  VkImageLayout old_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  backend.NormalizeDesktopBarrier(draw, desktop, &source, &destination,
                                  &old_layout,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  EXPECT_EQ(source, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(destination, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(old_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  source = 4;
  destination = 3;
  old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  backend.NormalizeDesktopBarrier(draw, desktop, &source, &destination,
                                  &old_layout,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  EXPECT_EQ(old_layout, VK_IMAGE_LAYOUT_UNDEFINED);
}
TEST_F(OpenXrSplitPresentation,
       ExplicitPresentReleaseDefersLayoutTransitionToAcquire) {
  unsigned source = 4, destination = 3;
  VkImageLayout old_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  const auto released_layout = backend.NormalizeDesktopBarrier(
      acquire, desktop, &source, &destination, &old_layout,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  EXPECT_EQ(old_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(released_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(source, VK_QUEUE_FAMILY_IGNORED);
  source = 4;
  destination = 3;
  const auto acquired_layout = backend.NormalizeDesktopBarrier(
      draw, desktop, &source, &destination, &old_layout,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  EXPECT_EQ(old_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(acquired_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}
TEST_F(OpenXrSplitPresentation,
       OriginalConcurrentAndUnrelatedImagesRemainUnchanged) {
  RegisterDesktop(false);
  unsigned source = 3, destination = 4;
  VkImageLayout old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  backend.NormalizeDesktopBarrier(acquire, desktop, &source, &destination,
                                  &old_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(source, 3u);
  EXPECT_EQ(destination, 4u);
  RegisterDesktop(true);
  backend.NormalizeDesktopBarrier(acquire, Access::Handle<VkImage>(99), &source,
                                  &destination, &old_layout,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(source, 3u);
  backend.RecordDesktopSwapchain(device, chain, nullptr, nullptr, 0);
  backend.NormalizeDesktopBarrier(acquire, desktop, &source, &destination,
                                  &old_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(source, 3u);
}

TEST_F(OpenXrSplitPresentation, BatchedOwnershipOnlyChangesConvertedDesktopImage) {
  std::array<VkImageMemoryBarrier, 4> barriers{};
  for (auto& barrier : barriers) {
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = 3;
    barrier.dstQueueFamilyIndex = 4;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.image = desktop;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  }
  barriers[1].image = Access::Handle<VkImage>(99);
  barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
  barriers[3].srcQueueFamilyIndex = barriers[3].dstQueueFamilyIndex = 3;
  const auto original = barriers;
  backend.NormalizeDesktopBarriers(acquire, barriers.size(), barriers.data());
  EXPECT_EQ(barriers[0].srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(barriers[0].dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(barriers[0].oldLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  for (unsigned i = 1; i < barriers.size(); ++i)
    EXPECT_EQ(std::memcmp(&barriers[i], &original[i], sizeof(barriers[i])), 0);
}

TEST_F(OpenXrSplitPresentation, BatchedBarrier2PreservesStagesAccessAndChain) {
  VkMemoryBarrier2 chain{};
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.pNext = &chain;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
  barrier.srcQueueFamilyIndex = 4;
  barrier.dstQueueFamilyIndex = 3;
  barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.image = desktop;
  backend.NormalizeDesktopBarriers(acquire, 1, &barrier);
  EXPECT_EQ(barrier.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(barrier.dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
  EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
  EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_NONE);
  EXPECT_EQ(barrier.pNext, &chain);
}

TEST_F(OpenXrSplitPresentation, BatchDoesNotRewriteOriginalConcurrentOrRetiredImages) {
  VkImageMemoryBarrier barrier{};
  barrier.srcQueueFamilyIndex = 3;
  barrier.dstQueueFamilyIndex = 4;
  barrier.image = desktop;
  RegisterDesktop(false);
  backend.NormalizeDesktopBarriers(draw, 1, &barrier);
  EXPECT_EQ(barrier.srcQueueFamilyIndex, 3u);
  RegisterDesktop(true);
  backend.RecordDesktopSwapchain(device, chain, nullptr, nullptr, 0);
  backend.NormalizeDesktopBarriers(draw, 1, &barrier);
  EXPECT_EQ(barrier.srcQueueFamilyIndex, 3u);
}

TEST_F(OpenXrSplitPresentation, BatchTracksCommandBufferAndPoolLifetime) {
  VkImageMemoryBarrier barrier{};
  barrier.srcQueueFamilyIndex = 3;
  barrier.dstQueueFamilyIndex = 4;
  barrier.image = desktop;
  const auto original = barrier;
  backend.ForgetCommandBuffers(1, &draw);
  backend.NormalizeDesktopBarriers(draw, 1, &barrier);
  EXPECT_EQ(barrier.srcQueueFamilyIndex, 3u);
  backend.RecordCommandBuffers(device, graphics_pool, 1, &draw);
  backend.ResetCommandPool(graphics_pool);
  backend.NormalizeDesktopBarriers(draw, 1, &barrier);
  EXPECT_EQ(barrier.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
  barrier = original;
  backend.ForgetCommandPool(graphics_pool);
  backend.NormalizeDesktopBarriers(draw, 1, &barrier);
  EXPECT_EQ(barrier.srcQueueFamilyIndex, 3u);
}

}  // namespace
}  // namespace mocktail::vr

namespace mocktail::vr {
namespace {

TEST(OpenXrEyeSwapchainFormat, KeepsGuestEyeBytesWithoutGammaConversion) {
  using internal::PreferredEyeSwapchainFormat;
  const auto formats = [](std::initializer_list<VkFormat> list) {
    std::vector<std::int64_t> out;
    for (const VkFormat format : list) out.push_back(format);
    return out;
  };
  // The guest's own format wins.
  EXPECT_EQ(PreferredEyeSwapchainFormat(
                VK_FORMAT_R8G8B8A8_UNORM,
                formats({VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM})),
            VK_FORMAT_R8G8B8A8_UNORM);
  // SteamVR-style SRGB-only runtime: the same-order sibling is a raw copy.
  EXPECT_EQ(PreferredEyeSwapchainFormat(
                VK_FORMAT_R8G8B8A8_UNORM,
                formats({VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB})),
            VK_FORMAT_R8G8B8A8_SRGB);
  // Only the other channel order: keep the sRGB-ness so the blit only
  // swizzles.
  EXPECT_EQ(PreferredEyeSwapchainFormat(
                VK_FORMAT_R8G8B8A8_UNORM,
                formats({VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM})),
            VK_FORMAT_B8G8R8A8_UNORM);
  EXPECT_EQ(PreferredEyeSwapchainFormat(VK_FORMAT_B8G8R8A8_SRGB,
                                        formats({VK_FORMAT_R8G8B8A8_SRGB})),
            VK_FORMAT_R8G8B8A8_SRGB);
  // Only a gamma-converting blit remains.
  EXPECT_EQ(PreferredEyeSwapchainFormat(VK_FORMAT_R8G8B8A8_UNORM,
                                        formats({VK_FORMAT_B8G8R8A8_SRGB})),
            VK_FORMAT_UNDEFINED);
  EXPECT_EQ(PreferredEyeSwapchainFormat(VK_FORMAT_R16G16B16A16_SFLOAT,
                                        formats({VK_FORMAT_R8G8B8A8_SRGB})),
            VK_FORMAT_UNDEFINED);
}

// A runtime loaded through DeepBoundLibraries is reported as newly loaded
// and still unloads once every handle is closed, so each XR instance gets a
// fresh copy of the runtime.
TEST(OpenXrRuntimeLibraries, DeepBoundRuntimeUnloadsWithItsLastHandle) {
  const char* runtime = MOCKTAIL_TEST_OPENXR_RUNTIME;
  ASSERT_EQ(dlopen(runtime, RTLD_NOW | RTLD_NOLOAD), nullptr);
  const auto before = LoadedSharedObjects();
  void* loader_handle = nullptr;
  {
    const DeepBoundLibraries deep_bound({runtime});
    const auto loaded = SharedObjectsLoadedSince(before);
    EXPECT_NE(std::find(loaded.begin(), loaded.end(), std::string(runtime)),
              loaded.end());
    loader_handle = dlopen(runtime, RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(loader_handle, nullptr) << dlerror();
  }
  void* still_loaded = dlopen(runtime, RTLD_NOW | RTLD_NOLOAD);
  EXPECT_NE(still_loaded, nullptr);
  if (still_loaded != nullptr) dlclose(still_loaded);
  ASSERT_EQ(dlclose(loader_handle), 0);
  EXPECT_EQ(dlopen(runtime, RTLD_NOW | RTLD_NOLOAD), nullptr);
}

}  // namespace
}  // namespace mocktail::vr

namespace mocktail::vr::internal {
TEST(GazeDensityMapTest, ProtectsActualGazeAndFlipsOpenXrVerticalCoordinate) {
  const auto pixels=GazeDensityMap(64,64,.75f,.75f,2);
  const auto density=[&](unsigned x,unsigned y){return pixels[(y*64+x)*2];};
  EXPECT_EQ(density(56,8),255);
  EXPECT_EQ(density(52,8),255);
  EXPECT_LT(density(8,56),200);
}
TEST(GazeDensityMapTest, ProtectsGazeAtImageBoundaries) {
  const auto upper_left=GazeDensityMap(64,64,-1.f,1.f,2);
  const auto lower_right=GazeDensityMap(64,64,1.f,-1.f,2);
  EXPECT_EQ(upper_left.front(),255);
  EXPECT_EQ(lower_right.back(),255);
}
TEST(GazeDensityMapTest, InvalidGazeAndDisabledFoveationKeepFullDetail) {
  for(const auto pixels:{GazeDensityMap(64,64,NAN,0.f),GazeDensityMap(64,64,0.f,INFINITY),GazeDensityMap(64,64,0.f,0.f,0)})
    EXPECT_TRUE(std::all_of(pixels.begin(),pixels.end(),[](auto value){return value==255;}));
}
TEST(GazeDensityMapTest, KeepsFocusDetailedWhileReducingPeripheralShading) {
  const auto pixels=GazeDensityMap(64,64,0.f,0.f,1);
  unsigned full=0,coarse=0;
  for(unsigned i=0;i<pixels.size();i+=2) {
    EXPECT_GE(pixels[i],127);
    EXPECT_GE(pixels[i+1],127);
    full+=pixels[i]==255;coarse+=pixels[i]<200;
  }
  EXPECT_GT(full,4096u/10);
  EXPECT_GT(coarse,4096u/4);
}
}

namespace mocktail::vr::internal {
TEST(GazeDensityMapTest, EveryModePreservesTheSameFullQualityFocus) {
  for(int level=1;level<=3;++level) {
    const auto pixels=GazeDensityMap(64,64,.5f,-.5f,level);
    for(unsigned y=44;y<=52;++y)for(unsigned x=44;x<=52;++x)EXPECT_EQ(pixels[(y*64+x)*2],255);
  }
}
TEST(GazeDensityMapTest, RadiusClampsAndWholeFocusDiskStaysFullDetail) {
  EXPECT_FLOAT_EQ(ClampGazeRadius(NAN),.22f);
  EXPECT_FLOAT_EQ(ClampGazeRadius(-1.f),.08f);
  EXPECT_FLOAT_EQ(ClampGazeRadius(1.f),.35f);
  for(float radius:{.08f,.22f,.35f})for(int level=1;level<=3;++level) {
    const auto pixels=GazeDensityMap(81,81,.47f,-.31f,level,radius);
    for(unsigned y=0;y<81;++y)for(unsigned x=0;x<81;++x)
      if(std::hypot((x+.5f)/81-.735f,(y+.5f)/81-.655f)<=radius) {
        EXPECT_EQ(pixels[(y*81+x)*2],255);EXPECT_EQ(pixels[(y*81+x)*2+1],255);
      }
  }
}
TEST(GazeDensityMapTest, LargerRadiusIncreasesDetailedAreaAndModesReduceWorkInOrder) {
  auto cost=[](const auto& map){double total=0;for(unsigned i=0;i<map.size();i+=2)total+=double(map[i])*map[i+1]/(255.*255.);return total;};
  for(int level=1;level<=3;++level)
    EXPECT_LT(cost(GazeDensityMap(128,128,0,0,level,.08f)),cost(GazeDensityMap(128,128,0,0,level,.35f)));
  const auto quality=cost(GazeDensityMap(128,128,0,0,1));
  const auto balanced=cost(GazeDensityMap(128,128,0,0,2));
  const auto performance=cost(GazeDensityMap(128,128,0,0,3));
  EXPECT_GT(quality,balanced);EXPECT_GT(balanced,performance);
}
TEST(GazeDensityMapTest, EveryHardwareRegionIntersectingGazeIsFullDensity) {
  constexpr unsigned eye_size=2592,map_size=81;
  const float guard=.5f*1024/eye_size;
  for(float radius:{.08f,.22f,.35f})for(int level=1;level<=3;++level) {
    const auto map=GazeDensityMap(map_size,map_size,.4f,.3f,level,radius,guard,guard);
    for(unsigned tile_size:{32,256,512,864,1024})
      for(unsigned top=0;top<eye_size;top+=tile_size)for(unsigned left=0;left<eye_size;left+=tile_size) {
        const unsigned right=std::min(left+tile_size,eye_size),bottom=std::min(top+tile_size,eye_size);
        const float dx=std::max({left/float(eye_size)-.7f,0.f,.7f-right/float(eye_size)});
        const float dy=std::max({top/float(eye_size)-.35f,0.f,.35f-bottom/float(eye_size)});
        if(std::hypot(dx,dy)>radius)continue;
        const unsigned x=((left+right)/2)/32,y=((top+bottom)/2)/32;
        EXPECT_EQ(map[(y*map_size+x)*2],255);EXPECT_EQ(map[(y*map_size+x)*2+1],255);
      }
  }
}
TEST(GazeDensityMapTest, HalfFloatDensitiesUseExactReciprocalFragmentSizes) {
  EXPECT_EQ(HalfDensityMap({255,127,63,0,200}),(std::vector<std::uint16_t>{0x3c00,0x3800,0x3400,0x3c00,0x3c00}));
}
TEST(GazeDensityMapTest, QuantizedFocusCacheKeepsTheSameMapAndTracksRealMovement) {
  const auto a=QuantizeGazeCenter(81,81,0,0),b=QuantizeGazeCenter(81,81,.00001f,-.00001f);
  EXPECT_EQ(a.x,b.x);EXPECT_EQ(a.y,b.y);
  EXPECT_EQ(GazeDensityMap(81,81,0,0),GazeDensityMap(81,81,.00001f,-.00001f));
  const auto moved=QuantizeGazeCenter(81,81,.5f,.5f);
  EXPECT_GT(moved.x,a.x);EXPECT_LT(moved.y,a.y);
}
TEST(GazeDensityMapTest, FalloffMixesIntermediateAreasAndIsTemporallyStable) {
  const auto map=GazeDensityMap(128,128,0,0,3);
  EXPECT_EQ(map,GazeDensityMap(128,128,.00001f,-.00001f,3));
  std::set<unsigned> areas;
  for(unsigned i=0;i<map.size();i+=2)areas.insert(unsigned(map[i])*map[i+1]);
  EXPECT_EQ(areas.size(),5u);
  // Average shading work decreases across annuli rather than one binary cut.
  double totals[5]{};unsigned counts[5]{};
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x) {
    float d=std::hypot((x+.5f)/128-.5f,(y+.5f)/128-.5f);
    int ring=int((d-.235f)/.019f);if(d>=.235f && ring>=0 && ring<5) {
      totals[ring]+=double(map[(y*128+x)*2])*map[(y*128+x)*2+1];++counts[ring];
    }
  }
  for(int i=1;i<5;++i)EXPECT_LT(totals[i]/counts[i],totals[i-1]/counts[i-1]);
}
TEST(GazeProjectionTest, HeadRotationCancelsAndAsymmetricEyeFovIsRespected) {
  const float q[]={0,.38268343f,0,.92387953f};
  const float symmetric[]={-.78539816f,.78539816f,.78539816f,-.78539816f};
  float x=0,y=0;ASSERT_TRUE(ProjectGazeNdc(q,q,symmetric,&x,&y));EXPECT_NEAR(x,0.f,1e-5f);EXPECT_NEAR(y,0.f,1e-5f);
  const float identity[]={0,0,0,1};const float asymmetric[]={-1.10714872f,.78539816f,.78539816f,-.78539816f};
  ASSERT_TRUE(ProjectGazeNdc(identity,identity,asymmetric,&x,&y));EXPECT_NEAR(x,1.f/3.f,1e-5f);
}
TEST(GazeProjectionTest, UpwardEyeGazeHasPositiveNdcAndInvalidRayIsRejected) {
  const float gaze[]={.25881905f,0,0,.96592583f},identity[]={0,0,0,1};
  const float fov[]={-.78539816f,.78539816f,.78539816f,-.78539816f};float x=0,y=0;
  ASSERT_TRUE(ProjectGazeNdc(gaze,identity,fov,&x,&y));EXPECT_GT(y,0.f);EXPECT_NEAR(x,0.f,1e-5f);
  const float behind[]={0,1,0,0};EXPECT_FALSE(ProjectGazeNdc(behind,identity,fov,&x,&y));
}
}

namespace mocktail::vr::internal {
TEST(SteamFrameResolution, UsesTheSteamShortcutPixelDimension) {
  EXPECT_EQ(SteamFrameEyeResolution(R"({"steam.app.123":{"resolutionOverride":2832},"steamvr":{"resolutionOverride":1728}})","123"),2832u);
}
TEST(SteamFrameResolution, CanonicalOverrideWinsLegacyButPreservesExplicitSteamChoice) {
  const auto settings = R"({"steam.app.123":{"resolutionOverride":2832},"io.github.CoderDayton.nightcap.vr":{"resolutionOverride":2160},"io.github.coderdayton.nightcap.vr":{"resolutionOverride":1728},"steamvr":{"resolutionOverride":1440}})";
  EXPECT_EQ(SteamFrameEyeResolution(settings,"123"),2832u);
  EXPECT_EQ(SteamFrameEyeResolution(settings,""),2160u);
  EXPECT_EQ(SteamFrameEyeResolution(R"({"io.github.coderdayton.nightcap.vr":{"resolutionOverride":1728},"steamvr":{"resolutionOverride":1440}})",""),1728u);
  EXPECT_EQ(SteamFrameEyeResolution(R"({"io.github.CoderDayton.nightcap.vr":{"resolutionOverride":0},"io.github.coderdayton.nightcap.vr":{"resolutionOverride":1728}})",""),1728u);
}
TEST(SteamFrameResolution, AcceptsNativeFrameSliderValuesWithoutAnExtraMultiplier) {
  for(unsigned value:{128u,1728u,2160u,2832u,8192u})
    EXPECT_EQ(SteamFrameEyeResolution("{\"steam.app.123\":{\"resolutionOverride\":"+std::to_string(value)+"}}","123"),value);
}
TEST(SteamFrameResolution, RejectsMalformedOrOutOfRangeValues) {
  for(const char* value:{"-1","0","127","8193","1e100","\"2160\"","null","true"})
    EXPECT_EQ(SteamFrameEyeResolution(std::string("{\"steam.app.123\":{\"resolutionOverride\":")+value+"}}","123"),0u);
  EXPECT_EQ(SteamFrameEyeResolution("invalid","123"),0u);
  EXPECT_EQ(SteamFrameEyeResolution("[]","123"),0u);
  EXPECT_EQ(SteamFrameEyeResolution(R"({"steam.app.4294967296":{"resolutionOverride":2832}})","4294967296"),0u);
}
TEST(SteamFrameResolution, FallsBackToTheGlobalSettingForAutomaticOrMissingAppValues) {
  EXPECT_EQ(SteamFrameEyeResolution(R"({"steam.app.123":{"resolutionOverride":0},"steamvr":{"resolutionOverride":2160}})","123"),2160u);
  EXPECT_EQ(SteamFrameEyeResolution(R"({"steamvr":{"resolutionOverride":2160}})",""),2160u);
}
}

namespace mocktail::vr {
TEST(SteamFrameLiveResolution, InactiveBackendNeverTouchesGuestOrRuntimeResources) {
  OpenXrBackend backend;
  EXPECT_FALSE(backend.UpdateEyeResolution());
}
}


TEST(GazeDensityMapBuffers, ReusesStorageAcrossEyeMovements) {
  using namespace mocktail::vr::internal;
  std::vector<std::uint8_t> pixels;
  FillGazeDensityMap(pixels,64,64,0,0,3,.15f);
  const auto* storage=pixels.data();
  EXPECT_EQ(pixels[(32*64+32)*2],255);
  for(int i=0;i<100;++i){FillGazeDensityMap(pixels,64,64,(i%10-5)*.1f,0,3,.15f);EXPECT_EQ(pixels.data(),storage);}
  FillGazeDensityMap(pixels,64,64,0,0,0,.15f);
  EXPECT_TRUE(std::all_of(pixels.begin(),pixels.end(),[](auto value){return value==255;}));
}
TEST(GazeDensityMapBuffers, ReusesHalfFloatStorageWithoutChangingDensityValues) {
  using namespace mocktail::vr::internal;
  std::vector<std::uint16_t> values;
  FillHalfDensityMap(values,{255,127,63});
  const auto* storage=values.data();
  EXPECT_EQ(values,(std::vector<std::uint16_t>{0x3c00,0x3800,0x3400}));
  FillHalfDensityMap(values,{63,127,255});
  EXPECT_EQ(values.data(),storage);
  EXPECT_EQ(values,(std::vector<std::uint16_t>{0x3400,0x3800,0x3c00}));
}


TEST(DeferredEyeQueueContract, AcceptsOneKnownUnprotectedQueue) {
  VkDeviceQueueCreateInfo queue{};
  queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue.queueFamilyIndex = 3;
  queue.queueCount = 1;
  VkDeviceCreateInfo device{};
  device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device.queueCreateInfoCount = 1;
  device.pQueueCreateInfos = &queue;
  EXPECT_TRUE(mocktail::vr::internal::SingleUnprotectedVulkanQueue(&device));
  queue.queueCount = 2;
  EXPECT_FALSE(mocktail::vr::internal::SingleUnprotectedVulkanQueue(&device));
  queue.queueCount = 1;
  queue.flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;
  EXPECT_FALSE(mocktail::vr::internal::SingleUnprotectedVulkanQueue(&device));
}
TEST(DeferredEyeQueueContract, RejectsMissingOrMultipleQueueContracts) {
  EXPECT_FALSE(mocktail::vr::internal::SingleUnprotectedVulkanQueue(nullptr));
  VkDeviceCreateInfo device{};
  device.queueCreateInfoCount = 1;
  EXPECT_FALSE(mocktail::vr::internal::SingleUnprotectedVulkanQueue(&device));
  VkDeviceQueueCreateInfo queues[2]{};
  queues[0].queueCount = queues[1].queueCount = 1;
  queues[0].queueFamilyIndex = 0;
  queues[1].queueFamilyIndex = 1;
  device.pQueueCreateInfos = queues;
  device.queueCreateInfoCount = 2;
  EXPECT_FALSE(mocktail::vr::internal::SingleUnprotectedVulkanQueue(&device));
  device.queueCreateInfoCount = 1;
  queues[0].queueCount = 0;
  EXPECT_FALSE(mocktail::vr::internal::SingleUnprotectedVulkanQueue(&device));
}

TEST(RenderCorePreference, SelectsPerformancePeersOnlyFromAllowedCpus) {
  EXPECT_EQ(mocktail::vr::internal::PerformanceCpuSubset(
      {{0,216},{1,216},{2,855},{3,855},{4,855}}),
      (std::vector<unsigned>{2,3,4}));
}
TEST(RenderCorePreference, KeepsHomogeneousAndSmallAllocations) {
  EXPECT_EQ(mocktail::vr::internal::PerformanceCpuSubset({{3,855},{4,855}}),
      (std::vector<unsigned>{3,4}));
  EXPECT_EQ(mocktail::vr::internal::PerformanceCpuSubset({{7,1024}}),
      (std::vector<unsigned>{7}));
  EXPECT_TRUE(mocktail::vr::internal::PerformanceCpuSubset({}).empty());
}
TEST(RenderCorePreference, PreservesAllocationWithMissingCapacity) {
  EXPECT_EQ(mocktail::vr::internal::PerformanceCpuSubset({{0,0},{2,855},{3,855}}),
      (std::vector<unsigned>{0,2,3}));
}
TEST(RenderCorePreference, AvoidsConfinementToOneFastCore) {
  EXPECT_EQ(mocktail::vr::internal::PerformanceCpuSubset({{0,216},{1,216},{7,1024}}),
      (std::vector<unsigned>{0,1,7}));
}
TEST(RenderCorePreference, KeepsNearbyPerformanceCapacityClasses) {
  EXPECT_EQ(mocktail::vr::internal::PerformanceCpuSubset({{1,216},{4,820},{6,1024}}),
      (std::vector<unsigned>{4,6}));
}
