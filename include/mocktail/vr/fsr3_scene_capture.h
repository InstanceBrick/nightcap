#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include "mocktail/vr/fsr3_scene_inputs.h"

namespace mocktail::vr {
// Owns depth snapshots, recorded before the guest reuses its scene depth.
// All methods are serialized by the backend and use its single graphics queue.
class Fsr3SceneCapture {
 public:
  struct Eye {
    VkImage depth{};
    VkDeviceMemory memory{};
    VkFormat format{};
    std::uint32_t width{},height{};
    std::uint64_t submitted_frame{};
    bool initialized{};
  };
  struct Motion {
    VkImage image{};VkDeviceMemory memory{};VkFormat format{};
    std::uint32_t width{},height{};std::uint64_t submitted_frame{};
    float previous_uv_scale_x=-.5f,previous_uv_scale_y=.5f;
  };
  ~Fsr3SceneCapture() { Reset(); }
  bool NeedsResize(const TemporalSceneInputs&) const;
  bool Record(VkDevice, PFN_vkGetDeviceProcAddr,
              const VkPhysicalDeviceMemoryProperties&, VkCommandBuffer,
              const TemporalSceneInputs&, std::uint64_t frame);
  bool RecordMotion(VkDevice, PFN_vkGetDeviceProcAddr,
                    const VkPhysicalDeviceMemoryProperties&, VkCommandBuffer,
                    const TemporalMotionInputs&, std::uint64_t frame);
  void Submitted(std::uint32_t,const VkCommandBuffer*);
  void Discard(VkCommandBuffer);
  bool HasUnsubmitted() const;
  const Eye* Get(int eye,std::uint64_t frame,std::uint32_t width,
                 std::uint32_t height) const;
  const Motion* GetMotion(int eye,std::uint64_t frame,std::uint32_t width,
                          std::uint32_t height) const;
  void Reset();
 private:
  template<class T> T Proc(const char* name) const {
    return reinterpret_cast<T>(proc_(device_,name));
  }
  bool Create(Eye&,const TemporalSceneInputs&,
              const VkPhysicalDeviceMemoryProperties&);
  bool Allocate(VkImage&,VkDeviceMemory&,VkFormat,std::uint32_t,std::uint32_t,
                const VkPhysicalDeviceMemoryProperties&);
  void Destroy(Eye&);
  VkDevice device_{};
  PFN_vkGetDeviceProcAddr proc_{};
  std::array<Eye,2> eyes_{};
  std::array<Motion,2> motion_{};
  struct Pending {std::array<std::uint64_t,2> depth{},motion{};bool submitted=false;};
  std::unordered_map<VkCommandBuffer,Pending> pending_;
};
}
