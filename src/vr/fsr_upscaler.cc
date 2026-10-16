#include "mocktail/vr/fsr_upscaler.h"
#include "mocktail/vr/foveation_focus.h"
#include "gaze_aa_spv.h"
#include "fsr_spv.h"
#include "temporal_spv.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mocktail::vr {
namespace {
bool Srgb(VkFormat f) { return f==VK_FORMAT_R8G8B8A8_SRGB || f==VK_FORMAT_B8G8R8A8_SRGB; }
bool Supported(VkFormat f) { return Srgb(f) || f==VK_FORMAT_R8G8B8A8_UNORM || f==VK_FORMAT_B8G8R8A8_UNORM; }
VkFormat EncodedFormat(VkFormat f) {
  if(f==VK_FORMAT_R8G8B8A8_SRGB)return VK_FORMAT_R8G8B8A8_UNORM;
  if(f==VK_FORMAT_B8G8R8A8_SRGB)return VK_FORMAT_B8G8R8A8_UNORM;
  return f;
}
std::uint32_t Bits(float f) { std::uint32_t u;std::memcpy(&u,&f,4);return u; }
}

VkFormat FsrUpscaler::intermediate_format() const { return EncodedFormat(format_); }

VkPipeline FsrUpscaler::Pipeline(VkRenderPass pass,const std::uint32_t* code,std::size_t bytes) {
  VkShaderModule shaders[2]{};
  const auto cleanup=[&]{for(auto shader:shaders)if(shader)Proc<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device_,shader,nullptr);};
  VkShaderModuleCreateInfo sm{};sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;sm.codeSize=sizeof(kGazeAaVertex);sm.pCode=kGazeAaVertex;
  if(Proc<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device_,&sm,nullptr,&shaders[0])!=VK_SUCCESS)return {};
  sm.codeSize=bytes;sm.pCode=code;
  if(Proc<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device_,&sm,nullptr,&shaders[1])!=VK_SUCCESS){cleanup();return {};}
  VkPipelineShaderStageCreateInfo stages[2]{};
  for(int i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=shaders[i];stages[i].pName="main";}
  VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;
  VkPipelineRasterizationStateCreateInfo rs{};rs.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;rs.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=15;
  VkPipelineColorBlendStateCreateInfo blend{};blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=1;blend.pAttachments=&attachment;
  VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo ds{};ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;ds.dynamicStateCount=2;ds.pDynamicStates=dynamic;
  VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.stageCount=2;info.pStages=stages;info.pVertexInputState=&vi;info.pInputAssemblyState=&ia;info.pViewportState=&vp;info.pRasterizationState=&rs;info.pMultisampleState=&ms;info.pColorBlendState=&blend;info.pDynamicState=&ds;info.layout=layout_;info.renderPass=pass;
  VkPipeline result{};
  const auto status=Proc<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(device_,{},1,&info,nullptr,&result);
  cleanup();return status==VK_SUCCESS?result:VK_NULL_HANDLE;
}

bool FsrUpscaler::Initialize(VkDevice device,PFN_vkGetDeviceProcAddr proc,VkFormat format) {
  if(device_==device && pipelines_[0] && format_==format)return true;
  Reset();device_=device;proc_=proc;format_=format;
  if(!device_ || !proc_ || !Supported(format))return false;
  const auto fail=[&]{Reset();return false;};
  VkDescriptorSetLayoutBinding binding[2]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
  VkDescriptorSetLayoutCreateInfo dl{};dl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;dl.bindingCount=2;dl.pBindings=binding;
  if(Proc<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device_,&dl,nullptr,&descriptor_layout_)!=VK_SUCCESS)return fail();
  VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Constants)};
  VkPipelineLayoutCreateInfo pl{};pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pl.setLayoutCount=1;pl.pSetLayouts=&descriptor_layout_;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
  if(Proc<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device_,&pl,nullptr,&layout_)!=VK_SUCCESS)return fail();
  VkSamplerCreateInfo sampler{};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if(Proc<PFN_vkCreateSampler>("vkCreateSampler")(device_,&sampler,nullptr,&sampler_)!=VK_SUCCESS)return fail();
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,12};
  VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;pool.maxSets=6;pool.poolSizeCount=1;pool.pPoolSizes=&size;
  if(Proc<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(device_,&pool,nullptr,&descriptor_pool_)!=VK_SUCCESS)return fail();
  for(int i=0;i<2;++i) {
    VkAttachmentDescription a{};a.format=i?format:EncodedFormat(format);a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=i?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout=i?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout=i?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
    VkSubpassDependency dependency{};dependency.srcSubpass=0;dependency.dstSubpass=VK_SUBPASS_EXTERNAL;dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=1;rp.pAttachments=&a;rp.subpassCount=1;rp.pSubpasses=&subpass;rp.dependencyCount=1;rp.pDependencies=&dependency;
    if(Proc<PFN_vkCreateRenderPass>("vkCreateRenderPass")(device_,&rp,nullptr,&passes_[i])!=VK_SUCCESS)return fail();
    pipelines_[i]=Pipeline(passes_[i],i?kFsrRcas:kFsrEasu,i?sizeof(kFsrRcas):sizeof(kFsrEasu));
    if(!pipelines_[i])return fail();
  }
  std::fprintf(stderr,"  [vr-fsr] FP32 EASU and gaze-only RCAS ready\n");
  return true;
}

