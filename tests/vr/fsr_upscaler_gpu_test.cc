// Offscreen GPU check: real raster/shader execution, no microphone or account.
#include "mocktail/vr/gaze_aa.h"
#include "mocktail/vr/fsr_upscaler.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

void Check(bool value,const char* what) { if(!value){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);} }
int validation_errors=0;
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*) {
  if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++validation_errors;std::fprintf(stderr,"VALIDATION ERROR %s\n",data->pMessage);}
  return VK_FALSE;
}
static PFN_vkUpdateDescriptorSets raw_update=nullptr;
static PFN_vkCreateGraphicsPipelines raw_pipeline=nullptr;
static unsigned descriptor_calls=0,pipeline_count=0;
static VKAPI_ATTR void VKAPI_CALL CountUpdate(VkDevice device,unsigned count,
    const VkWriteDescriptorSet* writes,unsigned copies,const VkCopyDescriptorSet* copy) {
  ++descriptor_calls;raw_update(device,count,writes,copies,copy);
}
static VKAPI_ATTR VkResult VKAPI_CALL CountPipeline(VkDevice device,VkPipelineCache cache,
    unsigned count,const VkGraphicsPipelineCreateInfo* infos,
    const VkAllocationCallbacks* allocation,VkPipeline* pipelines) {
  pipeline_count+=count;return raw_pipeline(device,cache,count,infos,allocation,pipelines);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL CountProc(VkDevice device,const char* name) {
  auto proc=vkGetDeviceProcAddr(device,name);
  if(!std::strcmp(name,"vkUpdateDescriptorSets")) {
    raw_update=reinterpret_cast<PFN_vkUpdateDescriptorSets>(proc);
    return reinterpret_cast<PFN_vkVoidFunction>(CountUpdate);
  }
  if(!std::strcmp(name,"vkCreateGraphicsPipelines")) {
    raw_pipeline=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(proc);
    return reinterpret_cast<PFN_vkVoidFunction>(CountPipeline);
  }
  return proc;
}
int main(int argc,char** argv) {
  const int mode=2;
  const float radius=argc>3?std::atof(argv[3]):.15f;
  Check(mode>=1&&mode<=3,"AA mode");
  Check(radius>=.08f&&radius<=.35f,"gaze radius");
  const unsigned size=argc>1?std::atoi(argv[1]):320;
  Check(mocktail::vr::FsrRenderSize(2160,1)==1728,"80 percent render extent");
  Check(mocktail::vr::FsrRenderSize(2160,0)==2160,"native render extent");
  Check(mocktail::vr::FsrRenderSize(0,1)==0,"empty extent");
  Check(mocktail::vr::FsrRenderSize(2161,1)==1728,"even render extent");
  const mocktail::vr::FsrSettings fsr_settings{0,argc>6?std::atoi(argv[6]):80};
  const unsigned input=mocktail::vr::FsrRenderSize(size,1,fsr_settings);
  const float gaze_x=argc>4?std::atof(argv[4]):-.22f;
  const float gaze_y=argc>5?std::atof(argv[5]):0.f;
  const VkFormat format=static_cast<VkFormat>(argc>2?std::atoi(argv[2]):37);
  const unsigned input_bytes=input*input*4;
  const bool temporal=argc>7 && std::atoi(argv[7])!=0;
  const auto output_format=argc>8?static_cast<VkFormat>(std::atoi(argv[8])):format;
  Check(size>=256 && size<=4096,"image size");
  const unsigned bytes=size*size*4;
  VkInstanceCreateInfo ii{};ii.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  const bool validate=std::getenv("NIGHTCAP_AA_VALIDATE")!=nullptr;
  const char* layer="VK_LAYER_KHRONOS_validation";
  const char* extensions[]={VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME,VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
  VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT validation{};validation.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;validation.enabledValidationFeatureCount=1;validation.pEnabledValidationFeatures=&sync;
  VkDebugUtilsMessengerCreateInfoEXT debug{};debug.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;debug.pfnUserCallback=&DebugMessage;validation.pNext=&debug;
  if(validate){ii.enabledLayerCount=1;ii.ppEnabledLayerNames=&layer;ii.enabledExtensionCount=2;ii.ppEnabledExtensionNames=extensions;ii.pNext=&validation;}
  VkInstance instance;Check(vkCreateInstance(&ii,nullptr,&instance)==VK_SUCCESS,"instance");
  VkDebugUtilsMessengerEXT messenger{};
  if(validate)Check(reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"))(instance,&debug,nullptr,&messenger)==VK_SUCCESS,"validation callback");
  unsigned count=1;VkPhysicalDevice physical;Check(vkEnumeratePhysicalDevices(instance,&count,&physical)==VK_SUCCESS&&count,"physical device");
  vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
  unsigned family=0;while(family<count&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;Check(family<count,"graphics queue");
  float priority=1;VkDeviceQueueCreateInfo qi{};qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  VkDeviceCreateInfo di{};di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
  VkDevice device;Check(vkCreateDevice(physical,&di,nullptr,&device)==VK_SUCCESS,"device");VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
  VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);
  auto allocate=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags){unsigned type=0;while(type<memory.memoryTypeCount&&(!(req.memoryTypeBits&(1u<<type))||(memory.memoryTypes[type].propertyFlags&flags)!=flags))++type;Check(type<memory.memoryTypeCount,"memory type");VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=req.size;ai.memoryTypeIndex=type;VkDeviceMemory result;Check(vkAllocateMemory(device,&ai,nullptr,&result)==VK_SUCCESS,"memory");return result;};
  VkImage images[2];VkDeviceMemory image_memory[2];
  for(int i=0;i<2;++i){VkImageCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ci.imageType=VK_IMAGE_TYPE_2D;ci.format=i?output_format:format;ci.extent={i?size:input,i?size:input,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(i?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_SAMPLED_BIT);Check(vkCreateImage(device,&ci,nullptr,&images[i])==VK_SUCCESS,"image");VkMemoryRequirements req;vkGetImageMemoryRequirements(device,images[i],&req);image_memory[i]=allocate(req,0);Check(vkBindImageMemory(device,images[i],image_memory[i],0)==VK_SUCCESS,"bind image");}
  VkBufferCreateInfo bi{};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=input_bytes+bytes*4;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buffer;Check(vkCreateBuffer(device,&bi,nullptr,&buffer)==VK_SUCCESS,"buffer");VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);auto buffer_memory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);vkBindBufferMemory(device,buffer,buffer_memory,0);
  unsigned char* mapped;Check(vkMapMemory(device,buffer_memory,0,input_bytes+bytes*4,0,reinterpret_cast<void**>(&mapped))==VK_SUCCESS,"map");
  for(unsigned y=0;y<input;++y)for(unsigned x=0;x<input;++x){
    const unsigned i=(y*input+x)*4;
    const int edge=(x*3>y*2+input/7)?170:65;
    mapped[i]=static_cast<unsigned char>(edge+(x%19));
    mapped[i+1]=static_cast<unsigned char>(edge+(y%13));
    mapped[i+2]=static_cast<unsigned char>(edge+12);
    mapped[i+3]=static_cast<unsigned char>(80+(x*157/input));
  }
  VkCommandPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pi.queueFamilyIndex=family;
  VkCommandPool pool;Check(vkCreateCommandPool(device,&pi,nullptr,&pool)==VK_SUCCESS,"pool");VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer command;vkAllocateCommandBuffers(device,&ca,&command);

  mocktail::vr::GazeAa aa;
  mocktail::vr::FsrUpscaler fsr;
  const auto prepare=[&](int eye) {return fsr.Prepare(device,CountProc,memory,eye,
      images[0],format,images[1],output_format,input,input,size,size,temporal);};
  Check(prepare(0),"prepare FSR");
  Check(pipeline_count==(temporal?3u:2u),"selected FSR pipelines compiled once");
  const auto initial_descriptors=descriptor_calls;
  for(unsigned repeat=0;repeat<32;++repeat)Check(prepare(0),"stable prepare");
  Check(descriptor_calls==initial_descriptors,"stable descriptors reused");
  fsr.ForgetImage(images[0]);
  Check(prepare(0),"replacement source rebind");
  Check(descriptor_calls==initial_descriptors+1,"source descriptor refreshed");
  Check(prepare(1),"second eye prepare");
  Check(aa.Prepare(device,CountProc,0,fsr.image(0),fsr.intermediate_format(),images[1],output_format,
                   size,size,mode,&memory),"SMAA after EASU");
  VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
  VkQueryPoolCreateInfo query_info{};query_info.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;query_info.queryType=VK_QUERY_TYPE_TIMESTAMP;query_info.queryCount=5;
  VkQueryPool queries{};
  const bool timestamps=families[family].timestampValidBits>0;
  if(timestamps)Check(vkCreateQueryPool(device,&query_info,nullptr,&queries)==VK_SUCCESS,"timestamp pool");
  auto barrier=[&](VkImage image,VkImageLayout old_layout,VkImageLayout new_layout,VkAccessFlags source,VkAccessFlags destination,VkPipelineStageFlags before=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VkPipelineStageFlags after=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT){VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcAccessMask=source;b.dstAccessMask=destination;b.oldLayout=old_layout;b.newLayout=new_layout;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdPipelineBarrier(command,before,after,0,0,nullptr,0,nullptr,1,&b);};
  auto capture=[&](unsigned slot){
    barrier(images[1],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy read{};read.bufferOffset=input_bytes+bytes*slot;read.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};read.imageExtent={size,size,1};
    vkCmdCopyImageToBuffer(command,images[1],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&read);
    barrier(images[1],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
  };
  const std::vector<unsigned char> original(mapped,mapped+input_bytes);
  for(unsigned repeat=0;repeat<4;++repeat){
    if(temporal && repeat)for(unsigned i=0;i<input_bytes;i+=4)for(unsigned channel=0;channel<3;++channel)
      mapped[i+channel]=repeat==3 ? (channel==0?20:channel==1?220:80) :
        std::clamp(int(original[i+channel])+(repeat==1?3:-3),0,255);
    Check(vkResetCommandBuffer(command,0)==VK_SUCCESS,"reset commands");
    VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;vkBeginCommandBuffer(command,&begin);
    if(timestamps)vkCmdResetQueryPool(command,queries,0,5);
    barrier(images[0],repeat?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,repeat?VK_ACCESS_MEMORY_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
    barrier(images[1],repeat?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,repeat?VK_ACCESS_MEMORY_WRITE_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy upload{};upload.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};upload.imageExtent={input,input,1};
    vkCmdCopyBufferToImage(command,buffer,images[0],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&upload);
    barrier(images[0],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    if(timestamps)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,0);
    fsr.RecordUpscale(command,0,gaze_x,gaze_y,radius);
    if(timestamps)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,1);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource=upload.imageSubresource;copy.extent={size,size,1};
    vkCmdCopyImage(command,fsr.image(0),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images[1],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    capture(0);
    Check(aa.Record(command,0,gaze_x,gaze_y,mode,radius),"SMAA record");
    capture(1);
    if(timestamps)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,2);
    if(temporal)fsr.RecordTemporal(command,0,mocktail::vr::TemporalEyePose{},gaze_x,gaze_y,radius,1'000'000'000ULL+repeat*20'000'000ULL);
    if(timestamps)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,3);
    capture(2);
    fsr.RecordSharpen(command,0,gaze_x,gaze_y,radius);
    if(timestamps)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,4);
    capture(3);
    VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer;host.offset=input_bytes;host.size=bytes*4;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    vkEndCommandBuffer(command);VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)==VK_SUCCESS,"submit");Check(vkQueueWaitIdle(queue)==VK_SUCCESS,"complete");
    if(input==size)Check(std::memcmp(mapped,mapped+input_bytes,bytes)==0,
                         "native FSR input keeps identical pixel bytes");
    unsigned changed=0,outer=0,alpha=0,black=0;
    const auto* baseline=mapped+input_bytes+bytes;
    const auto* temporal_result=baseline+bytes;
    const auto* result=baseline+bytes*2;
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
      const unsigned index=(y*size+x)*4;
      const bool diff=std::memcmp(baseline+index,result+index,3)!=0;
      const float dx=(x+.5f)/size-(.5f+.5f*gaze_x),dy=(y+.5f)/size-(.5f-.5f*gaze_y);
      if(diff){++changed;if(dx*dx+dy*dy>radius*radius)++outer;}
      if(result[index+3]!=baseline[index+3])++alpha;
      if(result[index]+result[index+1]+result[index+2]<50)++black;
    }
    if(!temporal || repeat!=3)Check(changed>5,"RCAS sharpens gaze");Check(!outer,"RCAS leaves periphery unchanged");Check(!alpha,"RCAS preserves alpha");Check(!black,"EASU writes full eye including edges");
    if(temporal) {
      unsigned temporal_changes=0,temporal_outer=0,max_difference=0;
      for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
        const unsigned index=(y*size+x)*4;bool changed=false;
        for(unsigned k=0;k<3;++k){const auto delta=unsigned(std::abs(int(temporal_result[index+k])-int(baseline[index+k])));max_difference=std::max(max_difference,delta);changed|=delta!=0;}
        if(changed){++temporal_changes;const float dx=(x+.5f)/size-(.5f+.5f*gaze_x),dy=(y+.5f)/size-(.5f-.5f*gaze_y);if(dx*dx+dy*dy>radius*radius)++temporal_outer;}
        Check(temporal_result[index+3]==baseline[index+3],"temporal alpha preserved");
      }
      std::printf("TEMPORAL_GPU frame=%u changed=%u outside=%u max_delta=%u\n",repeat,temporal_changes,temporal_outer,max_difference);
      Check(!temporal_outer,"temporal periphery unchanged");
      if(repeat==0)Check(!temporal_changes,"first frame ignores uninitialized history");
      if(repeat==1 || repeat==2)Check(temporal_changes>5,"temporal history contributes to noisy frames");
      if(repeat==3)Check(max_difference<=1,"scene change rejects old colors");
    }
    if(timestamps){std::uint64_t times[5]{};Check(vkGetQueryPoolResults(device,queries,0,5,sizeof(times),times,sizeof(times[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT)==VK_SUCCESS,"timestamps");
      std::printf("FSR_GPU input=%u output=%u format=%d frame=%u EASU_ms=%.3f temporal_ms=%.3f RCAS_ms=%.3f sharpened=%u outer=%u alpha=%u\n",input,size,int(format),repeat,(times[1]-times[0])*properties.limits.timestampPeriod/1e6,(times[3]-times[2])*properties.limits.timestampPeriod/1e6,(times[4]-times[3])*properties.limits.timestampPeriod/1e6,changed,outer,alpha);}
  }
  aa.Reset();fsr.ForgetImage(images[0]);fsr.Reset();fsr.Reset();
  if(queries)vkDestroyQueryPool(device,queries,nullptr);
  vkDestroyCommandPool(device,pool,nullptr);vkUnmapMemory(device,buffer_memory);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,buffer_memory,nullptr);
  for(int i=0;i<2;++i){vkDestroyImage(device,images[i],nullptr);vkFreeMemory(device,image_memory[i],nullptr);}vkDestroyDevice(device,nullptr);
  if(messenger)reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);
  vkDestroyInstance(instance,nullptr);Check(!validation_errors,"zero Vulkan validation errors");
  std::puts("FSR GPU cache, source replacement, repeated frames, SMAA ordering, gaze boundary and alpha checks passed");
}
