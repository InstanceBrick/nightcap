#include "mocktail/vr/fsr3_upscaler.h"
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include "opticalflow_motion_spv.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

FfxErrorCode GetDeviceCapabilitiesVK(FfxInterface*, FfxDeviceCapabilities*);

namespace mocktail::vr {
namespace {
FfxErrorCode Fp32Capabilities(FfxInterface* backend, FfxDeviceCapabilities* caps) {
  const auto result=GetDeviceCapabilitiesVK(backend,caps);
  if(result==FFX_OK) {
    // Use shader permutations that do not require enabling float16 or forced
    // subgroup-size features on the guest-created logical device.
    caps->fp16Supported=false;
    caps->dedicatedAllocationSupported=false;
    caps->deviceCoherentMemorySupported=false;
    caps->bufferMarkerSupported=false;
    caps->extendedSynchronizationSupported=false;
    caps->shaderStorageBufferArrayNonUniformIndexing=false;
    caps->maximumSupportedShaderModel=std::min(caps->maximumSupportedShaderModel,
                                              FFX_SHADER_MODEL_6_5);
  }
  return result;
}
FfxErrorCode Fp16Capabilities(FfxInterface* backend,FfxDeviceCapabilities* caps) {
  const auto result=Fp32Capabilities(backend,caps);
  if(result==FFX_OK) {
    FfxDeviceCapabilities supported{};
    if(GetDeviceCapabilitiesVK(backend,&supported)==FFX_OK)
      caps->fp16Supported=supported.fp16Supported;
  }
  return result;
}
void Message(FfxMsgType type,const wchar_t* text) {
  std::fprintf(stderr,"  [vr-fsr3] %s: %ls\n",
               type==FFX_MESSAGE_TYPE_ERROR?"error":"warning",text);
}
bool Same(const Fsr3Upscaler::Configuration& a,const Fsr3Upscaler::Configuration& b) {
  return a.render_width==b.render_width && a.render_height==b.render_height &&
         a.output_width==b.output_width && a.output_height==b.output_height &&
         a.inverted_depth==b.inverted_depth && a.infinite_depth==b.infinite_depth &&
         a.hdr==b.hdr && a.estimate_motion==b.estimate_motion &&
         a.motion_vectors_jittered==b.motion_vectors_jittered && a.allow_fp16==b.allow_fp16;
}
FfxResource Resource(const Fsr3Upscaler::Image& image,bool storage,const wchar_t* name) {
  if(!image.image)return {};
  VkImageCreateInfo info{};
  info.imageType=VK_IMAGE_TYPE_2D;info.format=image.format;
  info.extent={image.width,image.height,1};info.mipLevels=info.arrayLayers=1;
  info.usage=storage?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT;
  return ffxGetResourceVK(reinterpret_cast<void*>(image.image),
      ffxGetImageResourceDescriptionVK(image.image,info),name,
      storage?FFX_RESOURCE_STATE_UNORDERED_ACCESS:FFX_RESOURCE_STATE_COMPUTE_READ);
}
}

struct Fsr3Upscaler::Impl {
  struct OwnedImage {
    Image image;
    VkDeviceMemory memory{};
    VkImageView view{};
    FfxResource resource{};
  };
  struct Eye {
    std::vector<std::uint64_t> scratch;
    std::unique_ptr<FfxFsr3UpscalerContext> context;
    std::array<OwnedImage,3> shared;
    bool initialized=false, first=true, shared_initialized=false;
    std::vector<std::uint64_t> flow_scratch;
    std::unique_ptr<FfxOpticalflowContext> flow_context;
    // SDK block flow, scene-change flags, render-resolution motion and reactive.
    std::array<OwnedImage,4> motion;
    VkDescriptorSet motion_set{};
    std::uint64_t flow_frames=0;
    JitterOffset previous_jitter;
    bool flow_initialized=false, flow_resources_initialized=false;
  };
  VkDevice device{};
  VkPhysicalDevice physical{};
  PFN_vkGetDeviceProcAddr proc{};
  VkPhysicalDeviceMemoryProperties memory_properties{};
  Configuration config;
  Eye eyes[2];
  VkDescriptorSetLayout motion_descriptor_layout{};
  VkDescriptorPool motion_descriptor_pool{};
  VkPipelineLayout motion_pipeline_layout{};
  VkPipeline motion_pipeline{};
  int error=FFX_OK;
  template<class T>T Proc(const char* name) const {
    return reinterpret_cast<T>(proc(device,name));
  }
  bool Allocate(OwnedImage& owned,const FfxCreateResourceDescription& desc) {
    const auto format=desc.resourceDescription.format;
    VkFormat vk_format=VK_FORMAT_UNDEFINED;
    if(format==FFX_SURFACE_FORMAT_R32_FLOAT)vk_format=VK_FORMAT_R32_SFLOAT;
    if(format==FFX_SURFACE_FORMAT_R32_UINT)vk_format=VK_FORMAT_R32_UINT;
    if(format==FFX_SURFACE_FORMAT_R16G16_FLOAT)vk_format=VK_FORMAT_R16G16_SFLOAT;
    if(format==FFX_SURFACE_FORMAT_R16G16_SINT)vk_format=VK_FORMAT_R16G16_SINT;
    if(format==FFX_SURFACE_FORMAT_R8_UNORM)vk_format=VK_FORMAT_R8_UNORM;
    if(vk_format==VK_FORMAT_UNDEFINED)return false;
    VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType=VK_IMAGE_TYPE_2D;info.format=vk_format;
    info.extent={desc.resourceDescription.width,desc.resourceDescription.height,1};
    info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;
    info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|
               VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto result=Proc<PFN_vkCreateImage>("vkCreateImage")(device,&info,nullptr,&owned.image.image);
    if(result!=VK_SUCCESS)return false;
    VkMemoryRequirements requirement{};
    Proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device,owned.image.image,&requirement);
    unsigned type=0;
    while(type<memory_properties.memoryTypeCount &&
          (!(requirement.memoryTypeBits&(1u<<type)) ||
           !(memory_properties.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))++type;
    if(type==memory_properties.memoryTypeCount)return false;
    VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize=requirement.size;allocation.memoryTypeIndex=type;
    if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device,&allocation,nullptr,&owned.memory)!=VK_SUCCESS)return false;
    if(Proc<PFN_vkBindImageMemory>("vkBindImageMemory")(device,owned.image.image,owned.memory,0)!=VK_SUCCESS)return false;
    owned.image.format=vk_format;owned.image.width=info.extent.width;owned.image.height=info.extent.height;
    owned.resource=Resource(owned.image,true,desc.name);
    return true;
  }
  bool InitializeMotionPipeline() {
    VkDescriptorSetLayoutBinding bindings[4]{};
    for(unsigned i=0;i<4;++i) {
      bindings[i].binding=i;bindings[i].descriptorCount=1;
      bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
      bindings[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount=4;layout.pBindings=bindings;
    if(Proc<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device,&layout,nullptr,&motion_descriptor_layout)!=VK_SUCCESS)return false;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,32};
    VkPipelineLayoutCreateInfo pipeline_layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout.setLayoutCount=1;pipeline_layout.pSetLayouts=&motion_descriptor_layout;
    pipeline_layout.pushConstantRangeCount=1;pipeline_layout.pPushConstantRanges=&push;
    if(Proc<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device,&pipeline_layout,nullptr,&motion_pipeline_layout)!=VK_SUCCESS)return false;
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=2;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
    if(Proc<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(device,&pool,nullptr,&motion_descriptor_pool)!=VK_SUCCESS)return false;
    VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_info.codeSize=sizeof(kOpticalflowToMotion);shader_info.pCode=kOpticalflowToMotion;
    VkShaderModule shader{};
    if(Proc<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device,&shader_info,nullptr,&shader)!=VK_SUCCESS)return false;
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=shader;pipeline.stage.pName="main";
    pipeline.layout=motion_pipeline_layout;
    const auto result=Proc<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(device,{},1,&pipeline,nullptr,&motion_pipeline);
    Proc<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device,shader,nullptr);
    return result==VK_SUCCESS;
  }
  bool InitializeFlow(Eye& eye) {
    VkDeviceContext device_context{device,physical,proc};
    const auto scratch_bytes=ffxGetScratchMemorySizeVK(physical,1);
    eye.flow_scratch.assign((scratch_bytes+7)/8,0);
    FfxOpticalflowContextDescription desc{};
    error=ffxGetInterfaceVK(&desc.backendInterface,ffxGetDeviceVK(&device_context),
        eye.flow_scratch.data(),eye.flow_scratch.size()*8,1);
    if(error!=FFX_OK)return false;
    desc.backendInterface.fpGetDeviceCapabilities=Fp32Capabilities;
    desc.resolution={config.render_width,config.render_height};
    eye.flow_context=std::make_unique<FfxOpticalflowContext>();
    error=ffxOpticalflowContextCreate(eye.flow_context.get(),&desc);
    if(error!=FFX_OK)return false;
    eye.flow_initialized=true;
    FfxOpticalflowSharedResourceDescriptions shared{};
    error=ffxOpticalflowGetSharedResourceDescriptions(eye.flow_context.get(),&shared);
    if(error!=FFX_OK)return false;
    if(!Allocate(eye.motion[0],shared.opticalFlowVector) ||
       !Allocate(eye.motion[1],shared.opticalFlowSCD))return false;
    FfxCreateResourceDescription motion{};
    motion.resourceDescription=shared.opticalFlowVector.resourceDescription;
    motion.resourceDescription.width=config.render_width;
    motion.resourceDescription.height=config.render_height;
    motion.resourceDescription.format=FFX_SURFACE_FORMAT_R16G16_FLOAT;
    motion.name=L"Estimated pixel motion";
    if(!Allocate(eye.motion[2],motion))return false;
    motion.resourceDescription.format=FFX_SURFACE_FORMAT_R8_UNORM;
    motion.name=L"Optical flow reactive mask";
    if(!Allocate(eye.motion[3],motion))return false;
    for(auto& owned:eye.motion) {
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image=owned.image.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=owned.image.format;
      view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      if(Proc<PFN_vkCreateImageView>("vkCreateImageView")(device,&view,nullptr,&owned.view)!=VK_SUCCESS)return false;
    }
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool=motion_descriptor_pool;allocate.descriptorSetCount=1;
    allocate.pSetLayouts=&motion_descriptor_layout;
    if(Proc<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(device,&allocate,&eye.motion_set)!=VK_SUCCESS)return false;
    VkDescriptorImageInfo images[4]{};
    VkWriteDescriptorSet writes[4]{};
    for(unsigned i=0;i<4;++i) {
      images[i].imageView=eye.motion[i].view;
      images[i].imageLayout=i<2?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
      writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=eye.motion_set;
      writes[i].dstBinding=i;writes[i].descriptorCount=1;
      writes[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      writes[i].pImageInfo=&images[i];
    }
    Proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device,4,writes,0,nullptr);
    return true;
  }
  void ImageBarrier(VkCommandBuffer command,const OwnedImage& image,VkImageLayout from,VkImageLayout to,
      VkAccessFlags source,VkAccessFlags destination,VkPipelineStageFlags source_stage=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image=image.image.image;barrier.oldLayout=from;barrier.newLayout=to;
    barrier.srcAccessMask=source;barrier.dstAccessMask=destination;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,source_stage,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,0,nullptr,0,nullptr,1,&barrier);
  }
  bool EstimateMotion(VkCommandBuffer command,Eye& eye,const Frame& frame) {
    if(!eye.flow_initialized || frame.color.width!=config.render_width ||
       frame.color.height!=config.render_height)return false;
    if(!eye.flow_resources_initialized) {
      for(auto& owned:eye.motion)ImageBarrier(command,owned,VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    } else {
      for(unsigned i=2;i<4;++i)ImageBarrier(command,eye.motion[i],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    }
    FfxOpticalflowDispatchDescription dispatch{};
    dispatch.commandList=ffxGetCommandListVK(command);
    dispatch.color=Resource(frame.color,false,L"Optical flow linear color");
    dispatch.opticalFlowVector=eye.motion[0].resource;dispatch.opticalFlowSCD=eye.motion[1].resource;
    dispatch.reset=frame.reset || eye.first || eye.flow_frames==0;
    dispatch.backbufferTransferFunction=0;dispatch.minMaxLuminance={0.f,1.f};
    error=ffxOpticalflowContextDispatch(eye.flow_context.get(),&dispatch);
    if(error!=FFX_OK)return false;
    for(unsigned i=0;i<2;++i)ImageBarrier(command,eye.motion[i],VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    struct Parameters {
      std::int32_t render_size[2],flow_size[2];float jitter_delta[2];std::uint32_t reset,reserved;
    } parameters{{static_cast<int>(config.render_width),static_cast<int>(config.render_height)},
                 {static_cast<int>(eye.motion[0].image.width),static_cast<int>(eye.motion[0].image.height)},
                 {frame.jitter.x-eye.previous_jitter.x,frame.jitter.y-eye.previous_jitter.y},
                 frame.reset || eye.first || eye.flow_frames<6,0};
    static_assert(sizeof(Parameters)==32);
    Proc<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_COMPUTE,motion_pipeline);
    Proc<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_COMPUTE,
        motion_pipeline_layout,0,1,&eye.motion_set,0,nullptr);
    Proc<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command,motion_pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
    Proc<PFN_vkCmdDispatch>("vkCmdDispatch")(command,(config.render_width+7)/8,(config.render_height+7)/8,1);
    for(unsigned i=2;i<4;++i)ImageBarrier(command,eye.motion[i],VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    for(unsigned i=0;i<2;++i)ImageBarrier(command,eye.motion[i],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
    eye.flow_resources_initialized=true;eye.previous_jitter=frame.jitter;++eye.flow_frames;
    return true;
  }
  bool InitializeEye(Eye& eye) {
    VkDeviceContext device_context{device,physical,proc};
    const auto scratch_bytes=ffxGetScratchMemorySizeVK(physical,1);
    if(!scratch_bytes){error=FFX_ERROR_BACKEND_API_ERROR;return false;}
    eye.scratch.assign((scratch_bytes+7)/8,0);
    FfxFsr3UpscalerContextDescription desc{};
    error=ffxGetInterfaceVK(&desc.backendInterface,ffxGetDeviceVK(&device_context),
        eye.scratch.data(),eye.scratch.size()*8,1);
    if(error!=FFX_OK)return false;
    desc.backendInterface.fpGetDeviceCapabilities=config.allow_fp16?Fp16Capabilities:Fp32Capabilities;
    desc.flags=FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE|FFX_FSR3UPSCALER_ENABLE_DYNAMIC_RESOLUTION|
               FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
    if(config.inverted_depth)desc.flags|=FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED;
    if(config.infinite_depth)desc.flags|=FFX_FSR3UPSCALER_ENABLE_DEPTH_INFINITE;
    if(config.hdr)desc.flags|=FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;
    if(config.motion_vectors_jittered)desc.flags|=FFX_FSR3UPSCALER_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    desc.maxRenderSize={config.render_width,config.render_height};
    desc.maxUpscaleSize={config.output_width,config.output_height};desc.fpMessage=Message;
    eye.context=std::make_unique<FfxFsr3UpscalerContext>();
    error=ffxFsr3UpscalerContextCreate(eye.context.get(),&desc);
    if(error!=FFX_OK)return false;
    eye.initialized=true;
    FfxFsr3UpscalerSharedResourceDescriptions shared{};
    error=ffxFsr3UpscalerGetSharedResourceDescriptions(eye.context.get(),&shared);
    if(error!=FFX_OK)return false;
    return Allocate(eye.shared[0],shared.dilatedDepth) &&
           Allocate(eye.shared[1],shared.dilatedMotionVectors) &&
           Allocate(eye.shared[2],shared.reconstructedPrevNearestDepth) &&
           (!config.estimate_motion || InitializeFlow(eye));
  }
  void Destroy() {
    if(!device || !proc)return;
    for(auto& eye:eyes) {
      if(eye.initialized)ffxFsr3UpscalerContextDestroy(eye.context.get());
      if(eye.flow_initialized)ffxOpticalflowContextDestroy(eye.flow_context.get());
      for(auto& shared:eye.shared) {
        if(shared.image.image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device,shared.image.image,nullptr);
        if(shared.memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device,shared.memory,nullptr);
      }
      for(auto& motion:eye.motion) {
        if(motion.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device,motion.view,nullptr);
        if(motion.image.image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device,motion.image.image,nullptr);
        if(motion.memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device,motion.memory,nullptr);
      }
      eye=Eye{};
    }
    if(motion_pipeline)Proc<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device,motion_pipeline,nullptr);
    if(motion_pipeline_layout)Proc<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device,motion_pipeline_layout,nullptr);
    if(motion_descriptor_pool)Proc<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device,motion_descriptor_pool,nullptr);
    if(motion_descriptor_layout)Proc<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device,motion_descriptor_layout,nullptr);
    motion_pipeline={};motion_pipeline_layout={};motion_descriptor_pool={};motion_descriptor_layout={};
    device={};physical={};proc={};
  }
};

