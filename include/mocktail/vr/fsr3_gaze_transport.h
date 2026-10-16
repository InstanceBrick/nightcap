#pragma once
#include "mocktail/vr/fsr3_upscaler.h"
#include <memory>

namespace mocktail::vr {

// A fixed-size temporal focus region with a cheap full-image periphery.
// The owner retires pending GPU users before reusing descriptors or resetting.
class Fsr3GazeTransport {
 public:
  struct BeginInfo {
    VkImage encoded_source{};  // TRANSFER_SRC_OPTIMAL; encoded UNORM or sRGB.
    Fsr3Upscaler::Image depth;  // Full-input size, SHADER_READ_ONLY_OPTIMAL.
    Fsr3Upscaler::Image motion; // Optional native clip-NDC delta, same layout/size.
    float gaze_x=.5f, gaze_y=.5f; // Normalized image UV, +Y down.
    bool reset=false;
  };
  struct Dimensions {
    std::uint32_t input_width{},input_height{},output_width{},output_height{};
  };
  Fsr3GazeTransport();
  ~Fsr3GazeTransport();
  Fsr3GazeTransport(const Fsr3GazeTransport&)=delete;
  Fsr3GazeTransport& operator=(const Fsr3GazeTransport&)=delete;
  bool Prepare(VkDevice,PFN_vkGetDeviceProcAddr,
      const VkPhysicalDeviceMemoryProperties&,VkFormat encoded_format,
      std::uint32_t full_input_width,std::uint32_t full_input_height,
      std::uint32_t full_output_width,std::uint32_t full_output_height,
      float sharp_radius);
  bool Begin(VkCommandBuffer,int eye,const BeginInfo&);
  void Finish(VkCommandBuffer,int eye);
  Dimensions dimensions()const;
  float vertical_fov_scale()const;
  bool reset_history(int eye)const;
  Fsr3Upscaler::Image color(int eye)const;
  Fsr3Upscaler::Image depth(int eye)const;
  Fsr3Upscaler::Image motion(int eye)const;
  Fsr3Upscaler::Image output(int eye)const;
  VkImage encoded(int eye)const;
  VkFormat encoded_format()const;
  void Reset();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
