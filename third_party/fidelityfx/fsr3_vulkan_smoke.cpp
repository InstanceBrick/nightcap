#include "mocktail/vr/fsr3_upscaler.h"
#include <vulkan/vulkan.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
void Check(VkResult result,const char* message) {
  if(result!=VK_SUCCESS){std::fprintf(stderr,"%s: %d\n",message,result);std::exit(1);}
}
struct Image {
  VkDevice device{};VkImage image{};VkDeviceMemory memory{};VkFormat format{};
  std::uint32_t width{},height{};
  ~Image(){if(image)vkDestroyImage(device,image,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
  mocktail::vr::Fsr3Upscaler::Image Describe()const{return {image,format,width,height};}
};
std::uint32_t MemoryType(const VkPhysicalDeviceMemoryProperties& memory,std::uint32_t bits,VkMemoryPropertyFlags flags) {
  for(unsigned i=0;i<memory.memoryTypeCount;++i)
    if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
  std::fprintf(stderr,"No compatible Vulkan memory type\n");std::exit(1);
}
void Allocate(Image& image,VkDevice device,const VkPhysicalDeviceMemoryProperties& memory,
              VkFormat format,std::uint32_t width,std::uint32_t height,VkImageUsageFlags usage) {
  image.device=device;image.format=format;image.width=width;image.height=height;
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={width,height,1};
  info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
  info.usage=usage|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  Check(vkCreateImage(device,&info,nullptr,&image.image),"image");
  VkMemoryRequirements requirement{};vkGetImageMemoryRequirements(device,image.image,&requirement);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize=requirement.size;
  allocation.memoryTypeIndex=MemoryType(memory,requirement.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  Check(vkAllocateMemory(device,&allocation,nullptr,&image.memory),"image memory");
  Check(vkBindImageMemory(device,image.image,image.memory,0),"bind image");
}
void Barrier(VkCommandBuffer command,VkImage image,VkImageLayout from,VkImageLayout to,
             VkAccessFlags source,VkAccessFlags dest,VkPipelineStageFlags srcStage,VkPipelineStageFlags dstStage) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.image=image;barrier.oldLayout=from;barrier.newLayout=to;
  barrier.srcAccessMask=source;barrier.dstAccessMask=dest;
  barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  vkCmdPipelineBarrier(command,srcStage,dstStage,0,0,nullptr,0,nullptr,1,&barrier);
}
float Half(std::uint16_t bits) {
  const auto sign=bits>>15,exp=(bits>>10)&31,mantissa=bits&1023;
  if(exp==31)return mantissa?NAN:(sign?-INFINITY:INFINITY);
  const float value=exp?std::ldexp(1.f+float(mantissa)/1024.f,int(exp)-15):std::ldexp(float(mantissa),-24);
  return sign?-value:value;
}
}

int main(int argc,char** argv){
  const bool estimate_motion=argc>1 && std::strcmp(argv[1],"opticalflow")==0;
  VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  application.pApplicationName="FSR 3.1 Vulkan smoke";application.apiVersion=VK_API_VERSION_1_2;
  VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};instanceInfo.pApplicationInfo=&application;
  VkInstance instance{};Check(vkCreateInstance(&instanceInfo,nullptr,&instance),"instance");
  std::uint32_t physicalCount=0;Check(vkEnumeratePhysicalDevices(instance,&physicalCount,nullptr),"device count");
  std::vector<VkPhysicalDevice> physicals(physicalCount);
  Check(vkEnumeratePhysicalDevices(instance,&physicalCount,physicals.data()),"devices");
  if(physicals.empty())return 1;
  const VkPhysicalDevice physical=physicals.front();
  VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
  std::printf("GPU %s\n",properties.deviceName);
  VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
  std::uint32_t queueCount=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&queueCount,nullptr);
  std::vector<VkQueueFamilyProperties> queues(queueCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physical,&queueCount,queues.data());
  std::uint32_t family=0;
  while(family<queueCount && !(queues[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;
  if(family==queueCount)return 1;
  float priority=1.f;
  VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
  VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&features12;
  vkGetPhysicalDeviceFeatures2(physical,&features);
  VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  deviceInfo.pNext=&features12;deviceInfo.pEnabledFeatures=&features.features;
  deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
  VkDevice device{};Check(vkCreateDevice(physical,&deviceInfo,nullptr,&device),"logical device");
  VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
  int status=0;
  {
    std::array<Image,4> images;
    Allocate(images[0],device,memory,VK_FORMAT_R8G8B8A8_SRGB,256,256,VK_IMAGE_USAGE_SAMPLED_BIT);
    Allocate(images[1],device,memory,VK_FORMAT_R32_SFLOAT,256,256,VK_IMAGE_USAGE_SAMPLED_BIT);
    Allocate(images[2],device,memory,VK_FORMAT_R16G16_SFLOAT,256,256,VK_IMAGE_USAGE_SAMPLED_BIT);
    Allocate(images[3],device,memory,VK_FORMAT_R16G16B16A16_SFLOAT,384,384,VK_IMAGE_USAGE_STORAGE_BIT);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex=family;poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool{};Check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool),"command pool");
    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool=pool;cmdInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cmdInfo.commandBufferCount=1;
    VkCommandBuffer command{};Check(vkAllocateCommandBuffers(device,&cmdInfo,&command),"command buffer");
    const auto submit=[&]{
      Check(vkEndCommandBuffer(command),"end command");
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
      Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE),"submit");
      Check(vkQueueWaitIdle(queue),"GPU completion");
    };
    const auto begin=[&]{
      Check(vkResetCommandBuffer(command,0),"reset command");
      VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      Check(vkBeginCommandBuffer(command,&info),"begin command");
    };
    begin();
    for(unsigned i=0;i<4;++i){
      Barrier(command,images[i].image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkClearColorValue color{};
      if(i==0)color={{.25f,.5f,.75f,1.f}};
      if(i==1)color={{.5f,.5f,.5f,.5f}};
      VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      vkCmdClearColorImage(command,images[i].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
      Barrier(command,images[i].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          i==3?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT,i==3?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    submit();
    mocktail::vr::Fsr3Upscaler upscaler;
    mocktail::vr::Fsr3Upscaler::Configuration config{256,256,384,384};
    config.estimate_motion=estimate_motion;
    if(!upscaler.Configure(physical,device,vkGetDeviceProcAddr,memory,config))return 2;
    mocktail::vr::Fsr3Upscaler::Frame frame;
    frame.color=images[0].Describe();frame.depth=images[1].Describe();
    frame.motion_vectors=images[2].Describe();frame.output=images[3].Describe();
    VkBuffer staging{};VkDeviceMemory staging_memory{};void* staging_map=nullptr;
    if(estimate_motion) {
      frame.motion_vectors={};
      VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      buffer.size=256*256*4;buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      Check(vkCreateBuffer(device,&buffer,nullptr,&staging),"motion input staging");
      VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,staging,&requirements);
      VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocation.allocationSize=requirements.size;
      allocation.memoryTypeIndex=MemoryType(memory,requirements.memoryTypeBits,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      Check(vkAllocateMemory(device,&allocation,nullptr,&staging_memory),"staging memory");
      Check(vkBindBufferMemory(device,staging,staging_memory,0),"bind staging");
      Check(vkMapMemory(device,staging_memory,0,VK_WHOLE_SIZE,0,&staging_map),"map staging");
    }
    for(unsigned i=0;i<(estimate_motion?32u:4u);++i){
      frame.jitter=estimate_motion?mocktail::vr::Fsr3Upscaler::JitterOffset{}:
          mocktail::vr::Fsr3Upscaler::Jitter(i/2,256,384);
      // The input is spatially constant, so each jittered scene sample and
      // zero pixel velocity are exact for this stationary synthetic scene.
      begin();
      if(estimate_motion) {
        // Translate a deterministic nonrepeating luminance pattern two pixels
        // right per stereo frame. Backward motion must therefore be -2 on X.
        auto* pixels=static_cast<std::uint8_t*>(staging_map);
        for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
          const unsigned original=(x-2*(i/2))&255;
          std::uint32_t hash=original*73856093u ^ y*19349663u;
          hash^=hash>>13;hash*=1274126177u;hash^=hash>>16;
          const std::uint8_t luminance=32+(hash&191);
          const auto at=(y*256+x)*4;
          pixels[at]=pixels[at+1]=pixels[at+2]=luminance;pixels[at+3]=255;
        }
        Barrier(command,images[0].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.imageExtent={256,256,1};
        vkCmdCopyBufferToImage(command,staging,images[0].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        Barrier(command,images[0].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
      }
      if(!upscaler.Record(command,i&1,frame)){std::fprintf(stderr,"FSR dispatch failed %d\n",upscaler.last_error());return 3;}
      submit();
    }
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size=384*384*8;bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer{};Check(vkCreateBuffer(device,&bufferInfo,nullptr,&buffer),"readback buffer");
    VkMemoryRequirements requirement{};vkGetBufferMemoryRequirements(device,buffer,&requirement);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirement.size;
    allocation.memoryTypeIndex=MemoryType(memory,requirement.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory readback{};Check(vkAllocateMemory(device,&allocation,nullptr,&readback),"readback memory");
    Check(vkBindBufferMemory(device,buffer,readback,0),"bind readback");
    begin();
    Barrier(command,images[3].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={384,384,1};
    vkCmdCopyImageToBuffer(command,images[3].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
    submit();
    void* mapped=nullptr;Check(vkMapMemory(device,readback,0,VK_WHOLE_SIZE,0,&mapped),"readback map");
    const auto* pixels=static_cast<const std::uint16_t*>(mapped);
    double sum=0;unsigned nonfinite=0;
    for(unsigned i=0;i<384*384*4;++i){const float value=Half(pixels[i]);if(!std::isfinite(value))++nonfinite;else sum+=value;}
    const auto center=(192*384+192)*4;
    std::printf("FSR 3.1.4 stereo dispatch: nonfinite=%u, sum=%.5f, center=(%.5f,%.5f,%.5f,%.5f)\n",
        nonfinite,sum,Half(pixels[center]),Half(pixels[center+1]),Half(pixels[center+2]),Half(pixels[center+3]));
    if(nonfinite || sum<=0)status=4;
    vkUnmapMemory(device,readback);
    if(estimate_motion) {
      const auto motion=upscaler.estimated_motion(1);
      begin();
      Barrier(command,motion.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
      copy.imageExtent={256,256,1};
      vkCmdCopyImageToBuffer(command,motion.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
      submit();
      Check(vkMapMemory(device,readback,0,VK_WHOLE_SIZE,0,&mapped),"motion readback map");
      const auto* vectors=static_cast<const std::uint16_t*>(mapped);
      double mean_x=0,mean_y=0;unsigned count=0,correct=0;
      for(unsigned y=24;y<232;++y)for(unsigned x=24;x<232;++x) {
        const float mx=Half(vectors[(y*256+x)*2]),my=Half(vectors[(y*256+x)*2+1]);
        mean_x+=mx;mean_y+=my;++count;
        if(std::abs(mx+2.f)<.5f && std::abs(my)<.5f)++correct;
      }
      std::printf("AMD Optical Flow known +2 pixel translation: backward mean=(%.4f,%.4f), correct=%u/%u\n",
          mean_x/count,mean_y/count,correct,count);
      if(std::abs(mean_x/count+2.f)>.5f || std::abs(mean_y/count)>.5f || correct<count*3/4)status=5;
      vkUnmapMemory(device,readback);
      vkUnmapMemory(device,staging_memory);vkDestroyBuffer(device,staging,nullptr);vkFreeMemory(device,staging_memory,nullptr);
    }
    vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,readback,nullptr);
    upscaler.Reset();vkDestroyCommandPool(device,pool,nullptr);
  }
  vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
  return status;
}
