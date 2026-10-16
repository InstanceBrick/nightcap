#include "mocktail/vr/fsr3_gaze_transport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <vector>

namespace {
void Check(VkResult code,const char* name) {
  if(code!=VK_SUCCESS){std::fprintf(stderr,"%s: %d\n",name,code);std::exit(1);}
}
std::uint32_t Memory(const VkPhysicalDeviceMemoryProperties& props,std::uint32_t bits,VkMemoryPropertyFlags flags) {
  for(unsigned i=0;i<props.memoryTypeCount;++i)
    if((bits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&flags)==flags)return i;
  std::fprintf(stderr,"No compatible GPU memory\n");std::exit(1);
}
struct Image {
  VkDevice device{};VkImage image{};VkDeviceMemory memory{};VkFormat format{};
  unsigned width{},height{};
  ~Image(){if(image)vkDestroyImage(device,image,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
  mocktail::vr::Fsr3Upscaler::Image Describe()const{return {image,format,width,height};}
};
void Allocate(Image& image,VkDevice device,const VkPhysicalDeviceMemoryProperties& memory,
              VkFormat format,unsigned width,unsigned height,VkImageUsageFlags usage) {
  image.device=device;image.width=width;image.height=height;image.format=format;
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={width,height,1};
  info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
  info.usage=usage|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  Check(vkCreateImage(device,&info,nullptr,&image.image),"image");
  VkMemoryRequirements requirement{};vkGetImageMemoryRequirements(device,image.image,&requirement);
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.allocationSize=requirement.size;
  allocate.memoryTypeIndex=Memory(memory,requirement.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  Check(vkAllocateMemory(device,&allocate,nullptr,&image.memory),"image allocation");
  Check(vkBindImageMemory(device,image.image,image.memory,0),"bind image");
}
void Barrier(VkCommandBuffer command,VkImage image,VkImageLayout from,VkImageLayout to,
              VkAccessFlags source,VkAccessFlags dest) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.image=image;barrier.oldLayout=from;barrier.newLayout=to;
  barrier.srcAccessMask=source;barrier.dstAccessMask=dest;
  barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0,0,nullptr,0,nullptr,1,&barrier);
}
struct Device {
  VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};
  unsigned family{};VkPhysicalDeviceMemoryProperties memory{};
  Device() {
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};application.apiVersion=VK_API_VERSION_1_2;
    application.pApplicationName="FSR 3.1 gaze region check";
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};create.pApplicationInfo=&application;
    Check(vkCreateInstance(&create,nullptr,&instance),"instance");
    unsigned count=0;Check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"device count");
    std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"devices");
    if(devices.empty())std::exit(1);physical=devices.front();
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;
    if(family==count)std::exit(1);
    const float priority=1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&features12;
    vkGetPhysicalDeviceFeatures2(physical,&features);
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.pNext=&features12;
    deviceInfo.pEnabledFeatures=&features.features;deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
    Check(vkCreateDevice(physical,&deviceInfo,nullptr,&device),"logical device");
    vkGetDeviceQueue(device,family,0,&queue);
  }
  ~Device(){if(device)vkDestroyDevice(device,nullptr);if(instance)vkDestroyInstance(instance,nullptr);}
};
}

