#ifndef MOCKTAIL_VR_DESKTOP_MIRROR_H_
#define MOCKTAIL_VR_DESKTOP_MIRROR_H_

#include <algorithm>
#include <vulkan/vulkan.h>

namespace mocktail::vr::internal {

struct DesktopMirrorDispatch {
  PFN_vkCmdPipelineBarrier barrier;
  PFN_vkCmdClearColorImage clear;
  PFN_vkCmdBlitImage blit;
};

// Record the whole copy, including layout restoration, into the submission's
// command buffer. The caller owns synchronization, begin/end and submission.
inline void RecordDesktopMirrorCommands(
    VkCommandBuffer command, VkImage source, VkImageLayout source_layout,
    VkExtent2D source_extent, VkImage destination, VkExtent2D destination_extent,
    const DesktopMirrorDispatch& vk) {
  VkImageMemoryBarrier barriers[2]{};
  for (auto& barrier : barriers) {
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
  }
  barriers[0].image = source;
  barriers[0].oldLayout = source_layout;
  barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[1].image = destination;
  barriers[1].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vk.barrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
             barriers);
  const VkClearColorValue black{{0.f, 0.f, 0.f, 1.f}};
  vk.clear(command, destination, barriers[1].newLayout, &black, 1,
           &barriers[1].subresourceRange);
  VkMemoryBarrier clear_barrier{};
  clear_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  clear_barrier.srcAccessMask = clear_barrier.dstAccessMask =
      VK_ACCESS_TRANSFER_WRITE_BIT;
  vk.barrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &clear_barrier, 0, nullptr, 0,
             nullptr);
  const double scale =
      std::min(double(destination_extent.width) / source_extent.width,
               double(destination_extent.height) / source_extent.height);
  const int width = std::max(1, int(source_extent.width * scale));
  const int height = std::max(1, int(source_extent.height * scale));
  const int x = (int(destination_extent.width) - width) / 2;
  const int y = (int(destination_extent.height) - height) / 2;
  VkImageBlit region{};
  region.srcSubresource =
      region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.srcOffsets[1] = {static_cast<int>(source_extent.width),
                          static_cast<int>(source_extent.height), 1};
  region.dstOffsets[0] = {x, y, 0};
  region.dstOffsets[1] = {x + width, y + height, 1};
  vk.blit(command, source, barriers[0].newLayout, destination,
          barriers[1].newLayout, 1, &region, VK_FILTER_NEAREST);
  for (auto& barrier : barriers) {
    std::swap(barrier.oldLayout, barrier.newLayout);
    barrier.srcAccessMask = barrier.dstAccessMask;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  }
  vk.barrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2,
             barriers);
}

}  // namespace mocktail::vr::internal

#endif  // MOCKTAIL_VR_DESKTOP_MIRROR_H_
