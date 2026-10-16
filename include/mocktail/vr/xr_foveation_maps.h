#pragma once

#include <cstdint>
#include <memory>
#include <vulkan/vulkan.h>
#include "mocktail/status.h"

namespace mocktail::vr {

// Runtime-owned eye-tracked density maps. The unused control color images are
// never submitted; ordinary output swapchains retain their transfer-copy path.
// Caller serializes access and completes GPU use before EndFrame/Destroy.
class XrFoveationMaps {
 public:
  XrFoveationMaps();
  ~XrFoveationMaps();
  XrFoveationMaps(const XrFoveationMaps&) = delete;
  XrFoveationMaps& operator=(const XrFoveationMaps&) = delete;
  Status Initialize(void* instance, std::uint64_t system, void* session,
                    VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc,
                    std::uint32_t width, std::uint32_t height, VkFormat format);
  bool BeginFrame();
  void EndFrame();
  void Destroy();
  bool ready() const;
  bool acquired() const;
  bool tracking() const;
  VkImageView view(int eye) const;
  std::uint32_t width() const;
  std::uint32_t height() const;
  bool center(int eye, float* x, float* y) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace mocktail::vr
