#include "mocktail/vr/fsr3_transport.h"
#include <limits>

namespace mocktail::vr {
bool Fsr3Transport::Create(Resource& r,VkFormat format,std::uint32_t width,
    std::uint32_t height,VkImageUsageFlags usage,
    const VkPhysicalDeviceMemoryProperties& memory) {
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={width,height,1};
  info.mipLevels=1;info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;
  info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  if(Proc<PFN_vkCreateImage>("vkCreateImage")(device_,&info,nullptr,&r.image)!=VK_SUCCESS)return false;
  VkMemoryRequirements req{};Proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device_,r.image,&req);
  std::uint32_t type=std::numeric_limits<std::uint32_t>::max();
  for(std::uint32_t i=0;i<memory.memoryTypeCount;++i)
    if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
  if(type==std::numeric_limits<std::uint32_t>::max()){Destroy(r);return false;}
  VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=type;
  if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device_,&a,nullptr,&r.memory)!=VK_SUCCESS||
     Proc<PFN_vkBindImageMemory>("vkBindImageMemory")(device_,r.image,r.memory,0)!=VK_SUCCESS){Destroy(r);return false;}
  return true;
}
void Fsr3Transport::Destroy(Resource& r) {
  if(r.image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,r.image,nullptr);
  if(r.memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,r.memory,nullptr);r={};
}
void Fsr3Transport::Reset() {
  if(device_&&proc_)for(auto& e:eyes_){Destroy(e.color);Destroy(e.output);Destroy(e.encoded);}
  device_=VK_NULL_HANDLE;proc_=nullptr;source_format_=srgb_format_=VK_FORMAT_UNDEFINED;
  width_=height_=output_width_=output_height_=0;
}
bool Fsr3Transport::Prepare(VkDevice device,PFN_vkGetDeviceProcAddr proc,
    const VkPhysicalDeviceMemoryProperties& memory,VkFormat source,
    std::uint32_t width,std::uint32_t height,std::uint32_t out_width,std::uint32_t out_height) {
  if(!device||!proc||!width||!height||!out_width||!out_height)return false;
  if(device_==device&&source_format_==source&&width_==width&&height_==height&&
     output_width_==out_width&&output_height_==out_height&&eyes_[1].encoded.image)return true;
  if(device_)return false; // Resize is a separate, drained lifecycle operation.
  switch(source) {
    case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_R8G8B8A8_SRGB:srgb_format_=VK_FORMAT_R8G8B8A8_SRGB;break;
    case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B8G8R8A8_SRGB:srgb_format_=VK_FORMAT_B8G8R8A8_SRGB;break;
    default:return false;
  }
  device_=device;proc_=proc;source_format_=source;
  for(const char* name:{"vkCreateImage","vkDestroyImage","vkGetImageMemoryRequirements","vkAllocateMemory",
      "vkFreeMemory","vkBindImageMemory","vkCmdPipelineBarrier","vkCmdCopyImage","vkCmdBlitImage"})
    if(!proc_(device_,name)){Reset();return false;}
  width_=width;height_=height;output_width_=out_width;output_height_=out_height;
  for(auto& eye:eyes_) {
    if(!Create(eye.color,srgb_format_,width,height,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,memory)||
       !Create(eye.output,VK_FORMAT_R16G16B16A16_SFLOAT,out_width,out_height,
               VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,memory)||
       !Create(eye.encoded,srgb_format_,out_width,out_height,
               VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,memory)){Reset();return false;}
  }
  return true;
}
void Fsr3Transport::Barrier(VkCommandBuffer command,VkImage image,VkImageLayout from,
    VkImageLayout to,VkAccessFlags source,VkAccessFlags destination) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.image=image;b.oldLayout=from;b.newLayout=to;b.srcAccessMask=source;b.dstAccessMask=destination;
  b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
void Fsr3Transport::Begin(VkCommandBuffer command,int eye,VkImage source) {
  const auto& e=eyes_[eye];
  Barrier(command,e.color.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  VkImageCopy region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.extent={width_,height_,1};
  Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      e.color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
  Barrier(command,e.color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  Barrier(command,e.output.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
}
void Fsr3Transport::Finish(VkCommandBuffer command,int eye) {
  const auto& e=eyes_[eye];
  Barrier(command,e.output.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  Barrier(command,e.encoded.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
  region.srcOffsets[1]=region.dstOffsets[1]={static_cast<std::int32_t>(output_width_),static_cast<std::int32_t>(output_height_),1};
  // A blit to an sRGB format encodes linear FSR output. The following raw copy
  // into the native UNORM swapchain preserves those encoded bytes unchanged.
  Proc<PFN_vkCmdBlitImage>("vkCmdBlitImage")(command,e.output.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      e.encoded.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_NEAREST);
  Barrier(command,e.encoded.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
}
Fsr3Upscaler::Image Fsr3Transport::color(int eye)const{return {eyes_[eye].color.image,srgb_format_,width_,height_};}
Fsr3Upscaler::Image Fsr3Transport::output(int eye)const{return {eyes_[eye].output.image,VK_FORMAT_R16G16B16A16_SFLOAT,output_width_,output_height_};}
}