void FsrUpscaler::FreeEye(Eye& e) {
  if(e.framebuffer)Proc<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(device_,e.framebuffer,nullptr);
  if(e.source_view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,e.source_view,nullptr);
  if(e.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,e.view,nullptr);
  if(e.image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,e.image,nullptr);
  if(e.gamma_image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,e.gamma_image,nullptr);
  if(e.gamma_memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,e.gamma_memory,nullptr);
  if(e.history_view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,e.history_view,nullptr);
  if(e.history_image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,e.history_image,nullptr);
  if(e.history_memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,e.history_memory,nullptr);
  if(e.memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,e.memory,nullptr);
  if(e.sets[0])Proc<PFN_vkFreeDescriptorSets>("vkFreeDescriptorSets")(device_,descriptor_pool_,3,e.sets);
  e={};
}
bool FsrUpscaler::Prepare(VkDevice device,PFN_vkGetDeviceProcAddr proc,
    const VkPhysicalDeviceMemoryProperties& memory,int eye,VkImage source,
    VkFormat source_format,VkImage destination,VkFormat output_format,
    std::uint32_t iw,std::uint32_t ih,std::uint32_t ow,std::uint32_t oh,bool temporal) {
  if(eye<0 || eye>1 || !source || !destination || !iw || !ih || iw>ow || ih>oh || !Supported(source_format) || !Supported(output_format))return false;
  if(!Initialize(device,proc,output_format))return false;
  auto& e=eyes_[eye];
  if(e.image && (e.width!=ow || e.height!=oh))return false; // Owner retires old AA views before a resize.
  const auto view=[&](VkImage image,VkFormat format,VkImageView* result){VkImageViewCreateInfo v{};v.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;v.image=image;v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.format=format;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};return Proc<PFN_vkCreateImageView>("vkCreateImageView")(device_,&v,nullptr,result)==VK_SUCCESS;};
  const auto fb=[&](VkImageView image,VkRenderPass pass,VkFramebuffer* result){VkFramebufferCreateInfo f{};f.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;f.renderPass=pass;f.attachmentCount=1;f.pAttachments=&image;f.width=ow;f.height=oh;f.layers=1;return Proc<PFN_vkCreateFramebuffer>("vkCreateFramebuffer")(device_,&f,nullptr,result)==VK_SUCCESS;};
  if(!e.image) {
    VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.imageType=VK_IMAGE_TYPE_2D;image.format=EncodedFormat(output_format);image.extent={ow,oh,1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(Proc<PFN_vkCreateImage>("vkCreateImage")(device_,&image,nullptr,&e.image)!=VK_SUCCESS)return false;
    VkMemoryRequirements req{};Proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device_,e.image,&req);
    unsigned type=0;
    while(type<memory.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || !(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))++type;
    if(type==memory.memoryTypeCount){FreeEye(e);return false;}
    VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=req.size;a.memoryTypeIndex=type;
    if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device_,&a,nullptr,&e.memory)!=VK_SUCCESS || Proc<PFN_vkBindImageMemory>("vkBindImageMemory")(device_,e.image,e.memory,0)!=VK_SUCCESS || !view(e.image,EncodedFormat(output_format),&e.view) || !fb(e.view,passes_[0],&e.framebuffer)){FreeEye(e);return false;}
    VkDescriptorSetLayout layouts[]={descriptor_layout_,descriptor_layout_,descriptor_layout_};
    VkDescriptorSetAllocateInfo sets{};sets.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;sets.descriptorPool=descriptor_pool_;sets.descriptorSetCount=3;sets.pSetLayouts=layouts;
    if(Proc<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(device_,&sets,e.sets)!=VK_SUCCESS){FreeEye(e);return false;}
    e.width=ow;e.height=oh;
    VkDescriptorImageInfo info{sampler_,e.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=e.sets[1];write.dstBinding=0;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&info;
    Proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device_,1,&write,0,nullptr);
    std::fprintf(stderr,"  [vr-fsr] eye%d %ux%u -> %ux%u\n",eye,iw,ih,ow,oh);
  }
  // EASU consumes perceptually encoded values. A raw copy to UNORM avoids
  // dozens of sRGB decode/re-encode operations per output pixel, without
  // requiring mutable-format views on guest-owned images.
  if(Srgb(source_format) && !e.gamma_image) {
    VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType=VK_IMAGE_TYPE_2D;image.format=EncodedFormat(source_format);
    image.extent={iw,ih,1};image.mipLevels=image.arrayLayers=1;
    image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    if(Proc<PFN_vkCreateImage>("vkCreateImage")(device_,&image,nullptr,&e.gamma_image)!=VK_SUCCESS){FreeEye(e);return false;}
    VkMemoryRequirements req{};Proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device_,e.gamma_image,&req);
    unsigned type=0;
    while(type<memory.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || !(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))++type;
    if(type==memory.memoryTypeCount){FreeEye(e);return false;}
    VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;
    if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device_,&allocation,nullptr,&e.gamma_memory)!=VK_SUCCESS ||
       Proc<PFN_vkBindImageMemory>("vkBindImageMemory")(device_,e.gamma_image,e.gamma_memory,0)!=VK_SUCCESS){FreeEye(e);return false;}
  }
  if(e.source!=source || e.source_format!=source_format) {
    if(e.source_view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,e.source_view,nullptr);
    e.source_view={};e.source={};
    if(!view(Srgb(source_format)?e.gamma_image:source,EncodedFormat(source_format),&e.source_view))return false;
    VkDescriptorImageInfo info{sampler_,e.source_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=e.sets[0];write.dstBinding=0;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&info;
    Proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device_,1,&write,0,nullptr);
    e.source=source;e.source_format=source_format;
  }
  e.in_width=iw;e.in_height=ih;e.destination=destination;
  if(!targets_.count(destination)) {
    Target target;
    if(!view(destination,output_format,&target.view) || !fb(target.view,passes_[1],&target.framebuffer)) {
      if(target.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,target.view,nullptr);
      return false;
    }
    targets_.emplace(destination,target);
  }
  if(temporal)(void)PrepareHistory(memory,eye);
  return true;
}

