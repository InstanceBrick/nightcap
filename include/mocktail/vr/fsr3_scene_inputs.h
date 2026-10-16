#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace mocktail::vr {

// A scene-pass candidate, not an asynchronous borrow. This is delivered while
// the guest command buffer is recording, immediately after its render pass.
// The backend must verify owner/eye/generation, then record any required copies
// on that command buffer before returning. Image handles may be reused by the
// very next guest pass. A later queue submission/fence establishes completion.
struct TemporalSceneInputs {
  std::uint32_t version = 1;
  std::uint32_t size = sizeof(TemporalSceneInputs);
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkImage color = VK_NULL_HANDLE;
  VkImage depth = VK_NULL_HANDLE;
  VkFormat color_format = VK_FORMAT_UNDEFINED;
  VkFormat depth_format = VK_FORMAT_UNDEFINED;
  VkImageLayout color_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageSubresourceRange color_range{};
  VkImageSubresourceRange depth_range{};
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Set only when this pass actually clears its depth attachment. This is
  // evidence for normal (clear=1) versus reversed (clear=0) device depth.
  float depth_clear = 0.f;
  bool depth_clear_valid = false;
  void* owner = nullptr;
  void* guest_framebuffer = nullptr;
  int eye = -1;
  // Zero means the adapter has no frame number. The backend must stamp the
  // current published frame after matching the exact live eye provenance.
  std::uint64_t frame = 0;
};

// Native 3222 motion shader output before conversion: current clip.xyz/w minus
// previous clip.xyz/w. Its vertex shader negates Y only for gl_Position, so
// previous-UV minus current-UV is nativeDelta.xy * (-0.5, +0.5). XYZ are signed
// floats; W is zero, not a validity mask. The shader can discard large deltas.
struct TemporalMotionInputs {
  std::uint32_t version = 1;
  std::uint32_t size = sizeof(TemporalMotionInputs);
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageSubresourceRange range{};
  std::uint32_t width = 0, height = 0;
  void* owner = nullptr;
  void* guest_framebuffer = nullptr;
  int eye = -1;
  std::uint64_t frame = 0;
  float previous_uv_scale_x = -.5f;
  float previous_uv_scale_y = .5f;
};

inline bool IsTemporalMotionFormat(VkFormat format) {
  return format == VK_FORMAT_R16G16_SFLOAT ||
         format == VK_FORMAT_R16G16B16A16_SFLOAT ||
         format == VK_FORMAT_R32G32_SFLOAT ||
         format == VK_FORMAT_R32G32B32A32_SFLOAT;
}

inline bool IsTemporalClipMotionShaders(std::uint64_t vertex,
                                        std::uint64_t fragment) {
  // FNV-1a of exact native shader module bytes, verified against both the
  // fragment subtraction and vertex current/previous clip outputs. The three
  // adjacent world-space variants deliberately have a different contract.
  return fragment == 0x821fff499ec5bdbbull &&
      (vertex == 0x9b0d6a7041cad338ull ||
       vertex == 0x4a99a561ef9805b6ull ||
       vertex == 0x6dcaa6712cd5a2cdull);
}

inline bool IsTemporalDepthFormat(VkFormat format) {
  return format == VK_FORMAT_D16_UNORM ||
         format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
         format == VK_FORMAT_D32_SFLOAT ||
         format == VK_FORMAT_D16_UNORM_S8_UINT ||
         format == VK_FORMAT_D24_UNORM_S8_UINT ||
         format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

inline bool IsTemporalColorFormat(VkFormat format) {
  return format == VK_FORMAT_R8G8B8A8_UNORM ||
         format == VK_FORMAT_R8G8B8A8_SRGB ||
         format == VK_FORMAT_B8G8R8A8_UNORM ||
         format == VK_FORMAT_B8G8R8A8_SRGB ||
         format == VK_FORMAT_R16G16B16A16_SFLOAT ||
         format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ||
         format == VK_FORMAT_A2B10G10R10_UNORM_PACK32;
}

inline bool IsTemporalImageCandidate(const VkImageCreateInfo& info) {
  const bool depth = IsTemporalDepthFormat(info.format) &&
      (info.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  const bool color = IsTemporalColorFormat(info.format) &&
      (info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  const bool motion = IsTemporalMotionFormat(info.format) &&
      (info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  // Native transient targets may request lazy memory; changing their usage would
  // disagree with the engine allocator's original memory-type request.
  return (depth || color || motion) &&
      !(info.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) &&
      info.imageType == VK_IMAGE_TYPE_2D &&
      info.tiling == VK_IMAGE_TILING_OPTIMAL &&
      info.samples == VK_SAMPLE_COUNT_1_BIT && info.extent.depth == 1 &&
      info.extent.width >= 256 && info.extent.height >= 256 &&
      info.arrayLayers == 1 && info.mipLevels == 1 && info.flags == 0;
}

// Depth/stencil formats use depth only. FSR samples device depth; stencil must
// never be reinterpreted as scene depth or included in a depth-only copy.
inline VkImageAspectFlags TemporalDepthAspect(VkFormat format) {
  return IsTemporalDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
}

}  // namespace mocktail::vr

extern "C" {
// Availability tracks compatible resources from Vulkan startup. Enabled only
// selects preserving passes and per-frame capture while temporal FSR is active.
bool mocktail_vr_xr_fsr3_inputs_available();
bool mocktail_vr_xr_fsr3_inputs_enabled();
void mocktail_vr_xr_fsr3_scene_end(
    VkDevice device, VkCommandBuffer command,
    const mocktail::vr::TemporalSceneInputs* inputs);
void mocktail_vr_xr_fsr3_motion_end(
    VkDevice device, VkCommandBuffer command,
    const mocktail::vr::TemporalMotionInputs* inputs);
}
