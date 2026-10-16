#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "mocktail/vr/fsr3_scene_capture.h"

namespace mocktail::vr {
namespace {

template<class T> T Handle(std::uintptr_t value) {
  if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
  else return static_cast<T>(value);
}

struct RawVulkan {
  struct Barrier {
    VkCommandBuffer command;
    VkPipelineStageFlags source_stage, destination_stage;
    std::vector<VkImageMemoryBarrier> images;
  };
  struct Copy {
    VkCommandBuffer command;
    VkImage source, destination;
    VkImageLayout source_layout, destination_layout;
    VkImageCopy region;
  };
  std::uintptr_t next = 0x10000;
  std::vector<VkImageCreateInfo> creates;
  std::vector<VkMemoryAllocateInfo> allocations;
  std::vector<VkImage> destroyed;
  std::vector<VkDeviceMemory> freed;
  std::vector<Barrier> barriers;
  std::vector<Copy> copies;
  std::vector<std::string> commands;
  bool fail_bind = false;
};
RawVulkan* raw;

VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice, const VkImageCreateInfo* in,
                                         const VkAllocationCallbacks*, VkImage* out) {
  raw->creates.push_back(*in);
  *out = Handle<VkImage>(++raw->next);
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice, VkImage image,
                                       const VkAllocationCallbacks*) {
  raw->destroyed.push_back(image);
}
VKAPI_ATTR void VKAPI_CALL MemoryRequirements(VkDevice, VkImage,
                                             VkMemoryRequirements* out) {
  *out = {4096, 256, 2};
}
VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice,
    const VkMemoryAllocateInfo* in, const VkAllocationCallbacks*, VkDeviceMemory* out) {
  raw->allocations.push_back(*in);
  *out = Handle<VkDeviceMemory>(++raw->next);
  return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL FreeMemory(VkDevice, VkDeviceMemory memory,
                                     const VkAllocationCallbacks*) {
  raw->freed.push_back(memory);
}
VKAPI_ATTR VkResult VKAPI_CALL BindMemory(VkDevice, VkImage, VkDeviceMemory,
                                         VkDeviceSize) {
  return raw->fail_bind ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL PipelineBarrier(VkCommandBuffer command,
    VkPipelineStageFlags source, VkPipelineStageFlags destination,
    VkDependencyFlags, std::uint32_t, const VkMemoryBarrier*,
    std::uint32_t, const VkBufferMemoryBarrier*, std::uint32_t count,
    const VkImageMemoryBarrier* images) {
  raw->barriers.push_back({command, source, destination,
                           std::vector<VkImageMemoryBarrier>(images, images + count)});
  raw->commands.push_back("barrier");
}
VKAPI_ATTR void VKAPI_CALL CopyImage(VkCommandBuffer command,
    VkImage source, VkImageLayout source_layout, VkImage destination,
    VkImageLayout destination_layout, std::uint32_t count, const VkImageCopy* copies) {
  EXPECT_EQ(count, 1u);
  raw->copies.push_back({command, source, destination, source_layout,
                         destination_layout, copies[0]});
  raw->commands.push_back("copy");
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetProc(VkDevice, const char* name) {
#define PROC(n, f) if (std::strcmp(name, n) == 0) return reinterpret_cast<PFN_vkVoidFunction>(&f)
  PROC("vkCreateImage", CreateImage);
  PROC("vkDestroyImage", DestroyImage);
  PROC("vkGetImageMemoryRequirements", MemoryRequirements);
  PROC("vkAllocateMemory", AllocateMemory);
  PROC("vkFreeMemory", FreeMemory);
  PROC("vkBindImageMemory", BindMemory);
  PROC("vkCmdPipelineBarrier", PipelineBarrier);
  PROC("vkCmdCopyImage", CopyImage);
#undef PROC
  return nullptr;
}

class Fsr3SceneCaptureTest : public testing::Test {
 protected:
  void SetUp() override {
    raw = &vulkan;
    memory.memoryTypeCount = 2;
    memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }
  void TearDown() override {
    capture.Reset();
    raw = nullptr;
  }
  TemporalSceneInputs Depth(int eye = 0) const {
    TemporalSceneInputs in;
    in.depth = Handle<VkImage>(0x2000 + eye);
    in.depth_format = VK_FORMAT_D32_SFLOAT;
    in.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    in.depth_range = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    in.width = 1440; in.height = 1280; in.eye = eye;
    return in;
  }
  TemporalMotionInputs Motion(int eye = 0) const {
    TemporalMotionInputs in;
    in.image = Handle<VkImage>(0x3000 + eye);
    in.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    in.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    in.range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    in.width = 1440; in.height = 1280; in.eye = eye;
    return in;
  }
  bool RecordDepth(VkCommandBuffer command, std::uint64_t frame,
                    const TemporalSceneInputs& in) {
    return capture.Record(device, GetProc, memory, command, in, frame);
  }
  bool RecordMotion(VkCommandBuffer command, std::uint64_t frame,
                     const TemporalMotionInputs& in) {
    return capture.RecordMotion(device, GetProc, memory, command, in, frame);
  }
  RawVulkan vulkan;
  Fsr3SceneCapture capture;
  VkPhysicalDeviceMemoryProperties memory{};
  VkDevice device = Handle<VkDevice>(0x1000);
  VkCommandBuffer command = Handle<VkCommandBuffer>(0x4000);
};

TEST_F(Fsr3SceneCaptureTest, SameCommandSnapshotsBecomeConsumableOnlyAfterSubmission) {
  ASSERT_TRUE(RecordDepth(command, 42, Depth()));
  ASSERT_TRUE(RecordMotion(command, 42, Motion()));
  EXPECT_EQ(capture.Get(0, 42, 1440, 1280), nullptr);
  EXPECT_EQ(capture.GetMotion(0, 42, 1440, 1280), nullptr);
  EXPECT_EQ(capture.Get(0, 0, 1440, 1280), nullptr);
  EXPECT_EQ(capture.GetMotion(0, 0, 1440, 1280), nullptr);
  ASSERT_EQ(vulkan.copies.size(), 2u);
  EXPECT_EQ(vulkan.copies[0].command, command);
  EXPECT_EQ(vulkan.copies[1].command, command);
  EXPECT_EQ(vulkan.commands, (std::vector<std::string>{"barrier", "copy", "barrier",
                                                       "barrier", "copy", "barrier"}));
  // Unrelated or failed queue submissions do not publish this command's data.
  const auto other = Handle<VkCommandBuffer>(0x4001);
  capture.Submitted(1, &other);
  EXPECT_EQ(capture.Get(0, 42, 1440, 1280), nullptr);
  capture.Submitted(1, &command);
  const auto* depth = capture.Get(0, 42, 1440, 1280);
  const auto* motion = capture.GetMotion(0, 42, 1440, 1280);
  ASSERT_NE(depth, nullptr);
  ASSERT_NE(motion, nullptr);
  EXPECT_EQ(depth->depth, vulkan.copies[0].destination);
  EXPECT_EQ(motion->image, vulkan.copies[1].destination);
  EXPECT_EQ(motion->previous_uv_scale_x, -.5f);
  EXPECT_EQ(motion->previous_uv_scale_y, .5f);
  EXPECT_NE(depth->depth, Depth().depth);
  EXPECT_NE(motion->image, Motion().image);
  for (const auto& allocation : vulkan.allocations)
    EXPECT_EQ(allocation.memoryTypeIndex, 1u);
}

TEST_F(Fsr3SceneCaptureTest, LifetimeGateWaitsForEveryUnsubmittedCommand) {
  EXPECT_FALSE(capture.HasUnsubmitted());
  const auto other = Handle<VkCommandBuffer>(0x4001);
  ASSERT_TRUE(RecordDepth(command, 42, Depth()));
  ASSERT_TRUE(RecordMotion(other, 42, Motion(1)));
  EXPECT_TRUE(capture.HasUnsubmitted());
  capture.Submitted(1, &command);
  // A successful submission of one command cannot release snapshots that a
  // second recorded command still references before reaching the queue.
  EXPECT_TRUE(capture.HasUnsubmitted());
  capture.Submitted(0, nullptr);
  EXPECT_TRUE(capture.HasUnsubmitted());
  capture.Discard(other);
  EXPECT_FALSE(capture.HasUnsubmitted());
  EXPECT_NE(capture.Get(0, 42, 1440, 1280), nullptr);
  EXPECT_EQ(capture.GetMotion(1, 42, 1440, 1280), nullptr);
}

TEST_F(Fsr3SceneCaptureTest, NewRecordingRearmsLifetimeGateUntilSubmitOrDiscard) {
  ASSERT_TRUE(RecordDepth(command, 7, Depth()));
  capture.Submitted(1, &command);
  EXPECT_FALSE(capture.HasUnsubmitted());
  ASSERT_TRUE(RecordMotion(command, 8, Motion()));
  EXPECT_TRUE(capture.HasUnsubmitted());
  const auto unrelated = Handle<VkCommandBuffer>(0x4002);
  capture.Submitted(1, &unrelated);
  EXPECT_TRUE(capture.HasUnsubmitted());
  capture.Discard(command);
  EXPECT_FALSE(capture.HasUnsubmitted());
  EXPECT_EQ(capture.GetMotion(0, 8, 1440, 1280), nullptr);
  ASSERT_TRUE(RecordMotion(command, 9, Motion()));
  EXPECT_TRUE(capture.HasUnsubmitted());
  capture.Submitted(1, &command);
  EXPECT_FALSE(capture.HasUnsubmitted());
  EXPECT_NE(capture.GetMotion(0, 9, 1440, 1280), nullptr);
  capture.Reset();
  EXPECT_FALSE(capture.HasUnsubmitted());
}

TEST_F(Fsr3SceneCaptureTest, DiscardPreventsPublishingNeverSubmittedCommands) {
  ASSERT_TRUE(RecordDepth(command, 12, Depth()));
  ASSERT_TRUE(RecordMotion(command, 12, Motion()));
  capture.Discard(command);
  capture.Submitted(1, &command);
  EXPECT_EQ(capture.Get(0, 12, 1440, 1280), nullptr);
  EXPECT_EQ(capture.GetMotion(0, 12, 1440, 1280), nullptr);
  ASSERT_TRUE(RecordDepth(command, 13, Depth()));
  ASSERT_TRUE(RecordMotion(command, 13, Motion()));
  capture.Submitted(1, &command);
  EXPECT_NE(capture.Get(0, 13, 1440, 1280), nullptr);
  EXPECT_NE(capture.GetMotion(0, 13, 1440, 1280), nullptr);
}

TEST_F(Fsr3SceneCaptureTest, RejectsStaleFramesOtherEyesAndWrongExtents) {
  ASSERT_TRUE(RecordDepth(command, 42, Depth()));
  ASSERT_TRUE(RecordMotion(command, 42, Motion()));
  capture.Submitted(1, &command);
  for (int eye : {-1, 1, 2}) {
    EXPECT_EQ(capture.Get(eye, 42, 1440, 1280), nullptr);
    EXPECT_EQ(capture.GetMotion(eye, 42, 1440, 1280), nullptr);
  }
  EXPECT_EQ(capture.Get(0, 41, 1440, 1280), nullptr);
  EXPECT_EQ(capture.GetMotion(0, 43, 1440, 1280), nullptr);
  EXPECT_EQ(capture.Get(0, 42, 1441, 1280), nullptr);
  EXPECT_EQ(capture.GetMotion(0, 42, 1440, 1279), nullptr);
  capture.Discard(command);
  ASSERT_TRUE(RecordDepth(command, 43, Depth()));
  ASSERT_TRUE(RecordMotion(command, 43, Motion()));
  capture.Submitted(1, &command);
  EXPECT_EQ(capture.Get(0, 42, 1440, 1280), nullptr);
  EXPECT_EQ(capture.GetMotion(0, 42, 1440, 1280), nullptr);
}

TEST_F(Fsr3SceneCaptureTest, DepthStencilTransitionsBothAspectsButCopiesOnlyDepth) {
  auto in = Depth();
  in.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
  in.depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  ASSERT_TRUE(RecordDepth(command, 1, in));
  ASSERT_EQ(vulkan.barriers.size(), 2u);
  ASSERT_EQ(vulkan.copies.size(), 1u);
  constexpr auto both = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  const auto& before = vulkan.barriers[0].images;
  const auto& after = vulkan.barriers[1].images;
  ASSERT_EQ(before.size(), 2u);
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(before[0].subresourceRange.aspectMask, both);
  EXPECT_EQ(before[1].subresourceRange.aspectMask, both);
  EXPECT_EQ(after[0].subresourceRange.aspectMask, both);
  EXPECT_EQ(after[1].subresourceRange.aspectMask, both);
  EXPECT_EQ(before[0].oldLayout, in.depth_layout);
  EXPECT_EQ(before[0].newLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  EXPECT_EQ(after[0].oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  EXPECT_EQ(after[0].newLayout, in.depth_layout);
  EXPECT_EQ(after[1].newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  EXPECT_EQ(vulkan.copies[0].region.srcSubresource.aspectMask, VK_IMAGE_ASPECT_DEPTH_BIT);
  EXPECT_EQ(vulkan.copies[0].region.dstSubresource.aspectMask, VK_IMAGE_ASPECT_DEPTH_BIT);
  EXPECT_EQ(vulkan.copies[0].region.extent.width, 1440u);
  EXPECT_EQ(vulkan.copies[0].region.extent.height, 1280u);
}

TEST_F(Fsr3SceneCaptureTest, MotionTransitionsRestoreGuestLayoutAndKeepSignedFloatFormat) {
  auto in = Motion();
  in.layout = VK_IMAGE_LAYOUT_GENERAL;
  ASSERT_TRUE(RecordMotion(command, 9, in));
  ASSERT_EQ(vulkan.creates.size(), 1u);
  EXPECT_EQ(vulkan.creates[0].format, VK_FORMAT_R16G16B16A16_SFLOAT);
  EXPECT_EQ(vulkan.creates[0].samples, VK_SAMPLE_COUNT_1_BIT);
  EXPECT_EQ(vulkan.creates[0].usage,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  ASSERT_EQ(vulkan.barriers.size(), 2u);
  const auto& before = vulkan.barriers[0].images;
  const auto& after = vulkan.barriers[1].images;
  EXPECT_EQ(before[0].oldLayout, VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(after[0].newLayout, VK_IMAGE_LAYOUT_GENERAL);
  EXPECT_EQ(after[1].newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  EXPECT_EQ(before[0].subresourceRange.aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
  EXPECT_EQ(vulkan.copies[0].region.srcSubresource.aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
  EXPECT_EQ(vulkan.copies[0].source_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  EXPECT_EQ(vulkan.copies[0].destination_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

TEST_F(Fsr3SceneCaptureTest, ResizeNeedsDrainAndResetBeforeReplacingSnapshots) {
  auto depth = Depth();
  auto motion = Motion();
  EXPECT_FALSE(capture.NeedsResize(depth));
  ASSERT_TRUE(RecordDepth(command, 7, depth));
  ASSERT_TRUE(RecordMotion(command, 7, motion));
  EXPECT_FALSE(capture.NeedsResize(depth));
  depth.width = motion.width = 1728;
  EXPECT_TRUE(capture.NeedsResize(depth));
  EXPECT_FALSE(RecordDepth(command, 8, depth));
  EXPECT_FALSE(RecordMotion(command, 8, motion));
  EXPECT_TRUE(vulkan.destroyed.empty());
  capture.Reset();
  EXPECT_EQ(vulkan.destroyed.size(), 2u);
  EXPECT_EQ(vulkan.freed.size(), 2u);
  ASSERT_TRUE(RecordDepth(command, 8, depth));
  ASSERT_TRUE(RecordMotion(command, 8, motion));
  capture.Submitted(1, &command);
  EXPECT_NE(capture.Get(0, 8, 1728, 1280), nullptr);
  EXPECT_NE(capture.GetMotion(0, 8, 1728, 1280), nullptr);
}

TEST_F(Fsr3SceneCaptureTest, AllocationFailureCleansResourcesAndCanRecover) {
  vulkan.fail_bind = true;
  EXPECT_FALSE(RecordDepth(command, 1, Depth()));
  EXPECT_EQ(vulkan.destroyed.size(), 1u);
  EXPECT_EQ(vulkan.freed.size(), 1u);
  EXPECT_TRUE(vulkan.copies.empty());
  EXPECT_EQ(capture.Get(0, 1, 1440, 1280), nullptr);
  vulkan.fail_bind = false;
  ASSERT_TRUE(RecordDepth(command, 2, Depth()));
  capture.Submitted(1, &command);
  EXPECT_NE(capture.Get(0, 2, 1440, 1280), nullptr);
}

TEST_F(Fsr3SceneCaptureTest, RejectsInvalidSourcesBeforeRecordingCommands) {
  auto depth = Depth();
  depth.eye = 2;
  EXPECT_FALSE(RecordDepth(command, 1, depth));
  depth = Depth(); depth.depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  EXPECT_FALSE(RecordDepth(command, 1, depth));
  depth = Depth(); depth.width = 0;
  EXPECT_FALSE(RecordDepth(command, 1, depth));
  depth = Depth(); depth.depth_range.layerCount = 2;
  EXPECT_FALSE(RecordDepth(command, 1, depth));
  EXPECT_FALSE(RecordDepth(command, 0, Depth()));
  auto motion = Motion(); motion.previous_uv_scale_y = -.5f;
  EXPECT_FALSE(RecordMotion(command, 1, motion));
  motion = Motion(); motion.format = VK_FORMAT_R8G8B8A8_UNORM;
  EXPECT_FALSE(RecordMotion(command, 1, motion));
  motion = Motion(); motion.range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  EXPECT_FALSE(RecordMotion(command, 1, motion));
  EXPECT_TRUE(vulkan.commands.empty());
  EXPECT_TRUE(vulkan.creates.empty());
}

}  // namespace
}  // namespace mocktail::vr
