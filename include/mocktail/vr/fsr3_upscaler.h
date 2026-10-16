#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

namespace mocktail::vr {

// AMD FSR 3.1.4 temporal upscaling. Each eye owns independent temporal history.
// GPU completion is the owner's responsibility before Reset or destruction.
class Fsr3Upscaler {
 public:
  struct Configuration {
    std::uint32_t render_width{}, render_height{};
    std::uint32_t output_width{}, output_height{};
    bool inverted_depth=false, infinite_depth=false, hdr=false;
    // Estimate image motion with AMD Optical Flow when engine vectors are
    // unavailable. This is a fallback with its own compute cost and limits.
    bool estimate_motion=false;
    bool motion_vectors_jittered=false;
    // Enable only when shaderFloat16 was enabled on the logical VkDevice.
    bool allow_fp16=false;
  };
  struct Image {
    VkImage image{};
    VkFormat format{};
    std::uint32_t width{}, height{};
  };
  struct JitterOffset { float x{}, y{}; };
  struct Frame {
    // Inputs must be sampled images in SHADER_READ_ONLY_OPTIMAL, output a
    // storage image in GENERAL. The SDK restores those layouts after dispatch.
    // Color is LINEAR: an sRGB image format decodes encoded source bytes.
    // The output should be RGBA16F linear; encode it before copying to an
    // encoded UNORM eye image. Depth and color must describe the same view.
    Image color, depth, motion_vectors, output;
    Image reactive, transparency;
    // Unjittered motion is previous pixel position minus current position.
    // Pixel-unit vectors use {1,1}; normalized UV vectors use render size.
    float motion_vector_scale[2]{1.f,1.f};
    JitterOffset jitter;
    float delta_ms=16.667f, near_plane=.1f, far_plane=1000.f;
    float vertical_fov=1.5707963f, meters_per_world_unit=1.f;
    float pre_exposure=1.f, sharpness=.2f;
    bool sharpening=true, reset=false;
  };

  Fsr3Upscaler();
  ~Fsr3Upscaler();
  Fsr3Upscaler(const Fsr3Upscaler&)=delete;
  Fsr3Upscaler& operator=(const Fsr3Upscaler&)=delete;
  bool Configure(VkPhysicalDevice, VkDevice, PFN_vkGetDeviceProcAddr,
                 const VkPhysicalDeviceMemoryProperties&, const Configuration&);
  bool Record(VkCommandBuffer, int eye, const Frame&);
  Image estimated_motion(int eye) const;
  void Reset();
  int last_error() const;
  static JitterOffset Jitter(std::uint64_t frame, std::uint32_t render_width,
                             std::uint32_t output_width);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