Fsr3Upscaler::Fsr3Upscaler():impl_(std::make_unique<Impl>()){}
Fsr3Upscaler::~Fsr3Upscaler(){Reset();}
void Fsr3Upscaler::Reset(){impl_->Destroy();}
int Fsr3Upscaler::last_error() const{return impl_->error;}
Fsr3Upscaler::Image Fsr3Upscaler::estimated_motion(int eye) const {
  return eye>=0 && eye<2?impl_->eyes[eye].motion[2].image:Image{};
}

bool Fsr3Upscaler::Configure(VkPhysicalDevice physical,VkDevice device,
    PFN_vkGetDeviceProcAddr proc,const VkPhysicalDeviceMemoryProperties& memory,
    const Configuration& config) {
  auto& state=*impl_;
  if(state.device==device && state.physical==physical &&
     state.eyes[0].initialized && state.eyes[1].initialized && Same(state.config,config))return true;
  // Changing allocation limits requires the owner to retire pending GPU users.
  if(state.device)return false;
  if(!physical || !device || !proc || !config.render_width || !config.render_height ||
     config.output_width<config.render_width || config.output_height<config.render_height ||
     (config.estimate_motion && config.motion_vectors_jittered))return false;
  state.physical=physical;state.device=device;state.proc=proc;
  state.memory_properties=memory;state.config=config;
  if((config.estimate_motion && !state.InitializeMotionPipeline()) ||
     !state.InitializeEye(state.eyes[0]) || !state.InitializeEye(state.eyes[1])) {
    if(state.error==FFX_OK)state.error=FFX_ERROR_BACKEND_API_ERROR;
    std::fprintf(stderr,"  [vr-fsr3] context creation failed: %d\n",state.error);
    state.Destroy();return false;
  }
  std::fprintf(stderr,"  [vr-fsr3] AMD FSR 3.1.4 %s stereo contexts ready: %ux%u -> %ux%u\n",
      config.allow_fp16?"FP16 enabled":"FP32",
      config.render_width,config.render_height,config.output_width,config.output_height);
  if(config.estimate_motion)std::fprintf(stderr,"  [vr-fsr3] AMD Optical Flow image-motion fallback enabled\n");
  return true;
}

