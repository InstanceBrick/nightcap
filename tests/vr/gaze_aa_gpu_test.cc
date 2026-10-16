// Offscreen GPU check: real raster/shader execution, no microphone or account.
#include "mocktail/vr/gaze_aa.h"
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
  const int mode=argc>1?std::atoi(argv[1]):1;
  const float radius=argc>2?std::atof(argv[2]):.22f;
  Check(mode>=1&&mode<=3,"AA mode");
  Check(radius>=.08f&&radius<=.35f,"gaze radius");
  const unsigned size=argc>3?std::atoi(argv[3]):256;
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
  for(int i=0;i<2;++i){VkImageCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;ci.extent={size,size,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(i?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_SAMPLED_BIT);Check(vkCreateImage(device,&ci,nullptr,&images[i])==VK_SUCCESS,"image");VkMemoryRequirements req;vkGetImageMemoryRequirements(device,images[i],&req);image_memory[i]=allocate(req,0);Check(vkBindImageMemory(device,images[i],image_memory[i],0)==VK_SUCCESS,"bind image");}
  VkBufferCreateInfo bi{};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=bytes*2;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buffer;Check(vkCreateBuffer(device,&bi,nullptr,&buffer)==VK_SUCCESS,"buffer");VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);auto buffer_memory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);vkBindBufferMemory(device,buffer,buffer_memory,0);
  unsigned char* mapped;Check(vkMapMemory(device,buffer_memory,0,bytes*2,0,reinterpret_cast<void**>(&mapped))==VK_SUCCESS,"map");
  for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){const unsigned char c=x*3>y*2+45?255:0;for(int k=0;k<3;++k)mapped[(y*size+x)*4+k]=c;mapped[(y*size+x)*4+3]=255;}
  VkCommandPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pi.queueFamilyIndex=family;
  VkCommandPool pool;Check(vkCreateCommandPool(device,&pi,nullptr,&pool)==VK_SUCCESS,"pool");VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer command;vkAllocateCommandBuffers(device,&ca,&command);
  mocktail::vr::GazeAa aa;
  const auto prepare=[&](int selected) {return aa.Prepare(device,CountProc,0,
      images[0],VK_FORMAT_R8G8B8A8_UNORM,images[1],VK_FORMAT_R8G8B8A8_UNORM,
      size,size,selected,&memory);};
  Check(prepare(mode),"prepare gaze AA");
  Check(pipeline_count==(mode==1?1u:3u),"only selected AA pipelines compiled");
  const auto initial_descriptors=descriptor_calls,initial_pipelines=pipeline_count;
  for(unsigned repeat=0;repeat<32;++repeat)Check(prepare(mode),"repeat stable prepare");
  Check(descriptor_calls==initial_descriptors,"unchanged descriptor bindings reused");
  Check(pipeline_count==initial_pipelines,"unchanged pipelines reused");
  aa.ForgetImage(images[0]);
  Check(prepare(mode),"rebind replacement source view");
  Check(descriptor_calls==initial_descriptors+(mode>=2?2u:1u),"replacement source refreshes all dependent descriptors");
  Check(pipeline_count==initial_pipelines,"replacement source reuses pipelines");
  const int alternate=mode==2?3:2;
  Check(prepare(alternate),"prepare alternate AA quality");
  Check(pipeline_count==initial_pipelines+3,"alternate pipelines built on demand");
  const auto switched_pipelines=pipeline_count;
  Check(prepare(mode),"return to initial AA quality");
  Check(pipeline_count==switched_pipelines,"switching back reuses pipelines");
  std::puts("AA_CACHE_CHECK stable descriptors, source replacement and lazy mode switches passed");
  VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;vkBeginCommandBuffer(command,&begin);
  VkImageMemoryBarrier barriers[2]{};
  for(int i=0;i<2;++i){auto& b=barriers[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.image=images[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers);
  VkBufferImageCopy upload{};upload.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};upload.imageExtent={size,size,1};vkCmdCopyBufferToImage(command,buffer,images[0],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&upload);
  barriers[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,barriers);
  VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource=upload.imageSubresource;copy.extent={size,size,1};vkCmdCopyImage(command,images[0],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images[1],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  // Invalid gaze must reject before recording any commands.
  Check(!aa.Record(command,0,NAN,0,mode),"invalid gaze rejected");
  Check(aa.Record(command,0,-.22f,0,mode,radius),"record gaze AA");
  barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,barriers+1);
  VkBufferImageCopy read=upload;read.bufferOffset=bytes;vkCmdCopyImageToBuffer(command,images[1],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&read);
  VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer;host.offset=bytes;host.size=bytes;
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
  vkEndCommandBuffer(command);VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)==VK_SUCCESS,"submit");Check(vkQueueWaitIdle(queue)==VK_SUCCESS,"complete");
  unsigned changed=0,outer=0,alpha=0;
  for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){unsigned index=(y*size+x)*4;bool diff=std::memcmp(mapped+index,mapped+bytes+index,4)!=0;float dx=(x+.5f)/size-.39f,dy=(y+.5f)/size-.5f;if(diff){++changed;if(dx*dx+dy*dy>radius*radius)++outer;}if(mapped[bytes+index+3]!=255)++alpha;}
  std::printf("gaze AA GPU mode %d: %u edge pixels smoothed, %u changes outside gaze disk, %u alpha changes\n",mode,changed,outer,alpha);
  std::uint64_t hash=1469598103934665603ull;
  for(unsigned i=0;i<bytes;++i){hash^=mapped[bytes+i];hash*=1099511628211ull;}
  std::printf("AA_PIXEL_HASH mode=%d radius=%.2f size=%u hash=%016llx\n",mode,radius,size,static_cast<unsigned long long>(hash));
  Check(changed>5,"edges visibly filtered");Check(!outer,"periphery unchanged");Check(!alpha,"alpha preserved");
  aa.ForgetImage(images[0]);aa.Reset();aa.Reset();
  vkDestroyCommandPool(device,pool,nullptr);vkUnmapMemory(device,buffer_memory);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,buffer_memory,nullptr);for(int i=0;i<2;++i){vkDestroyImage(device,images[i],nullptr);vkFreeMemory(device,image_memory[i],nullptr);}vkDestroyDevice(device,nullptr);
  if(messenger)reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);
  vkDestroyInstance(instance,nullptr);Check(!validation_errors,"zero Vulkan validation errors");
}
