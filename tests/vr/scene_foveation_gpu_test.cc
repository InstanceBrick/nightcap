// Real colour + depth raster test. A full-density reference is compared with
// each gaze map: all pixels inside the focus must remain byte-for-byte exact,
// while the high-frequency periphery must demonstrate actual coarse shading.
// No XR session, client data, microphone, or account access.
#include "mocktail/vr/foveation_focus.h"
#include "gaze_aa_spv.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static unsigned errors=0;
static void Check(bool ok,const char* message) {
  if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}
}
static VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*) {
  if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ++errors;std::fprintf(stderr,"VALIDATION ERROR %s\n",data->pMessage);
  }
  return VK_FALSE;
}
int main(int argc,char** argv) {
  Check(argc==2,"fragment SPIR-V path argument");
  std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
  Check(bool(file),"open fragment SPIR-V");const auto length=file.tellg();
  Check(length>0 && length%4==0,"SPIR-V length");
  std::vector<std::uint32_t> fragment(std::size_t(length)/4);
  file.seekg(0);file.read(reinterpret_cast<char*>(fragment.data()),length);
  VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_3;
  const char* layer="VK_LAYER_KHRONOS_validation";
  const char* extensions[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
  VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkDebugUtilsMessengerCreateInfoEXT debug{};debug.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  debug.pfnUserCallback=Debug;debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  VkValidationFeaturesEXT validation{};validation.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
  validation.pNext=&debug;validation.enabledValidationFeatureCount=1;validation.pEnabledValidationFeatures=&sync;
  VkInstanceCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ci.pNext=&validation;
  ci.pApplicationInfo=&app;ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&layer;ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=extensions;
  VkInstance instance{};Check(vkCreateInstance(&ci,nullptr,&instance)==VK_SUCCESS,"instance");
  VkDebugUtilsMessengerEXT messenger{};
  Check(reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"))(instance,&debug,nullptr,&messenger)==VK_SUCCESS,"debug callback");
  unsigned count=1;VkPhysicalDevice physical{};
  Check(vkEnumeratePhysicalDevices(instance,&count,&physical)==VK_SUCCESS && count,"physical device");
  VkPhysicalDeviceFragmentDensityMapPropertiesEXT density_properties{};
  density_properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT;
  VkPhysicalDeviceProperties2 properties{};properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;properties.pNext=&density_properties;
  vkGetPhysicalDeviceProperties2(physical,&properties);
  VkPhysicalDeviceFragmentDensityMapFeaturesEXT density{};
  density.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT;
  VkPhysicalDeviceHostImageCopyFeaturesEXT host{};host.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT;host.pNext=&density;
  VkPhysicalDeviceFeatures2 features{};features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features.pNext=&host;
  vkGetPhysicalDeviceFeatures2(physical,&features);
  Check(density.fragmentDensityMap && density.fragmentDensityMapNonSubsampledImages && host.hostImageCopy,"required FDM and host-copy features");
  density.fragmentDensityMapDynamic=VK_FALSE;
  vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
  std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
  unsigned family=0;while(family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
  Check(family<count,"graphics queue");float priority=1;
  VkDeviceQueueCreateInfo qi{};qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
  const char* device_extensions[]={VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME,VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME,VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME,VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME};
  VkDeviceCreateInfo di{};di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;di.pNext=&host;
  di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.enabledExtensionCount=4;di.ppEnabledExtensionNames=device_extensions;
  VkDevice device{};Check(vkCreateDevice(physical,&di,nullptr,&device)==VK_SUCCESS,"FDM device");
  VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
  VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
  auto allocate=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags) {
    unsigned type=0;while(type<memory.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || (memory.memoryTypes[type].propertyFlags&flags)!=flags))++type;
    Check(type<memory.memoryTypeCount,"memory type");VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize=req.size;ai.memoryTypeIndex=type;VkDeviceMemory result{};
    Check(vkAllocateMemory(device,&ai,nullptr,&result)==VK_SUCCESS,"memory");return result;
  };
  constexpr unsigned size=2592,bytes=size*size*4;
  // A one-texel map satisfies the driver's maximum texel extent but cannot
  // describe a focus disk. Use the same 32-pixel density cells as the app.
  const unsigned map_width=std::max((size+31)/32,(size+density_properties.maxFragmentDensityTexelSize.width-1)/density_properties.maxFragmentDensityTexelSize.width);
  const unsigned map_height=std::max((size+31)/32,(size+density_properties.maxFragmentDensityTexelSize.height-1)/density_properties.maxFragmentDensityTexelSize.height);
  std::printf("GPU %s density texel range %ux%u..%ux%u, test map %ux%u\n",properties.properties.deviceName,density_properties.minFragmentDensityTexelSize.width,density_properties.minFragmentDensityTexelSize.height,density_properties.maxFragmentDensityTexelSize.width,density_properties.maxFragmentDensityTexelSize.height,map_width,map_height);
  VkFormatProperties fp{};vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R16G16_SFLOAT,&fp);
  std::printf("R16G16 density support linear=%d optimal=%d\n",bool(fp.linearTilingFeatures&VK_FORMAT_FEATURE_FRAGMENT_DENSITY_MAP_BIT_EXT),bool(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_FRAGMENT_DENSITY_MAP_BIT_EXT));
  VkImage images[3]{};VkImageView views[3]{};VkDeviceMemory image_memory[3]{};
  const VkFormat formats[]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_D32_SFLOAT,VK_FORMAT_R16G16_SFLOAT};
  for(unsigned i=0;i<3;++i) {
    VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.imageType=VK_IMAGE_TYPE_2D;
    image.format=formats[i];image.extent={i==2?map_width:size,i==2?map_height:size,1};
    image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=i==0?(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT):i==1?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:(VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT|VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT);
    Check(vkCreateImage(device,&image,nullptr,&images[i])==VK_SUCCESS,"image");
    VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,images[i],&req);image_memory[i]=allocate(req,0);
    Check(vkBindImageMemory(device,images[i],image_memory[i],0)==VK_SUCCESS,"bind image");
    VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=images[i];
    view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=formats[i];view.subresourceRange={i==1?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    Check(vkCreateImageView(device,&view,nullptr,&views[i])==VK_SUCCESS,"image view");
  }
  VkHostImageLayoutTransitionInfoEXT transition{};transition.sType=VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT;
  transition.image=images[2];transition.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;transition.newLayout=VK_IMAGE_LAYOUT_GENERAL;
  transition.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  Check(reinterpret_cast<PFN_vkTransitionImageLayoutEXT>(vkGetDeviceProcAddr(device,"vkTransitionImageLayoutEXT"))(device,1,&transition)==VK_SUCCESS,"host transition");
  VkAttachmentDescription attachments[3]{};
  for(unsigned i=0;i<3;++i) {
    auto& a=attachments[i];a.format=formats[i];a.samples=VK_SAMPLE_COUNT_1_BIT;
    a.loadOp=i==2?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp=i==0?VK_ATTACHMENT_STORE_OP_STORE:VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout=i==2?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout=i==0?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:i==1?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
  }
  VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;subpass.pDepthStencilAttachment=&depth;
  VkRenderPassFragmentDensityMapCreateInfoEXT map{};map.sType=VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT;map.fragmentDensityMapAttachment={2,VK_IMAGE_LAYOUT_GENERAL};
  VkSubpassDependency dependency{};dependency.srcSubpass=0;dependency.dstSubpass=VK_SUBPASS_EXTERNAL;
  dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
  dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  VkRenderPassCreateInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.pNext=&map;
  rp.attachmentCount=3;rp.pAttachments=attachments;rp.subpassCount=1;rp.pSubpasses=&subpass;rp.dependencyCount=1;rp.pDependencies=&dependency;
  VkRenderPass pass{};Check(vkCreateRenderPass(device,&rp,nullptr,&pass)==VK_SUCCESS,"render pass");
  VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=pass;
  fb.attachmentCount=3;fb.pAttachments=views;fb.width=fb.height=size;fb.layers=1;
  VkFramebuffer framebuffer{};Check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer)==VK_SUCCESS,"framebuffer");
  VkShaderModule shaders[2]{};VkShaderModuleCreateInfo sm{};sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  sm.pCode=mocktail::vr::kGazeAaVertex;sm.codeSize=sizeof(mocktail::vr::kGazeAaVertex);
  Check(vkCreateShaderModule(device,&sm,nullptr,&shaders[0])==VK_SUCCESS,"vertex shader");
  sm.pCode=fragment.data();sm.codeSize=fragment.size()*4;Check(vkCreateShaderModule(device,&sm,nullptr,&shaders[1])==VK_SUCCESS,"fragment shader");
  VkPipelineShaderStageCreateInfo stages[2]{};
  for(unsigned i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=shaders[i];stages[i].pName="main";}
  VkPipelineLayoutCreateInfo li{};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;VkPipelineLayout layout{};
  Check(vkCreatePipelineLayout(device,&li,nullptr,&layout)==VK_SUCCESS,"pipeline layout");
  VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0,0,float(size),float(size),0,1};VkRect2D scissor{{0,0},{size,size}};
  VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
  VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{};ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;ds.depthTestEnable=ds.depthWriteEnable=VK_TRUE;ds.depthCompareOp=VK_COMPARE_OP_LESS;
  VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
  VkPipelineColorBlendStateCreateInfo cb{};cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;cb.attachmentCount=1;cb.pAttachments=&blend;
  VkGraphicsPipelineCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;pi.stageCount=2;pi.pStages=stages;
  pi.pVertexInputState=&vi;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;
  pi.pDepthStencilState=&ds;pi.pColorBlendState=&cb;pi.layout=layout;pi.renderPass=pass;
  VkPipeline pipeline{};Check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline)==VK_SUCCESS,"graphics pipeline");
  VkBufferCreateInfo bi{};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buffer{};Check(vkCreateBuffer(device,&bi,nullptr,&buffer)==VK_SUCCESS,"readback buffer");
  VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffer,&req);
  auto buffer_memory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  Check(vkBindBufferMemory(device,buffer,buffer_memory,0)==VK_SUCCESS,"bind readback");void* mapped=nullptr;
  Check(vkMapMemory(device,buffer_memory,0,bytes,0,&mapped)==VK_SUCCESS,"map readback");
  VkCommandPoolCreateInfo cp{};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.queueFamilyIndex=family;
  VkCommandPool pool{};Check(vkCreateCommandPool(device,&cp,nullptr,&pool)==VK_SUCCESS,"command pool");
  VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
  VkCommandBuffer command{};Check(vkAllocateCommandBuffers(device,&ca,&command)==VK_SUCCESS,"command buffer");
  std::vector<unsigned char> reference;
  const float gazes[][2]={{.4f,.3f},{0,0},{-.8f,.7f},{1,-1}};
  for(int level=0;level<=3;++level)for(float radius:{.08f,.22f,.35f})for(const auto& gaze:gazes) {
    if(level==0 && !reference.empty())continue;
    const float gaze_x=gaze[0],gaze_y=gaze[1];
    const auto pixels=mocktail::vr::internal::GazeDensityMap(map_width,map_height,gaze_x,gaze_y,level,radius,
        .5f*std::min(size,density_properties.maxFragmentDensityTexelSize.width)/size,
        .5f*std::min(size,density_properties.maxFragmentDensityTexelSize.height)/size);
    const auto densities=mocktail::vr::internal::HalfDensityMap(pixels);
    VkMemoryToImageCopyEXT region{};region.sType=VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT;region.pHostPointer=densities.data();
    region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={map_width,map_height,1};
    VkCopyMemoryToImageInfoEXT copy{};copy.sType=VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT;copy.dstImage=images[2];copy.dstImageLayout=VK_IMAGE_LAYOUT_GENERAL;copy.regionCount=1;copy.pRegions=&region;
    Check(reinterpret_cast<PFN_vkCopyMemoryToImageEXT>(vkGetDeviceProcAddr(device,"vkCopyMemoryToImageEXT"))(device,&copy)==VK_SUCCESS,"gaze upload");
    Check(vkResetCommandPool(device,pool,0)==VK_SUCCESS,"reset command pool");
    VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Check(vkBeginCommandBuffer(command,&begin)==VK_SUCCESS,"begin command buffer");
    VkClearValue clears[2]{};clears[1].depthStencil={1.f,0};
    VkRenderPassBeginInfo render{};render.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render.renderPass=pass;render.framebuffer=framebuffer;render.renderArea=scissor;render.clearValueCount=2;render.pClearValues=clears;
    vkCmdBeginRenderPass(command,&render,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
    VkBufferImageCopy read{};read.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};read.imageExtent={size,size,1};
    vkCmdCopyImageToBuffer(command,images[0],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&read);
    VkBufferMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=buffer;barrier.size=bytes;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    Check(vkEndCommandBuffer(command)==VK_SUCCESS,"end commands");
    VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)==VK_SUCCESS,"submit");Check(vkQueueWaitIdle(queue)==VK_SUCCESS,"complete");
    auto* output=static_cast<unsigned char*>(mapped);
    if(level==0) {reference.assign(output,output+bytes);continue;}
    unsigned focus_changes=0,peripheral_changes=0,alpha_changes=0;
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
      const auto index=(y*size+x)*4;const bool changed=std::memcmp(output+index,reference.data()+index,4)!=0;
      const float distance=std::hypot((x+.5f)/size-(gaze_x+1)*.5f,(y+.5f)/size-(1-gaze_y)*.5f);
      if(distance<=radius)focus_changes+=changed;else peripheral_changes+=changed;
      alpha_changes+=output[index+3]!=255;
    }
    std::printf("Scene FDM GPU mode=%d radius=%.2f gaze=%.1f,%.1f focus_changes=%u peripheral_changes=%u alpha_changes=%u\n",level,radius,gaze_x,gaze_y,focus_changes,peripheral_changes,alpha_changes);
    if(focus_changes) {
      for(unsigned ty=0;ty<16;++ty) {
        for(unsigned tx=0;tx<16;++tx) {
          unsigned changed=0;
          for(unsigned y=ty*(size/16);y<(ty+1)*(size/16);++y)for(unsigned x=tx*(size/16);x<(tx+1)*(size/16);++x)
            changed+=std::memcmp(output+(y*size+x)*4,reference.data()+(y*size+x)*4,4)!=0;
          std::printf("%3u ",changed*100/((size/16)*(size/16)));
        }
        std::puts("");
      }
      unsigned shown=0;
      for(unsigned y=0;y<size && shown<8;++y)for(unsigned x=0;x<size && shown<8;++x) {
        const auto i=(y*size+x)*4;
        if(std::hypot((x+.5f)/size-(gaze_x+1)*.5f,(y+.5f)/size-(1-gaze_y)*.5f)<=radius && std::memcmp(output+i,reference.data()+i,4)) {
          std::printf("focus mismatch %u,%u expected=%u,%u,%u actual=%u,%u,%u\n",x,y,reference[i],reference[i+1],reference[i+2],output[i],output[i+1],output[i+2]);++shown;
        }
      }
    }
    Check(!focus_changes,"full-density gaze disk matches reference exactly");
    if(gaze_x==.4f && gaze_y==.3f)Check(peripheral_changes>size,"coarse shading actually affects the periphery");
    Check(!alpha_changes,"alpha preserved");
  }
  vkDestroyCommandPool(device,pool,nullptr);vkUnmapMemory(device,buffer_memory);
  vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,buffer_memory,nullptr);
  vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);
  for(auto shader:shaders)vkDestroyShaderModule(device,shader,nullptr);
  vkDestroyFramebuffer(device,framebuffer,nullptr);vkDestroyRenderPass(device,pass,nullptr);
  for(unsigned i=0;i<3;++i){vkDestroyImageView(device,views[i],nullptr);vkDestroyImage(device,images[i],nullptr);vkFreeMemory(device,image_memory[i],nullptr);}
  vkDestroyDevice(device,nullptr);
  reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);
  vkDestroyInstance(instance,nullptr);Check(!errors,"zero validation errors");
  std::puts("Scene FDM: 36 colour/depth raster cases; exact gaze detail, real peripheral shading, zero validation errors");
}