bool Fsr3Upscaler::Record(VkCommandBuffer command,int index,const Frame& frame) {
  auto& state=*impl_;
  if(index<0 || index>1 || !command || !state.device)return false;
  auto& eye=state.eyes[index];
  if(!eye.initialized || !frame.color.image || !frame.depth.image ||
     (!frame.motion_vectors.image && !state.config.estimate_motion) || !frame.output.image ||
     !frame.color.width || !frame.color.height ||
     frame.depth.width!=frame.color.width || frame.depth.height!=frame.color.height ||
     (frame.motion_vectors.image && (frame.motion_vectors.width!=frame.color.width || frame.motion_vectors.height!=frame.color.height)) ||
     frame.color.width>state.config.render_width || frame.color.height>state.config.render_height ||
     frame.output.width>state.config.output_width || frame.output.height>state.config.output_height ||
     frame.output.width<frame.color.width || frame.output.height<frame.color.height ||
     !std::isfinite(frame.delta_ms) || frame.delta_ms<=0 ||
     !std::isfinite(frame.jitter.x) || !std::isfinite(frame.jitter.y) ||
     !std::isfinite(frame.near_plane) || !std::isfinite(frame.far_plane) ||
     !std::isfinite(frame.vertical_fov) || frame.near_plane<=0 || frame.far_plane<=frame.near_plane ||
     frame.vertical_fov<=0 || frame.vertical_fov>=3.1415927f)return false;
  if(!eye.shared_initialized) {
    VkImageMemoryBarrier barriers[3]{};
    for(unsigned i=0;i<3;++i) {
      auto& barrier=barriers[i];barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
      barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
      barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
      barrier.image=eye.shared[i].image.image;
      barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    }
    state.Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
        0,nullptr,0,nullptr,3,barriers);
    eye.shared_initialized=true;
  }
  const bool estimated=!frame.motion_vectors.image;
  if(estimated && !state.EstimateMotion(command,eye,frame))return false;
  if(!estimated)eye.flow_frames=0;
  FfxFsr3UpscalerDispatchDescription dispatch{};
  dispatch.commandList=ffxGetCommandListVK(command);
  dispatch.color=Resource(frame.color,false,L"Scene color");
  dispatch.depth=Resource(frame.depth,false,L"Scene depth");
  dispatch.motionVectors=Resource(estimated?eye.motion[2].image:frame.motion_vectors,false,L"Scene motion");
  dispatch.reactive=Resource(estimated && !frame.reactive.image?eye.motion[3].image:frame.reactive,false,L"Reactive mask");
  dispatch.transparencyAndComposition=Resource(frame.transparency,false,L"Composition mask");
  dispatch.output=Resource(frame.output,true,L"Upscaled linear color");
  dispatch.dilatedDepth=eye.shared[0].resource;
  dispatch.dilatedMotionVectors=eye.shared[1].resource;
  dispatch.reconstructedPrevNearestDepth=eye.shared[2].resource;
  dispatch.jitterOffset={frame.jitter.x,frame.jitter.y};
  dispatch.motionVectorScale=estimated?FfxFloatCoords2D{1.f,1.f}:
      FfxFloatCoords2D{frame.motion_vector_scale[0],frame.motion_vector_scale[1]};
  dispatch.renderSize={frame.color.width,frame.color.height};
  dispatch.upscaleSize={frame.output.width,frame.output.height};
  dispatch.enableSharpening=frame.sharpening;dispatch.sharpness=std::clamp(frame.sharpness,0.f,1.f);
  dispatch.frameTimeDelta=frame.delta_ms;dispatch.preExposure=frame.pre_exposure;
  dispatch.reset=frame.reset || eye.first;
  dispatch.cameraNear=state.config.inverted_depth?frame.far_plane:frame.near_plane;
  dispatch.cameraFar=state.config.inverted_depth?frame.near_plane:frame.far_plane;
  dispatch.cameraFovAngleVertical=frame.vertical_fov;
  dispatch.viewSpaceToMetersFactor=frame.meters_per_world_unit;
  state.error=ffxFsr3UpscalerContextDispatch(eye.context.get(),&dispatch);
  if(state.error!=FFX_OK) {
    eye.first=true;
    std::fprintf(stderr,"  [vr-fsr3] eye%d dispatch failed: %d\n",index,state.error);
    return false;
  }
  eye.first=false;
  return true;
}

Fsr3Upscaler::JitterOffset Fsr3Upscaler::Jitter(std::uint64_t frame,
    std::uint32_t render_width,std::uint32_t output_width) {
  if(!render_width || !output_width || output_width<render_width)return {};
  const int phases=ffxFsr3UpscalerGetJitterPhaseCount(render_width,output_width);
  JitterOffset result;
  if(phases>0)ffxFsr3UpscalerGetJitterOffset(&result.x,&result.y,
      static_cast<int>(frame%static_cast<std::uint64_t>(phases)),phases);
  return result;
}
}
