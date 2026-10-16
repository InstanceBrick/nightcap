#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstddef>
#include <unordered_map>

namespace mocktail::vr {

// Gaze-limited edge filtering after the normal eye copy.
// Every call uses raw host dispatch. The owner waits its copy fence before
// changing descriptors, dropping views, or destroying resources.
class GazeAa {
 public:
  ~GazeAa() { Reset(); }
  bool Prepare(VkDevice, PFN_vkGetDeviceProcAddr, int eye, VkImage source,
               VkFormat source_format, VkImage destination,
               VkFormat destination_format, std::uint32_t width,
               std::uint32_t height, int mode=1,
               const VkPhysicalDeviceMemoryProperties* memory=nullptr);
  bool Record(VkCommandBuffer, int eye, float gaze_x, float gaze_y, int mode=1,
              float radius=.22f);
  void ForgetImage(VkImage);
  void Reset();

 private:
  template<class T> T Proc(const char* name) const {
    return reinterpret_cast<T>(get_proc_(device_,name));
  }
  bool Initialize(VkDevice,PFN_vkGetDeviceProcAddr,VkFormat);
  VkPipeline MakePipeline(VkRenderPass,const std::uint32_t*,std::size_t);
  bool PrepareSmaa(int,const VkPhysicalDeviceMemoryProperties&,int mode);
  void RecordSmaa(VkCommandBuffer,int,int,const void*,const VkRect2D&);
  void RecordPass(VkCommandBuffer,int,VkPipeline,VkRenderPass,VkFramebuffer,
                  VkDescriptorSet,const void*,const VkRect2D&,bool);
  void DestroySmaa();
  struct Texture { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; };
  bool MakeTexture(Texture&,VkFormat,unsigned,unsigned,VkImageUsageFlags);
  void FreeTexture(Texture&);
  struct SmaaEye {
    Texture textures[2];
    VkFramebuffer framebuffers[2]{};
    VkDescriptorSet descriptors[2]{};
    std::uint32_t width=0,height=0;
    VkImageView descriptor_source{};
  };
  struct Source { VkImage image{}; VkImageView view{}; VkFormat format{}; bool descriptor_ready=false; };
  struct Target { VkImageView view{}; VkFramebuffer framebuffer{}; };
  VkDevice device_{};
  PFN_vkGetDeviceProcAddr get_proc_{};
  VkFormat format_=VK_FORMAT_UNDEFINED;
  VkDescriptorSetLayout descriptor_layout_{};
  VkDescriptorPool descriptor_pool_{};
  VkDescriptorSet descriptor_sets_[2]{};
  VkPipelineLayout pipeline_layout_{};
  VkPipeline pipeline_{};
  VkRenderPass render_pass_{};
  VkSampler sampler_{};
  VkRenderPass smaa_pass_{};
  VkPipeline smaa_pipelines_[2][3]{};
  SmaaEye smaa_eyes_[2];
  Texture smaa_luts_[2];
  VkBuffer smaa_staging_{};
  VkDeviceMemory smaa_staging_memory_{};
  bool smaa_uploaded_=false;
  Source sources_[2];
  std::unordered_map<VkImage,Target> targets_;
  VkImage destinations_[2]{};
  std::uint32_t widths_[2]{},heights_[2]{};
};
}
