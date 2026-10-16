#pragma once
#include <vulkan/vulkan.h>
#include "mocktail/vr/fsr_settings.h"
#include "mocktail/vr/temporal_reprojection.h"
#include <cstdint>
#include <cstddef>
#include <unordered_map>

namespace mocktail::vr {
// FP32 FSR 1 EASU, followed by gaze-only RCAS after the normal gaze AA.
// Uses raw host dispatch and the backend's existing copy command/fence.
class FsrUpscaler {
 public:
  ~FsrUpscaler() { Reset(); }
  bool Prepare(VkDevice, PFN_vkGetDeviceProcAddr,
               const VkPhysicalDeviceMemoryProperties&, int eye,
               VkImage source, VkFormat source_format, VkImage destination,
               VkFormat output_format, std::uint32_t input_width,
               std::uint32_t input_height, std::uint32_t output_width,
               std::uint32_t output_height, bool temporal=false);
  void RecordUpscale(VkCommandBuffer, int eye, float gaze_x=0, float gaze_y=0,
                     float radius=.15f);
  void RecordTemporal(VkCommandBuffer,int eye,const TemporalEyePose&,
                      float gaze_x,float gaze_y,float radius,std::uint64_t now);
  void RecordSharpen(VkCommandBuffer, int eye, float gaze_x, float gaze_y,
                     float radius);
  VkImage image(int eye) const { return eyes_[eye].image; }
  VkFormat intermediate_format() const;
  void ForgetImage(VkImage);
  void Reset();
 private:
  template<class T> T Proc(const char* name) const {
    return reinterpret_cast<T>(proc_(device_, name));
  }
  bool Initialize(VkDevice, PFN_vkGetDeviceProcAddr, VkFormat);
  VkPipeline Pipeline(VkRenderPass, const std::uint32_t*, std::size_t);
  struct Eye {
    VkImage image{}, source{}, destination{}, gamma_image{};
    VkDeviceMemory memory{}, gamma_memory{};
    VkImageView view{}, source_view{};
    VkFramebuffer framebuffer{};
    VkDescriptorSet sets[3]{};
    VkImage history_image{};VkDeviceMemory history_memory{};VkImageView history_view{};
    bool history_initialized=false,history_failed=false;
    TemporalEyePose previous_pose;
    float previous_center[2]{},previous_radius=0;
    std::uint64_t previous_time=0;
    VkFormat source_format{};
    std::uint32_t in_width=0, in_height=0, width=0, height=0;
  };
  struct Target { VkImageView view{}; VkFramebuffer framebuffer{}; };
  void FreeEye(Eye&);
  bool PrepareHistory(const VkPhysicalDeviceMemoryProperties&,int eye);
  struct alignas(16) Constants {
    std::uint32_t con[4][4]{};
    float texel[2]{}, center[2]{};
    float radius=0;
    std::uint32_t input_srgb=0, output_srgb=0, reserved=0;
  };
  static_assert(sizeof(Constants)==96);
  struct alignas(16) TemporalConstants {
    float rows[3][4]{},texel[2]{},center[2]{},previous_center[2]{};
    float radius=0,previous_radius=0,blend=0;
    std::uint32_t output_srgb=0,reserved[2]{};
  };
  static_assert(sizeof(TemporalConstants)==96);
  void Draw(VkCommandBuffer, VkRenderPass, VkFramebuffer, VkPipeline,
            VkDescriptorSet, const void*, const Eye&, VkRect2D);
  void Barrier(VkCommandBuffer, VkImage, VkImageLayout, VkImageLayout,
               VkAccessFlags, VkAccessFlags, VkPipelineStageFlags,
               VkPipelineStageFlags);
  VkDevice device_{};
  PFN_vkGetDeviceProcAddr proc_{};
  VkFormat format_{};
  VkRenderPass passes_[2]{};
  VkPipeline pipelines_[3]{};
  VkDescriptorSetLayout descriptor_layout_{};
  VkDescriptorPool descriptor_pool_{};
  VkPipelineLayout layout_{};
  VkSampler sampler_{};
  Eye eyes_[2];
  std::unordered_map<VkImage,Target> targets_;
};
}