int main(int argc,char** argv) {
  const auto option=[&](const char* name){
    for(int i=1;i<argc;++i)if(std::strcmp(argv[i],name)==0)return true;
    return false;
  };
  const bool large_grid=option("grid3072") || option("grid3072-odd");
  const bool large_size=large_grid || option("size3072") || option("size3072-odd");
  const bool grid=option("grid80") || option("grid90") || large_grid;
  const bool timing=argc>1 && std::isdigit(static_cast<unsigned char>(argv[1][0]));
  const unsigned input=large_size?2160:(timing?static_cast<unsigned>(std::atoi(argv[1])):(option("grid90")?576:512));
  const unsigned output=large_size?3072:(timing && argc>2?static_cast<unsigned>(std::atoi(argv[2])):(timing?2160:(grid?640:768)));
  const float radius=option("grid3072-odd") || option("size3072-odd")?.10f:(large_size?.08f:.15f);
  const bool estimate_motion=option("opticalflow");
  if(input<128 || output<input || output>4096)return 1;
  Device gpu;
  VkPhysicalDeviceProperties physical_properties{};
  vkGetPhysicalDeviceProperties(gpu.physical,&physical_properties);
  constexpr std::array<std::array<unsigned,4>,8> colors{{
    {0,0,0,255},{2,8,16,255},{19,43,83,255},{64,128,192,255},
    {127,127,127,255},{191,83,19,255},{239,247,251,255},{255,255,255,255}}};
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.queueFamilyIndex=gpu.family;poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VkCommandPool pool{};Check(vkCreateCommandPool(gpu.device,&poolInfo,nullptr,&pool),"command pool");
  VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  commandInfo.commandPool=pool;commandInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commandInfo.commandBufferCount=1;
  VkCommandBuffer command{};Check(vkAllocateCommandBuffers(gpu.device,&commandInfo,&command),"command");
  const auto begin=[&]{Check(vkResetCommandBuffer(command,0),"reset command");
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(vkBeginCommandBuffer(command,&info),"begin command");};
  const auto submit=[&]{Check(vkEndCommandBuffer(command),"end command");
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.commandBufferCount=1;info.pCommandBuffers=&command;
    Check(vkQueueSubmit(gpu.queue,1,&info,VK_NULL_HANDLE),"submit");Check(vkQueueWaitIdle(gpu.queue),"complete");};
  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bufferInfo.size=output*output*16;
  bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VkBuffer buffer{};Check(vkCreateBuffer(gpu.device,&bufferInfo,nullptr,&buffer),"readback");
  VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(gpu.device,buffer,&requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;
  allocation.memoryTypeIndex=Memory(gpu.memory,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory readback{};Check(vkAllocateMemory(gpu.device,&allocation,nullptr,&readback),"readback memory");
  Check(vkBindBufferMemory(gpu.device,buffer,readback,0),"readback bind");
  VkQueryPool queries{};
  if(timing) {
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=2;
    Check(vkCreateQueryPool(gpu.device,&info,nullptr,&queries),"timestamp queries");
  }
  std::vector<double> milliseconds;
  unsigned tests=0,failures=0,max_rgb_error=0,max_alpha_error=0;
  for(const VkFormat format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM}) {
    if(timing && format==VK_FORMAT_B8G8R8A8_UNORM)continue;
    Image source,depth,motion,destination;
    Allocate(source,gpu.device,gpu.memory,format,input,input,0);
    Allocate(depth,gpu.device,gpu.memory,VK_FORMAT_R32_SFLOAT,input,input,VK_IMAGE_USAGE_SAMPLED_BIT);
    Allocate(motion,gpu.device,gpu.memory,VK_FORMAT_R16G16_SFLOAT,input,input,VK_IMAGE_USAGE_SAMPLED_BIT);
    Allocate(destination,gpu.device,gpu.memory,format,output,output,0);
    mocktail::vr::Fsr3GazeTransport transport;
    mocktail::vr::Fsr3Upscaler upscaler;
    if(!transport.Prepare(gpu.device,vkGetDeviceProcAddr,gpu.memory,format,input,input,output,output,radius))return 2;
    const auto dimensions=transport.dimensions();
    mocktail::vr::Fsr3Upscaler::Configuration config{dimensions.input_width,dimensions.input_height,
        dimensions.output_width,dimensions.output_height};
    config.estimate_motion=estimate_motion;
    config.allow_fp16=option("fp16");
    config.inverted_depth=option("inverted");
    config.motion_vectors_jittered=option("jittered");
    if(!upscaler.Configure(gpu.physical,gpu.device,vkGetDeviceProcAddr,gpu.memory,config))return 2;
    std::printf("Gaze crop: input %ux%u -> ROI %ux%u -> output %ux%u, final %u, radius %.3f\n",
        input,input,dimensions.input_width,dimensions.input_height,dimensions.output_width,dimensions.output_height,output,radius);
    if(large_size && (std::uint64_t(dimensions.input_width)*output!=std::uint64_t(dimensions.output_width)*input ||
        dimensions.input_width%2==0 || dimensions.output_width%64!=0))return 10;
    std::int32_t previous_origin[2][2]{};
    bool previous_valid[2]{};
    begin();
    for(auto* image:{&depth,&motion}) {
      Barrier(command,image->image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
      VkClearColorValue color{};if(image==&depth)color={{.5f,.5f,.5f,.5f}};
      if(image==&motion && !timing)color={{.125f,-.0625f,0,0}};
      VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      vkCmdClearColorImage(command,image->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
      Barrier(command,image->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    }
    submit();
    bool first=true;
    for(unsigned trial=0;trial<(timing?40u:colors.size());++trial)for(int eye=0;eye<2;++eye) {
      const auto& reference=colors[timing?3:trial];
      if(grid) {
        void* mapped=nullptr;Check(vkMapMemory(gpu.device,readback,0,VK_WHOLE_SIZE,0,&mapped),"map pattern");
        auto* pixels=static_cast<unsigned char*>(mapped);
        for(unsigned y=0;y<input;++y)for(unsigned x=0;x<input;++x) {
          const bool stripe=((x/2+y/3)&1)!=0;
          const unsigned char rgba[4]={static_cast<unsigned char>(stripe?235:20),
              static_cast<unsigned char>(stripe?160:70),static_cast<unsigned char>(stripe?255:0),255};
          for(unsigned c=0;c<4;++c)pixels[(y*input+x)*4+c]=rgba[format==VK_FORMAT_B8G8R8A8_UNORM&&c<3?2-c:c];
        }
        vkUnmapMemory(gpu.device,readback);
      }
      begin();
      Barrier(command,source.image,first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,first?0:VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
      VkClearColorValue clear{};for(unsigned c=0;c<4;++c)clear.float32[c]=float(reference[c])/255;
      VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      if(grid) {
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer=buffer;barrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&barrier,0,nullptr);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={input,input,1};
        vkCmdCopyBufferToImage(command,buffer,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&barrier,0,nullptr);
      } else vkCmdClearColorImage(command,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
      Barrier(command,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
      if(timing) {
        vkCmdResetQueryPool(command,queries,0,2);
        vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0);
      }
      mocktail::vr::Fsr3GazeTransport::BeginInfo begin_info;
      begin_info.encoded_source=source.image;begin_info.depth=depth.Describe();
      if(!estimate_motion)begin_info.motion=motion.Describe();
      begin_info.gaze_x=timing?.5f:(trial==5?.02f:(.5f+.01f*trial));
      begin_info.gaze_y=large_grid?(trial==6?.98f:(.5f-.007f*trial)):.5f;
      if(!transport.Begin(command,eye,begin_info))return 6;
      const auto crop_origin=[&](float gaze,unsigned crop_input,unsigned crop_output){
        const double desired=double(gaze)*input-double(crop_input)*.5;
        const auto divisor=std::gcd(input,output);
        const unsigned input_step=input/divisor,output_step=output/divisor;
        const auto maximum=int(input-crop_input);
        const float guard_pixels=std::max(32.f,radius*.2f*output);
        if(std::uint64_t(crop_input)*output==std::uint64_t(crop_output)*input && output_step<=64u &&
           (output_step<=16u?output_step<=guard_pixels:output_step*.5f<=guard_pixels))
          return std::clamp(int(std::lround(desired/input_step)),0,maximum/int(input_step))*int(input_step);
        return std::clamp(int(std::lround(desired)),0,maximum);
      };
      const std::int32_t origin[2]={crop_origin(begin_info.gaze_x,dimensions.input_width,dimensions.output_width),
          crop_origin(begin_info.gaze_y,dimensions.input_height,dimensions.output_height)};
      const std::int32_t expected_motion[2]={previous_valid[eye]?origin[0]-previous_origin[eye][0]:0,
          previous_valid[eye]?origin[1]-previous_origin[eye][1]:0};
      const bool expected_reset=!previous_valid[eye] ||
          std::abs(expected_motion[0])>int(dimensions.input_width/4) ||
          std::abs(expected_motion[1])>int(dimensions.input_height/4);
      if(transport.reset_history(eye)!=expected_reset)return 7;
      previous_origin[eye][0]=origin[0];previous_origin[eye][1]=origin[1];previous_valid[eye]=true;
      mocktail::vr::Fsr3Upscaler::Frame frame;
      frame.color=transport.color(eye);frame.depth=transport.depth(eye);
      frame.motion_vectors=transport.motion(eye);
      frame.output=transport.output(eye);frame.reset=!timing || transport.reset_history(eye);
      frame.vertical_fov=2*std::atan(transport.vertical_fov_scale());
      frame.sharpening=!option("no-sharpening");
      frame.jitter=mocktail::vr::Fsr3Upscaler::Jitter(tests/2,dimensions.input_width,dimensions.output_width);
      if(!upscaler.Record(command,eye,frame))return 3;
      transport.Finish(command,eye);
      Barrier(command,destination.image,first?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,first?0:VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
      VkImageCopy region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
      region.extent={output,output,1};
      vkCmdCopyImage(command,transport.encoded(eye),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          destination.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
      Barrier(command,destination.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
      if(timing)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,1);
      VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={output,output,1};
      if(!timing)vkCmdCopyImageToBuffer(command,destination.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
      if(grid) {
        const auto focus=transport.output(eye);
        Barrier(command,focus.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy focus_copy{};focus_copy.bufferOffset=output*output*4;
        focus_copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        focus_copy.imageExtent={dimensions.output_width,dimensions.output_height,1};
        vkCmdCopyImageToBuffer(command,focus.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&focus_copy);
        Barrier(command,focus.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
      }
      if(!timing && !estimate_motion && !grid) {
        const auto cropped_motion=transport.motion(eye);
        Barrier(command,cropped_motion.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy motion_copy{};motion_copy.bufferOffset=output*output*4;
        motion_copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        motion_copy.imageExtent={dimensions.input_width,dimensions.input_height,1};
        vkCmdCopyImageToBuffer(command,cropped_motion.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&motion_copy);
        Barrier(command,cropped_motion.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
      }
      submit();first=false;++tests;
      if(timing) {
        std::uint64_t ticks[2]{};
        Check(vkGetQueryPoolResults(gpu.device,queries,0,2,sizeof(ticks),ticks,sizeof(ticks[0]),
            VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"GPU timestamps");
        if(trial>=8)milliseconds.push_back(double(ticks[1]-ticks[0])*physical_properties.limits.timestampPeriod/1e6);
        continue;
      }
      void* mapped=nullptr;Check(vkMapMemory(gpu.device,readback,0,VK_WHOLE_SIZE,0,&mapped),"map result");
      const auto* pixels=static_cast<const unsigned char*>(mapped);
      const bool bgra=format==VK_FORMAT_B8G8R8A8_UNORM;
      unsigned rgb_error=0,alpha_error=0;
      unsigned worst_pixel=0,worst_channel=0;
      if(grid) {
        const auto half=[](std::uint16_t value){
          const unsigned exponent=(value>>10)&31,mantissa=value&1023;
          const float magnitude=exponent?std::ldexp(1.f+float(mantissa)/1024.f,int(exponent)-15):std::ldexp(float(mantissa),-24);
          return value&32768?-magnitude:magnitude;
        };
        const auto encode=[](float value){
          value=std::clamp(value,0.f,1.f);
          return int(std::lround(255.f*(value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f)));
        };
        const auto* focus=reinterpret_cast<const std::uint16_t*>(pixels+output*output*4);
        for(unsigned sample=0;sample<dimensions.output_width*dimensions.output_height*4;++sample) {
          if((focus[sample]&0x7c00u)==0x7c00u) {
            std::fprintf(stderr,"Non-finite FSR output sample %u\n",sample);return 11;
          }
        }
        unsigned checked=0;
        for(unsigned y=0;y<output;++y)for(unsigned x=0;x<output;++x) {
          const float dx=(x+.5f)/output-begin_info.gaze_x,dy=(y+.5f)/output-begin_info.gaze_y;
          const float checked_radius=std::min(.1f,radius*.8f);
          if(dx*dx+dy*dy>checked_radius*checked_radius)continue;
          const double fx=((x+.5)*input/output-origin[0])*dimensions.output_width/dimensions.input_width-.5;
          const double fy=((y+.5)*input/output-origin[1])*dimensions.output_height/dimensions.input_height-.5;
          if(std::abs(fx-std::round(fx))>1e-8 || std::abs(fy-std::round(fy))>1e-8) {
            std::fprintf(stderr,"Fractional gaze texel centre %.6f,%.6f\n",fx,fy);return 8;
          }
          const int ix=int(std::lround(fx)),iy=int(std::lround(fy));
          if(ix<0||iy<0||ix>=int(dimensions.output_width)||iy>=int(dimensions.output_height))continue;
          ++checked;
          for(unsigned c=0;c<3;++c) {
            const unsigned stored=bgra?2-c:c;
            const auto error=unsigned(std::abs(int(pixels[(y*output+x)*4+stored])-encode(half(focus[(iy*dimensions.output_width+ix)*4+c]))));
            if(error>rgb_error){rgb_error=error;worst_pixel=y*output+x;worst_channel=c;}
          }
          alpha_error=std::max(alpha_error,unsigned(std::abs(int(pixels[(y*output+x)*4+3])-255)));
        }
        if(!checked)return 9;
      } else for(unsigned pixel=0;pixel<output*output;++pixel)for(unsigned c=0;c<4;++c) {
        const unsigned stored=bgra && c<3?2-c:c;
        const unsigned error=std::abs(int(pixels[pixel*4+stored])-int(reference[c]));
        if(c==3)alpha_error=std::max(alpha_error,error);
        else if(error>rgb_error){rgb_error=error;worst_pixel=pixel;worst_channel=c;}
      }
      const unsigned center=(output/2*output+output/2)*4;
      const std::array<unsigned,3> center_color{{pixels[center+(bgra?2:0)],pixels[center+1],pixels[center+(bgra?0:2)]}};
      if(!estimate_motion && !grid) {
        const auto* motion_values=reinterpret_cast<const std::uint16_t*>(pixels+output*output*4);
        const auto half=[](std::uint16_t value){
          const unsigned exponent=(value>>10)&31,mantissa=value&1023;
          const float magnitude=exponent?std::ldexp(1.f+float(mantissa)/1024.f,int(exponent)-15):std::ldexp(float(mantissa),-24);
          return value&32768?-magnitude:magnitude;
        };
        const auto at=(dimensions.input_height/2*dimensions.input_width+dimensions.input_width/2)*2;
        if(std::abs(half(motion_values[at])-(expected_motion[0]-input/16.f))>.01f ||
           std::abs(half(motion_values[at+1])-(expected_motion[1]-input/32.f))>.01f){
          std::fprintf(stderr,"Crop-origin motion mismatch: got %.4f,%.4f expected %d,%d\n",
              half(motion_values[at]),half(motion_values[at+1]),expected_motion[0],expected_motion[1]);
          ++failures;
        }
      }
      vkUnmapMemory(gpu.device,readback);
      max_rgb_error=std::max(max_rgb_error,rgb_error);max_alpha_error=std::max(max_alpha_error,alpha_error);
      if(rgb_error>2 || alpha_error) {
        ++failures;std::fprintf(stderr,"Transport mismatch format=%d eye=%d reference=(%u,%u,%u), center=(%u,%u,%u), RGB error=%u at(%u,%u,%u) alpha error=%u\n",
            format,eye,reference[0],reference[1],reference[2],center_color[0],center_color[1],center_color[2],
            rgb_error,worst_pixel%output,worst_pixel/output,worst_channel,alpha_error);
      }
    }
    upscaler.Reset();transport.Reset();
  }
  if(timing) {
    std::sort(milliseconds.begin(),milliseconds.end());
    std::printf("FSR 3.1.4 gaze ROI GPU cost %u->%u, opticalflow=%d fp16=%d, per-eye median=%.4f ms p95=%.4f ms, stereo estimated median=%.4f ms, samples=%zu\n",
        input,output,estimate_motion,option("fp16"),milliseconds[milliseconds.size()/2],milliseconds[milliseconds.size()*95/100],
        2*milliseconds[milliseconds.size()/2],milliseconds.size());
  } else {
    std::printf("FSR gaze ROI color/moving-origin/grid transport: %u cases, %u failures, max RGB byte error=%u, max opaque alpha error=%u\n",
        tests,failures,max_rgb_error,max_alpha_error);
  }
  if(queries)vkDestroyQueryPool(gpu.device,queries,nullptr);
  vkDestroyBuffer(gpu.device,buffer,nullptr);vkFreeMemory(gpu.device,readback,nullptr);vkDestroyCommandPool(gpu.device,pool,nullptr);
  return failures?4:0;
}
