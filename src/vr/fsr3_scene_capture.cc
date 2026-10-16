#include "mocktail/vr/fsr3_scene_capture.h"
#include <limits>

namespace mocktail::vr {
namespace {
VkImageAspectFlags BarrierAspects(VkFormat format) {
  switch(format) {
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    default:return VK_IMAGE_ASPECT_DEPTH_BIT;
  }
}
}
bool Fsr3SceneCapture::NeedsResize(const TemporalSceneInputs& in) const {
  if(in.eye<0||in.eye>1)return false;
  const auto& e=eyes_[in.eye];
  return e.depth && (e.width!=in.width||e.height!=in.height||e.format!=in.depth_format);
}
bool Fsr3SceneCapture::Create(Eye& e,const TemporalSceneInputs& in,
                             const VkPhysicalDeviceMemoryProperties& memory) {
  if(!Allocate(e.depth,e.memory,in.depth_format,in.width,in.height,memory))return false;
  e.width=in.width;e.height=in.height;e.format=in.depth_format;return true;
}
bool Fsr3SceneCapture::Allocate(VkImage& image,VkDeviceMemory& allocation_memory,
    VkFormat format,std::uint32_t width,std::uint32_t height,
    const VkPhysicalDeviceMemoryProperties& memory) {
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType=VK_IMAGE_TYPE_2D;info.format=format;
  info.extent={width,height,1};info.mipLevels=1;info.arrayLayers=1;
  info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
  info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
  info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  if(Proc<PFN_vkCreateImage>("vkCreateImage")(device_,&info,nullptr,&image)!=VK_SUCCESS)return false;
  VkMemoryRequirements requirements{};
  Proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device_,image,&requirements);
  std::uint32_t type=std::numeric_limits<std::uint32_t>::max();
  for(std::uint32_t i=0;i<memory.memoryTypeCount;++i)
    if((requirements.memoryTypeBits&(1u<<i)) &&
       (memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {type=i;break;}
  auto cleanup=[&] {
    if(image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,image,nullptr);
    if(allocation_memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,allocation_memory,nullptr);
    image=VK_NULL_HANDLE;allocation_memory=VK_NULL_HANDLE;
  };
  if(type==std::numeric_limits<std::uint32_t>::max()){cleanup();return false;}
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
  if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device_,&allocation,nullptr,&allocation_memory)!=VK_SUCCESS ||
     Proc<PFN_vkBindImageMemory>("vkBindImageMemory")(device_,image,allocation_memory,0)!=VK_SUCCESS) {
    cleanup();return false;
  }
  return true;
}
void Fsr3SceneCapture::Destroy(Eye& eye) {
  if(eye.depth)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,eye.depth,nullptr);
  if(eye.memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,eye.memory,nullptr);
  eye={};
}
void Fsr3SceneCapture::Reset() {
  if(device_&&proc_) {
    for(auto& eye:eyes_)Destroy(eye);
    for(auto& m:motion_) {
      if(m.image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,m.image,nullptr);
      if(m.memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,m.memory,nullptr);
      m={};
    }
  }
  pending_.clear();device_=VK_NULL_HANDLE;proc_=nullptr;
}
bool Fsr3SceneCapture::Record(VkDevice device,PFN_vkGetDeviceProcAddr proc,
    const VkPhysicalDeviceMemoryProperties& memory,VkCommandBuffer command,
    const TemporalSceneInputs& in,std::uint64_t frame) {
  if(!device||!proc||!command||!frame||in.eye<0||in.eye>1||!in.depth||
     !in.width||!in.height||!IsTemporalDepthFormat(in.depth_format)||
     in.depth_layout==VK_IMAGE_LAYOUT_UNDEFINED||in.depth_layout==VK_IMAGE_LAYOUT_PREINITIALIZED||
     !(in.depth_range.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)||
     in.depth_range.levelCount!=1||in.depth_range.layerCount!=1)return false;
  if(device_ && device_!=device)return false;
  if(NeedsResize(in))return false; // Caller must drain all uses and Reset first.
  device_=device;proc_=proc;
  for(const char* name:{"vkCreateImage","vkDestroyImage","vkGetImageMemoryRequirements",
       "vkAllocateMemory","vkFreeMemory","vkBindImageMemory","vkCmdPipelineBarrier","vkCmdCopyImage"})
    if(!proc_(device_,name))return false;
  auto& eye=eyes_[in.eye];
  if(!eye.depth&&!Create(eye,in,memory))return false;
  const auto barrier=Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
  std::array<VkImageMemoryBarrier,2> before{};
  for(auto& b:before) {
    b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  }
  before[0].image=in.depth;before[0].oldLayout=in.depth_layout;
  before[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  before[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;
  before[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  before[0].subresourceRange=in.depth_range;
  before[0].subresourceRange.aspectMask=BarrierAspects(in.depth_format);
  before[1].image=eye.depth;
  // This snapshot has no preserved content. UNDEFINED also handles a guest
  // command that was recorded and discarded before ever being submitted.
  before[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
  before[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  before[1].srcAccessMask=0;
  before[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  before[1].subresourceRange={BarrierAspects(in.depth_format),0,1,0,1};
  barrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,before.data());
  VkImageCopy region{};
  region.srcSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,in.depth_range.baseMipLevel,in.depth_range.baseArrayLayer,1};
  region.dstSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};region.extent={in.width,in.height,1};
  Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,in.depth,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      eye.depth,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
  auto after=before;
  after[0].oldLayout=before[0].newLayout;after[0].newLayout=in.depth_layout;
  after[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  after[0].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  after[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  after[1].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  after[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  after[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  barrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,after.data());
  eye.initialized=true;pending_[command].depth[in.eye]=frame;
  pending_[command].submitted=false;return true;
}
bool Fsr3SceneCapture::RecordMotion(VkDevice device,PFN_vkGetDeviceProcAddr proc,
    const VkPhysicalDeviceMemoryProperties& memory,VkCommandBuffer command,
    const TemporalMotionInputs& in,std::uint64_t frame) {
  if(!device||!proc||!command||!frame||in.eye<0||in.eye>1||!in.image||
     !in.width||!in.height||!IsTemporalMotionFormat(in.format)||
     in.layout==VK_IMAGE_LAYOUT_UNDEFINED||in.layout==VK_IMAGE_LAYOUT_PREINITIALIZED||
     in.range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||in.range.levelCount!=1||in.range.layerCount!=1||
     in.previous_uv_scale_x!=-.5f||in.previous_uv_scale_y!=.5f)return false;
  if(device_&&device_!=device)return false;
  device_=device;proc_=proc;
  for(const char* name:{"vkCreateImage","vkDestroyImage","vkGetImageMemoryRequirements","vkAllocateMemory",
      "vkFreeMemory","vkBindImageMemory","vkCmdPipelineBarrier","vkCmdCopyImage"})if(!proc_(device_,name))return false;
  auto& m=motion_[in.eye];
  if(m.image&&(m.width!=in.width||m.height!=in.height||m.format!=in.format))return false;
  if(!m.image) {
    if(!Allocate(m.image,m.memory,in.format,in.width,in.height,memory))return false;
    m.width=in.width;m.height=in.height;m.format=in.format;
    m.previous_uv_scale_x=in.previous_uv_scale_x;m.previous_uv_scale_y=in.previous_uv_scale_y;
  }
  std::array<VkImageMemoryBarrier,2> before{};
  for(auto& b:before){b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;}
  before[0].image=in.image;before[0].oldLayout=in.layout;before[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  before[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;before[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  before[0].subresourceRange=in.range;
  before[1].image=m.image;before[1].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;before[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  before[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;before[1].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  const auto barrier=Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
  barrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,before.data());
  VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,in.range.baseMipLevel,in.range.baseArrayLayer,1};
  copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={in.width,in.height,1};
  Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,in.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,m.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  auto after=before;after[0].oldLayout=before[0].newLayout;after[0].newLayout=in.layout;
  after[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;after[0].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  after[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;after[1].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  after[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;after[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  barrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,2,after.data());
  pending_[command].motion[in.eye]=frame;pending_[command].submitted=false;return true;
}
void Fsr3SceneCapture::Submitted(std::uint32_t count,const VkCommandBuffer* buffers) {
  if(!buffers)return;
  for(std::uint32_t i=0;i<count;++i) {
    const auto p=pending_.find(buffers[i]);if(p==pending_.end())continue;
    p->second.submitted=true;
    for(int eye=0;eye<2;++eye) {
      if(p->second.depth[eye])eyes_[eye].submitted_frame=p->second.depth[eye];
      if(p->second.motion[eye])motion_[eye].submitted_frame=p->second.motion[eye];
    }
  }
}
void Fsr3SceneCapture::Discard(VkCommandBuffer command) {pending_.erase(command);}
bool Fsr3SceneCapture::HasUnsubmitted() const {
  for(const auto& entry:pending_)if(!entry.second.submitted)return true;
  return false;
}
const Fsr3SceneCapture::Eye* Fsr3SceneCapture::Get(int eye,std::uint64_t frame,
    std::uint32_t width,std::uint32_t height) const {
  if(!frame||eye<0||eye>1)return nullptr;const auto& e=eyes_[eye];
  return e.depth&&e.submitted_frame==frame&&e.width==width&&e.height==height?&e:nullptr;
}
const Fsr3SceneCapture::Motion* Fsr3SceneCapture::GetMotion(int eye,std::uint64_t frame,
    std::uint32_t width,std::uint32_t height) const {
  if(!frame||eye<0||eye>1)return nullptr;const auto& m=motion_[eye];
  return m.image&&m.submitted_frame==frame&&m.width==width&&m.height==height?&m:nullptr;
}
}
