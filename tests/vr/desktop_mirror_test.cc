#include "mocktail/vr/desktop_mirror.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace mocktail::vr::internal {
namespace {
template <class T>
T Handle(std::uintptr_t value) {
  return reinterpret_cast<T>(value);
}
VkCommandBuffer expected_command;
std::vector<std::array<VkImageMemoryBarrier, 2>> transitions;
std::vector<int> operations;
VkImageBlit copied_region;
VKAPI_ATTR void VKAPI_CALL Barrier(
    VkCommandBuffer command, VkPipelineStageFlags, VkPipelineStageFlags,
    VkDependencyFlags, uint32_t memory_count, const VkMemoryBarrier* memory,
    uint32_t buffer_count, const VkBufferMemoryBarrier*, uint32_t image_count,
    const VkImageMemoryBarrier* images) {
  EXPECT_EQ(command, expected_command);
  EXPECT_EQ(buffer_count, 0U);
  if (image_count) {
    ASSERT_EQ(image_count, 2U);
    EXPECT_EQ(memory_count, 0U);
    transitions.push_back({images[0], images[1]});
    operations.push_back(1);
  } else {
    ASSERT_EQ(memory_count, 1U);
    EXPECT_EQ(memory->srcAccessMask, VK_ACCESS_TRANSFER_WRITE_BIT);
    EXPECT_EQ(memory->dstAccessMask, VK_ACCESS_TRANSFER_WRITE_BIT);
    operations.push_back(3);
  }
}
VKAPI_ATTR void VKAPI_CALL Clear(
    VkCommandBuffer command, VkImage image, VkImageLayout layout,
    const VkClearColorValue* color, uint32_t count,
    const VkImageSubresourceRange* range) {
  EXPECT_EQ(command, expected_command);
  EXPECT_EQ(image, Handle<VkImage>(102));
  EXPECT_EQ(layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  EXPECT_EQ(color->float32[0], 0.F);
  EXPECT_EQ(color->float32[3], 1.F);
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(range->aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
  operations.push_back(2);
}
VKAPI_ATTR void VKAPI_CALL Blit(
    VkCommandBuffer command, VkImage source, VkImageLayout source_layout,
    VkImage destination, VkImageLayout destination_layout, uint32_t count,
    const VkImageBlit* region, VkFilter filter) {
  EXPECT_EQ(command, expected_command);
  EXPECT_EQ(source, Handle<VkImage>(101));
  EXPECT_EQ(destination, Handle<VkImage>(102));
  EXPECT_EQ(source_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  EXPECT_EQ(destination_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(filter, VK_FILTER_NEAREST);
  copied_region = *region;
  operations.push_back(4);
}
class DesktopMirrorCommands : public testing::Test {
 protected:
  void SetUp() override {
    expected_command = Handle<VkCommandBuffer>(777);
    transitions.clear();
    operations.clear();
    copied_region = {};
  }
  void Record(VkExtent2D source, VkExtent2D destination) {
    RecordDesktopMirrorCommands(
        expected_command, Handle<VkImage>(101),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, source, Handle<VkImage>(102),
        destination, {Barrier, Clear, Blit});
  }
};
TEST_F(DesktopMirrorCommands, CopyAndRestorationUseTheSameSubmissionCommand) {
  Record({2160, 2160}, {1920, 1080});
  EXPECT_EQ(operations, (std::vector<int>{1, 2, 3, 4, 1}));
  ASSERT_EQ(transitions.size(), 2U);
  for (unsigned image = 0; image != 2; ++image) {
    const auto& before = transitions[0][image];
    const auto& after = transitions[1][image];
    EXPECT_EQ(after.image, before.image);
    EXPECT_EQ(after.oldLayout, before.newLayout);
    EXPECT_EQ(after.newLayout, before.oldLayout);
    EXPECT_EQ(after.srcAccessMask, before.dstAccessMask);
    EXPECT_EQ(after.dstAccessMask, VK_ACCESS_MEMORY_READ_BIT);
    EXPECT_EQ(after.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    EXPECT_EQ(after.dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
  }
  EXPECT_EQ(transitions[1][0].newLayout,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  EXPECT_EQ(transitions[1][1].newLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
TEST_F(DesktopMirrorCommands, CentersTheLeftEyeWithoutStretchingIt) {
  Record({2160, 2160}, {1920, 1080});
  EXPECT_EQ(copied_region.srcOffsets[1].x, 2160);
  EXPECT_EQ(copied_region.srcOffsets[1].y, 2160);
  EXPECT_EQ(copied_region.dstOffsets[0].x, 420);
  EXPECT_EQ(copied_region.dstOffsets[0].y, 0);
  EXPECT_EQ(copied_region.dstOffsets[1].x, 1500);
  EXPECT_EQ(copied_region.dstOffsets[1].y, 1080);
}
TEST_F(DesktopMirrorCommands, FitsWideEyesWithinTallDesktopWindows) {
  Record({2000, 1000}, {600, 800});
  EXPECT_EQ(copied_region.dstOffsets[0].x, 0);
  EXPECT_EQ(copied_region.dstOffsets[0].y, 250);
  EXPECT_EQ(copied_region.dstOffsets[1].x, 600);
  EXPECT_EQ(copied_region.dstOffsets[1].y, 550);
}
}  // namespace
}  // namespace mocktail::vr::internal
