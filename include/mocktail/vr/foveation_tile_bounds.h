#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mocktail::vr {

struct FoveationTileDriverIdentity {
  std::uint32_t vendor_id = 0;
  std::uint32_t device_id = 0;
  std::uint32_t driver_id = 0;
  std::uint32_t driver_version = 0;
  std::string_view driver_name;
  std::string_view driver_info;
  std::array<std::uint8_t, 16> driver_uuid{};
};

// Values describe the complete render pass, including its density attachment.
// Attachment formats use VkFormat's numeric values without requiring Vulkan.
struct FoveationTilePassIdentity {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t color_format = 0;
  std::uint32_t depth_format = 0;
  std::uint32_t color_samples = 0;
  std::uint32_t depth_samples = 0;
  std::uint32_t framebuffer_layers = 0;
  std::uint32_t subpass_count = 0;
  std::uint32_t total_attachment_count = 0;
  std::uint32_t color_attachment_count = 0;
  std::uint32_t gmem_attachment_count = 0;
  std::uint32_t resolve_attachment_mask = 0;
  std::uint32_t input_attachment_mask = 0;
  std::uint32_t feedback_attachment_mask = 0;
  std::uint32_t view_mask = 0;
  bool has_depth_attachment = false;
  bool has_density_attachment = false;
  bool density_offsets_enabled = false;
  bool multisampled_render_to_single_sampled = false;
  bool custom_resolve = false;
  bool has_shading_rate_attachment = false;
};

struct FoveationTileBound {
  std::uint32_t width;
  std::uint32_t height;
};

// A calibrated entry for one driver and pass tuple, not a hardware maximum.
// Unknown identities retain the caller's conservative policy. This exact entry
// passed 588 GPU focus cases; the matching driver ELF forces FDM's AVOID_CCU
// layout for all calibrated load/store combinations and the autotune path.
inline std::optional<FoveationTileBound> KnownFoveationTileBound(
    const FoveationTileDriverIdentity& driver,
    const FoveationTilePassIdentity& pass) {
  constexpr std::array<std::uint8_t, 16> kDriverUuid{
      0x58, 0x6b, 0x9f, 0xa4, 0x51, 0x4c, 0xb8, 0x8b,
      0x3c, 0x11, 0xa9, 0x68, 0xef, 0x4d, 0x62, 0xe4};
  if (driver.vendor_id != 0x5143 || driver.device_id != 0x43051401 ||
      driver.driver_id != 18 || driver.driver_version != 109060195 ||
      driver.driver_name != "turnip Mesa driver" ||
      driver.driver_info != "Mesa 26.3.0-devel (git-abc426bb62)" ||
      driver.driver_uuid != kDriverUuid) {
    return std::nullopt;
  }
  if (pass.width != 2160 || pass.height != 2160 ||
      pass.color_format != 37 || pass.depth_format != 126 ||
      pass.color_samples != 1 || pass.depth_samples != 1 ||
      pass.framebuffer_layers != 1 || pass.subpass_count != 1 ||
      pass.total_attachment_count != 3 || pass.color_attachment_count != 1 ||
      pass.gmem_attachment_count != 2 ||
      pass.resolve_attachment_mask || pass.input_attachment_mask ||
      pass.feedback_attachment_mask || pass.view_mask ||
      !pass.has_depth_attachment || !pass.has_density_attachment ||
      pass.density_offsets_enabled ||
      pass.multisampled_render_to_single_sampled || pass.custom_resolve ||
      pass.has_shading_rate_attachment) {
    return std::nullopt;
  }
  return FoveationTileBound{768, 448};
}

}  // namespace mocktail::vr
