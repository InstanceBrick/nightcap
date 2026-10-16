#pragma once

#include <vulkan/vulkan.h>

namespace mocktail::vr {

inline bool IsFoveationSceneSampleCount(VkSampleCountFlagBits samples) {
  return samples == VK_SAMPLE_COUNT_1_BIT || samples == VK_SAMPLE_COUNT_2_BIT ||
         samples == VK_SAMPLE_COUNT_4_BIT || samples == VK_SAMPLE_COUNT_8_BIT;
}

// Preserve the native MSAA attachments and resolve. Only the extra density map
// is single-sampled; changing the scene images or pipeline sample count here
// would invalidate native framebuffers and memory allocations.
inline bool IsFoveationScenePassCompatible(const VkRenderPassCreateInfo& info) {
  if (info.pNext || info.flags || info.subpassCount != 1 || !info.pSubpasses ||
      !info.pAttachments) {
    return false;
  }
  const auto& subpass = info.pSubpasses[0];
  if (subpass.flags || subpass.pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS ||
      subpass.colorAttachmentCount != 1 || !subpass.pColorAttachments ||
      !subpass.pDepthStencilAttachment || subpass.inputAttachmentCount ||
      subpass.preserveAttachmentCount) {
    return false;
  }
  const auto color = subpass.pColorAttachments[0].attachment;
  const auto depth = subpass.pDepthStencilAttachment->attachment;
  if (color >= info.attachmentCount || depth >= info.attachmentCount || color == depth) {
    return false;
  }
  const auto samples = info.pAttachments[color].samples;
  if (!IsFoveationSceneSampleCount(samples) || info.pAttachments[depth].samples != samples) {
    return false;
  }
  const auto resolve = subpass.pResolveAttachments ?
      subpass.pResolveAttachments[0].attachment : VK_ATTACHMENT_UNUSED;
  if (resolve != VK_ATTACHMENT_UNUSED) {
    if (samples == VK_SAMPLE_COUNT_1_BIT || info.attachmentCount != 3 ||
        resolve >= info.attachmentCount || resolve == color || resolve == depth ||
        info.pAttachments[resolve].samples != VK_SAMPLE_COUNT_1_BIT ||
        info.pAttachments[resolve].format != info.pAttachments[color].format) {
      return false;
    }
  } else if (info.attachmentCount != 2) {
    return false;
  }
  for (unsigned i = 0; i < info.attachmentCount; ++i) {
    if (info.pAttachments[i].flags) {
      return false;
    }
  }
  return true;
}

}  // namespace mocktail::vr
