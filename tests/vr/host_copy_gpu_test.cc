// Exercise the optimal R8G8 host-upload path and read it back through the GPU.
// Does not create an XR session or access the running game's resources.
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned errors=0;
static void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
static VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){
  if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++errors;std::fprintf(stderr,"VALIDATION ERROR %s\n",data->pMessage);}return VK_FALSE;
}
int main(){
  VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_3;
  const char* layer="VK_LAYER_KHRONOS_validation";const char* iext[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
  VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT validation{};validation.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;validation.enabledValidationFeatureCount=1;validation.pEnabledValidationFeatures=&sync;
  VkDebugUtilsMessengerCreateInfoEXT debug{};debug.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;debug.pfnUserCallback=Debug;debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;validation.pNext=&debug;
  VkInstanceCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ci.pApplicationInfo=&app;ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&layer;ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=iext;ci.pNext=&validation;
  VkInstance instance{};Check(vkCreateInstance(&ci,nullptr,&instance)==VK_SUCCESS,"instance");
  VkDebugUtilsMessengerEXT messenger{};Check(reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"))(instance,&debug,nullptr,&messenger)==VK_SUCCESS,"messenger");
  unsigned count=1;VkPhysicalDevice gpu{};Check(vkEnumeratePhysicalDevices(instance,&count,&gpu)==VK_SUCCESS&&count,"GPU");
  vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());
  unsigned family=0;while(family<count&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;Check(family<count,"graphics queue");
  float priority=1;VkDeviceQueueCreateInfo qi{};qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  VkPhysicalDeviceHostImageCopyFeaturesEXT feature{};feature.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT;feature.hostImageCopy=VK_TRUE;
  const char* dext[]={VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME,VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME,VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME};
  VkDeviceCreateInfo di{};di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;di.pNext=&feature;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.enabledExtensionCount=3;di.ppEnabledExtensionNames=dext;
  VkDevice device{};Check(vkCreateDevice(gpu,&di,nullptr,&device)==VK_SUCCESS,"host-copy device");VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
  VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
  auto allocate=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags){unsigned type=0;while(type<memory.memoryTypeCount&&(!(req.memoryTypeBits&(1u<<type))||(memory.memoryTypes[type].propertyFlags&flags)!=flags))++type;Check(type<memory.memoryTypeCount,"memory type");VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=req.size;ai.memoryTypeIndex=type;VkDeviceMemory result{};Check(vkAllocateMemory(device,&ai,nullptr,&result)==VK_SUCCESS,"allocate");return result;};
  constexpr unsigned size=81,bytes=size*size*2;
  VkImageCreateInfo image_ci{};image_ci.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image_ci.imageType=VK_IMAGE_TYPE_2D;image_ci.format=VK_FORMAT_R8G8_UNORM;image_ci.extent={size,size,1};image_ci.mipLevels=image_ci.arrayLayers=1;image_ci.samples=VK_SAMPLE_COUNT_1_BIT;image_ci.tiling=VK_IMAGE_TILING_OPTIMAL;image_ci.usage=VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImage image{};Check(vkCreateImage(device,&image_ci,nullptr,&image)==VK_SUCCESS,"optimal R8G8 image");VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,image,&req);auto imemory=allocate(req,0);Check(vkBindImageMemory(device,image,imemory,0)==VK_SUCCESS,"bind image");
  VkBufferCreateInfo buffer_ci{};buffer_ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;buffer_ci.size=bytes;buffer_ci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buffer{};Check(vkCreateBuffer(device,&buffer_ci,nullptr,&buffer)==VK_SUCCESS,"readback buffer");vkGetBufferMemoryRequirements(device,buffer,&req);auto bmemory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);Check(vkBindBufferMemory(device,buffer,bmemory,0)==VK_SUCCESS,"bind buffer");void* mapped=nullptr;Check(vkMapMemory(device,bmemory,0,bytes,0,&mapped)==VK_SUCCESS,"map readback");
  VkHostImageLayoutTransitionInfoEXT transition{};transition.sType=VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT;transition.image=image;transition.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;transition.newLayout=VK_IMAGE_LAYOUT_GENERAL;transition.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  Check(reinterpret_cast<PFN_vkTransitionImageLayoutEXT>(vkGetDeviceProcAddr(device,"vkTransitionImageLayoutEXT"))(device,1,&transition)==VK_SUCCESS,"host layout transition");
  VkCommandPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pi.queueFamilyIndex=family;
  VkCommandPool pool{};Check(vkCreateCommandPool(device,&pi,nullptr,&pool)==VK_SUCCESS,"pool");VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer command{};Check(vkAllocateCommandBuffers(device,&ca,&command)==VK_SUCCESS,"command buffer");
  for(unsigned iteration=0;iteration<3;++iteration){
    std::vector<unsigned char> pixels(bytes);for(unsigned i=0;i<bytes;++i)pixels[i]=(i*37+iteration*71)%256;
    VkMemoryToImageCopyEXT region{};region.sType=VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT;region.pHostPointer=pixels.data();region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={size,size,1};
    VkCopyMemoryToImageInfoEXT copy{};copy.sType=VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT;copy.dstImage=image;copy.dstImageLayout=VK_IMAGE_LAYOUT_GENERAL;copy.regionCount=1;copy.pRegions=&region;
    Check(reinterpret_cast<PFN_vkCopyMemoryToImageEXT>(vkGetDeviceProcAddr(device,"vkCopyMemoryToImageEXT"))(device,&copy)==VK_SUCCESS,"CPU upload");
    Check(vkResetCommandPool(device,pool,0)==VK_SUCCESS,"reset pool");VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;Check(vkBeginCommandBuffer(command,&begin)==VK_SUCCESS,"begin");
    VkBufferImageCopy read{};read.imageSubresource=region.imageSubresource;read.imageExtent=region.imageExtent;vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&read);
    VkBufferMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=buffer;barrier.size=bytes;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);Check(vkEndCommandBuffer(command)==VK_SUCCESS,"end");VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)==VK_SUCCESS,"GPU readback submit");Check(vkQueueWaitIdle(queue)==VK_SUCCESS,"readback complete");Check(std::memcmp(mapped,pixels.data(),bytes)==0,"exact R8G8 bytes after repeated CPU upload / GPU readback");
  }
  vkDestroyCommandPool(device,pool,nullptr);vkUnmapMemory(device,bmemory);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,bmemory,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,imemory,nullptr);vkDestroyDevice(device,nullptr);
  reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);vkDestroyInstance(instance,nullptr);
  Check(!errors,"zero validation errors");std::puts("Host image copy: 3 exact optimal R8G8 CPU uploads / GPU readbacks; zero validation errors");
}