void FsrUpscaler::Barrier(VkCommandBuffer command,VkImage image,VkImageLayout old_layout,VkImageLayout new_layout,
    VkAccessFlags source,VkAccessFlags destination,VkPipelineStageFlags source_stage,VkPipelineStageFlags destination_stage) {
  VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcAccessMask=source;b.dstAccessMask=destination;b.oldLayout=old_layout;b.newLayout=new_layout;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,source_stage,destination_stage,0,0,nullptr,0,nullptr,1,&b);
}
void FsrUpscaler::Draw(VkCommandBuffer command,VkRenderPass pass,VkFramebuffer target,VkPipeline pipeline,
    VkDescriptorSet set,const void* constants,const Eye& e,VkRect2D region) {
  VkRenderPassBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=pass;begin.framebuffer=target;begin.renderArea=region;
  Proc<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass")(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport{0,0,float(e.width),float(e.height),0,1};
  Proc<PFN_vkCmdSetViewport>("vkCmdSetViewport")(command,0,1,&viewport);
  Proc<PFN_vkCmdSetScissor>("vkCmdSetScissor")(command,0,1,&region);
  Proc<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
  Proc<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout_,0,1,&set,0,nullptr);
  Proc<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command,layout_,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Constants),constants);
  Proc<PFN_vkCmdDraw>("vkCmdDraw")(command,3,1,0,0);
  Proc<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass")(command);
}
void FsrUpscaler::RecordUpscale(VkCommandBuffer command,int eye,float gaze_x,float gaze_y,float radius) {
  const auto& e=eyes_[eye];
  // A 100% input already has every output pixel. Preserve its encoded bytes
  // instead of running edge reconstruction and an sRGB staging copy at 1:1.
  if(e.in_width==e.width && e.in_height==e.height &&
     EncodedFormat(e.source_format)==EncodedFormat(format_)) {
    Barrier(command,e.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    copy.extent={e.width,e.height,1};
    Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,e.source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        e.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    Barrier(command,e.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    return;
  }
  Constants pc;
  const float ix=1.f/e.in_width,iy=1.f/e.in_height;
  const float sx=e.in_width*(1.f/e.width),sy=e.in_height*(1.f/e.height);
  const float values[4][4]={{sx,sy,.5f*sx-.5f,.5f*sy-.5f},{ix,iy,ix,-iy},{-ix,2*iy,ix,2*iy},{0,4*iy,0,0}};
  for(int a=0;a<4;++a)for(int b=0;b<4;++b)pc.con[a][b]=Bits(values[a][b]);
  pc.input_srgb=pc.output_srgb=0;
  if(!std::isfinite(gaze_x) || !std::isfinite(gaze_y)){gaze_x=0;gaze_y=0;}
  pc.texel[0]=1.f/e.width;pc.texel[1]=1.f/e.height;
  pc.center[0]=std::clamp(.5f+.5f*gaze_x,0.f,1.f);
  pc.center[1]=std::clamp(.5f-.5f*gaze_y,0.f,1.f);
  pc.radius=internal::ClampGazeRadius(radius);
  if(Srgb(e.source_format)) {
    Barrier(command,e.gamma_image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_MEMORY_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={e.in_width,e.in_height,1};
    Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,e.source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,e.gamma_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    Barrier(command,e.gamma_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  } else {
    Barrier(command,e.source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  }
  Draw(command,passes_[0],e.framebuffer,pipelines_[0],e.sets[0],&pc,e,{{0,0},{e.width,e.height}});
  if(!Srgb(e.source_format))Barrier(command,e.source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
}
void FsrUpscaler::RecordSharpen(VkCommandBuffer command,int eye,float gaze_x,float gaze_y,float radius) {
  if(eye<0 || eye>1 || !std::isfinite(gaze_x) || !std::isfinite(gaze_y) || std::abs(gaze_x)>1.2f || std::abs(gaze_y)>1.2f)return;
  const auto& e=eyes_[eye];const auto target=targets_.find(e.destination);if(target==targets_.end())return;
  Constants pc;pc.con[0][0]=Bits(std::exp2(-.6f));pc.input_srgb=0;pc.output_srgb=Srgb(format_);
  pc.texel[0]=1.f/e.width;pc.texel[1]=1.f/e.height;
  pc.center[0]=std::clamp(.5f+.5f*gaze_x,0.f,1.f);pc.center[1]=std::clamp(.5f-.5f*gaze_y,0.f,1.f);pc.radius=internal::ClampGazeRadius(radius);
  const int x0=std::max(0,int(std::floor((pc.center[0]-pc.radius)*e.width))),y0=std::max(0,int(std::floor((pc.center[1]-pc.radius)*e.height)));
  const int x1=std::min(int(e.width),int(std::ceil((pc.center[0]+pc.radius)*e.width))),y1=std::min(int(e.height),int(std::ceil((pc.center[1]+pc.radius)*e.height)));
  const int left=std::max(0,x0-1),top=std::max(0,y0-1),right=std::min(int(e.width),x1+1),bottom=std::min(int(e.height),y1+1);
  Barrier(command,e.destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  Barrier(command,e.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.srcOffset=copy.dstOffset={left,top,0};copy.extent={unsigned(right-left),unsigned(bottom-top),1};
  Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,e.destination,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,e.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  Barrier(command,e.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  Barrier(command,e.destination,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  Draw(command,passes_[1],target->second.framebuffer,pipelines_[1],e.sets[1],&pc,e,{{x0,y0},{unsigned(x1-x0),unsigned(y1-y0)}});
  Barrier(command,e.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  Barrier(command,e.destination,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
}
void FsrUpscaler::ForgetImage(VkImage image) {
  for(auto& e:eyes_)if(e.source==image) {
    if(e.source_view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,e.source_view,nullptr);
    e.source_view={};e.source={};
  }
}
void FsrUpscaler::Reset() {
  if(!device_ || !proc_)return;
  for(auto& entry:targets_) {
    Proc<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(device_,entry.second.framebuffer,nullptr);
    Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,entry.second.view,nullptr);
  }
  targets_.clear();for(auto& e:eyes_)FreeEye(e);
  for(auto p:pipelines_)if(p)Proc<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device_,p,nullptr);
  for(auto p:passes_)if(p)Proc<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(device_,p,nullptr);
  if(sampler_)Proc<PFN_vkDestroySampler>("vkDestroySampler")(device_,sampler_,nullptr);
  if(layout_)Proc<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device_,layout_,nullptr);
  if(descriptor_pool_)Proc<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device_,descriptor_pool_,nullptr);
  if(descriptor_layout_)Proc<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device_,descriptor_layout_,nullptr);
  device_={};proc_=nullptr;format_={};passes_[0]=passes_[1]={};pipelines_[0]=pipelines_[1]=pipelines_[2]={};sampler_={};layout_={};descriptor_pool_={};descriptor_layout_={};
}
#include "fsr_temporal.inc"

}
