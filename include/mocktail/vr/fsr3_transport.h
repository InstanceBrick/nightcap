#pragma once
#include <array>
#include <vulkan/vulkan.h>
#include "mocktail/vr/fsr3_upscaler.h"

namespace mocktail::vr {
// Explicit encoded-native -> linear-FSR -> encoded-native transport.
// The caller drains uses before resizing/resetting these reusable eye images.
class Fsr3Transport {
 public:
  ~Fsr3Transport() { Reset(); }
  bool Prepare(VkDevice,PFN_vkGetDeviceProcAddr,
               const VkPhysicalDeviceMemoryProperties&,VkFormat source,
               std::uint32_t width,std::uint32_t height,
               std::uint32_t output_width,std::uint32_t output_height);
  void Begin(VkCommandBuffer,int eye,VkImage encoded_source);
  void Finish(VkCommandBuffer,int eye);
  Fsr3Upscaler::Image color(int eye) const;
  Fsr3Upscaler::Image output(int eye) const;
  VkImage encoded(int eye) const {return eyes_[eye].encoded.image;}
  VkFormat encoded_format() const {return srgb_format_;}
  void Reset();
 private:
  struct Resource {VkImage image{};VkDeviceMemory memory{};};
  struct Eye {Resource color,output,encoded;};
  template<class T>T Proc(const char* name)const{return reinterpret_cast<T>(proc_(device_,name));}
  bool Create(Resource&,VkFormat,std::uint32_t,std::uint32_t,VkImageUsageFlags,
              const VkPhysicalDeviceMemoryProperties&);
  void Destroy(Resource&);
  void Barrier(VkCommandBuffer,VkImage,VkImageLayout,VkImageLayout,VkAccessFlags,VkAccessFlags);
  VkDevice device_{};
  PFN_vkGetDeviceProcAddr proc_{};
  VkFormat source_format_{},srgb_format_{};
  std::uint32_t width_{},height_{},output_width_{},output_height_{};
  std::array<Eye,2> eyes_{};
};
}
