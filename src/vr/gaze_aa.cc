#include "mocktail/vr/gaze_aa.h"
#include "mocktail/vr/foveation_focus.h"
#include "gaze_aa_spv.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"

namespace mocktail::vr {
namespace {
bool Srgb(VkFormat f) {
  return f==VK_FORMAT_R8G8B8A8_SRGB || f==VK_FORMAT_B8G8R8A8_SRGB;
}
struct Focus {
  float texel[2],center[2],inner_radius=.18f,outer_radius=.22f;
  std::int32_t decode_output=0,encode_output=0;
};
static_assert(sizeof(Focus)==32);
}

VkPipeline GazeAa::MakePipeline(VkRenderPass render_pass,const std::uint32_t* code,std::size_t bytes) {
  VkShaderModule shaders[2]{};
  const auto destroy_shaders=[&]{for(auto shader:shaders)if(shader)Proc<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device_,shader,nullptr);};
  VkShaderModuleCreateInfo sm{};sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;sm.codeSize=sizeof(kGazeAaVertex);sm.pCode=kGazeAaVertex;
  if(Proc<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device_,&sm,nullptr,&shaders[0])!=VK_SUCCESS)return VK_NULL_HANDLE;
  sm.codeSize=bytes;sm.pCode=code;
  if(Proc<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device_,&sm,nullptr,&shaders[1])!=VK_SUCCESS){destroy_shaders();return VK_NULL_HANDLE;}
  VkPipelineShaderStageCreateInfo stages[2]{};
  for(int i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=shaders[i];stages[i].pName="main";}
  VkPipelineVertexInputStateCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ia{};ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;vp.viewportCount=vp.scissorCount=1;
  VkPipelineRasterizationStateCreateInfo rs{};rs.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;rs.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo ms{};ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{};cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;cb.attachmentCount=1;cb.pAttachments=&blend;
  VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo ds{};ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;ds.dynamicStateCount=2;ds.pDynamicStates=dynamic;
  VkGraphicsPipelineCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;pi.stageCount=2;pi.pStages=stages;pi.pVertexInputState=&vi;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&rs;pi.pMultisampleState=&ms;pi.pColorBlendState=&cb;pi.pDynamicState=&ds;pi.layout=pipeline_layout_;pi.renderPass=render_pass;
  VkPipeline pipeline{};
  auto result=Proc<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(device_,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline);
  destroy_shaders();
  if(result!=VK_SUCCESS){if(pipeline)Proc<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device_,pipeline,nullptr);return VK_NULL_HANDLE;}
  return pipeline;
}

bool GazeAa::Initialize(VkDevice device,PFN_vkGetDeviceProcAddr proc,VkFormat format) {
  if(device_==device && render_pass_ && format_==format)return true;
  Reset();device_=device;get_proc_=proc;format_=format;
  if(!device_ || !get_proc_)return false;
  const auto fail=[&]{Reset();return false;};
  VkDescriptorSetLayoutBinding bindings[3]{};
  for(unsigned i=0;i<3;++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
  VkDescriptorSetLayoutCreateInfo dl{};dl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;dl.bindingCount=3;dl.pBindings=bindings;
  if(Proc<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device_,&dl,nullptr,&descriptor_layout_)!=VK_SUCCESS)return fail();
  VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Focus)};
  VkPipelineLayoutCreateInfo pl{};pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pl.setLayoutCount=1;pl.pSetLayouts=&descriptor_layout_;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
  if(Proc<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device_,&pl,nullptr,&pipeline_layout_)!=VK_SUCCESS)return fail();
  VkAttachmentDescription attachment{};attachment.format=format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
  VkRenderPassCreateInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&subpass;
  if(Proc<PFN_vkCreateRenderPass>("vkCreateRenderPass")(device_,&rp,nullptr,&render_pass_)!=VK_SUCCESS)return fail();

  VkSamplerCreateInfo si{};si.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;si.maxLod=0;
  if(Proc<PFN_vkCreateSampler>("vkCreateSampler")(device_,&si,nullptr,&sampler_)!=VK_SUCCESS)return fail();
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,18};
  VkDescriptorPoolCreateInfo dp{};dp.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;dp.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;dp.maxSets=6;dp.poolSizeCount=1;dp.pPoolSizes=&size;
  if(Proc<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(device_,&dp,nullptr,&descriptor_pool_)!=VK_SUCCESS)return fail();
  VkDescriptorSetLayout layouts[]={descriptor_layout_,descriptor_layout_};
  VkDescriptorSetAllocateInfo da{};da.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;da.descriptorPool=descriptor_pool_;da.descriptorSetCount=2;da.pSetLayouts=layouts;
  if(Proc<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(device_,&da,descriptor_sets_)!=VK_SUCCESS)return fail();
  std::fprintf(stderr,"  [vr-aa] gaze AA resources ready; multisampling disabled\n");
  return true;
}

bool GazeAa::Prepare(VkDevice device,PFN_vkGetDeviceProcAddr proc,int eye,VkImage source,VkFormat source_format,VkImage destination,VkFormat destination_format,std::uint32_t width,std::uint32_t height,int mode,const VkPhysicalDeviceMemoryProperties* memory) {
  if(eye<0||eye>1||!source||!destination||source==destination||!width||!height)return false;
  if(!Initialize(device,proc,destination_format))return false;
  if(mode==1 && !pipeline_) {
    pipeline_=MakePipeline(render_pass_,kGazeAaFragment,sizeof(kGazeAaFragment));
    if(!pipeline_)return false;
  }
  auto& input=sources_[eye];
  if(input.image!=source || input.format!=source_format) {
    smaa_eyes_[eye].descriptor_source=VK_NULL_HANDLE;
    if(input.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,input.view,nullptr);
    input={};
    VkImageViewCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;vi.image=source;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=source_format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if(Proc<PFN_vkCreateImageView>("vkCreateImageView")(device_,&vi,nullptr,&input.view)!=VK_SUCCESS)return false;
    input.image=source;input.format=source_format;
  }
  auto target=targets_.find(destination);
  if(target==targets_.end()) {
    Target item;
    VkImageViewCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;vi.image=destination;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=destination_format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if(Proc<PFN_vkCreateImageView>("vkCreateImageView")(device_,&vi,nullptr,&item.view)!=VK_SUCCESS)return false;
    VkFramebufferCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fi.renderPass=render_pass_;fi.attachmentCount=1;fi.pAttachments=&item.view;fi.width=width;fi.height=height;fi.layers=1;
    if(Proc<PFN_vkCreateFramebuffer>("vkCreateFramebuffer")(device_,&fi,nullptr,&item.framebuffer)!=VK_SUCCESS){Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,item.view,nullptr);return false;}
    target=targets_.emplace(destination,item).first;
  }
  if(!input.descriptor_ready) {
  VkDescriptorImageInfo image{sampler_,input.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=descriptor_sets_[eye];write.dstBinding=0;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&image;
  Proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device_,1,&write,0,nullptr);
    input.descriptor_ready=true;
  }
  destinations_[eye]=destination;widths_[eye]=width;heights_[eye]=height;
  if(mode>=2 && (!memory || !PrepareSmaa(eye,*memory,mode)))return false;
  return true;
}

bool GazeAa::Record(VkCommandBuffer command,int eye,float gaze_x,float gaze_y,int mode,float radius) {
  if(mode<1||mode>3)return false;
  if(mode>=2 && (!smaa_pass_ || eye<0 || eye>1 || smaa_eyes_[eye].width!=widths_[eye] || smaa_eyes_[eye].height!=heights_[eye]))return false;
  if(!render_pass_||(mode==1&&!pipeline_)||eye<0||eye>1||!std::isfinite(gaze_x)||!std::isfinite(gaze_y)||std::abs(gaze_x)>1.2f||std::abs(gaze_y)>1.2f)return false;
  auto target=targets_.find(destinations_[eye]);if(target==targets_.end())return false;
  Focus focus;focus.texel[0]=1.f/widths_[eye];focus.texel[1]=1.f/heights_[eye];
  radius=internal::ClampGazeRadius(radius);
  focus.outer_radius=radius;focus.inner_radius=radius*(.18f/.22f);
  focus.center[0]=std::clamp(.5f+.5f*gaze_x,0.f,1.f);focus.center[1]=std::clamp(.5f-.5f*gaze_y,0.f,1.f);
  focus.decode_output=!Srgb(sources_[eye].format)&&Srgb(format_);focus.encode_output=Srgb(sources_[eye].format)&&!Srgb(format_);
  VkImageMemoryBarrier barriers[2]{};
  for(auto& b:barriers){b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
  barriers[0].image=sources_[eye].image;barriers[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barriers[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barriers[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  barriers[1].image=destinations_[eye];barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,2,barriers);
  const int x0=std::max(0,int(std::floor((focus.center[0]-radius)*widths_[eye])));
  const int y0=std::max(0,int(std::floor((focus.center[1]-radius)*heights_[eye])));
  const int x1=std::min(int(widths_[eye]),int(std::ceil((focus.center[0]+radius)*widths_[eye])));
  const int y1=std::min(int(heights_[eye]),int(std::ceil((focus.center[1]+radius)*heights_[eye])));
  VkRect2D scissor{{x0,y0},{std::uint32_t(x1-x0),std::uint32_t(y1-y0)}};
  if(mode>=2)RecordSmaa(command,eye,mode,&focus,scissor);
  else RecordPass(command,eye,pipeline_,render_pass_,target->second.framebuffer,descriptor_sets_[eye],&focus,scissor,false);
  // Rejoin the existing copy's layout contract; it owns the final transitions.
  std::swap(barriers[0].oldLayout,barriers[0].newLayout);barriers[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT;barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  std::swap(barriers[1].oldLayout,barriers[1].newLayout);barriers[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers);
  return true;
}

void GazeAa::ForgetImage(VkImage image) {
  if(!device_)return;
  for(auto& source:sources_)if(source.image==image){if(source.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,source.view,nullptr);source={};}
}
void GazeAa::Reset() {
  if(!device_||!get_proc_)return;
  DestroySmaa();
  for(const auto& item:targets_){Proc<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(device_,item.second.framebuffer,nullptr);Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,item.second.view,nullptr);}targets_.clear();
  for(auto& source:sources_){if(source.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,source.view,nullptr);source={};}
  if(pipeline_)Proc<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device_,pipeline_,nullptr);
  if(pipeline_layout_)Proc<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device_,pipeline_layout_,nullptr);
  if(descriptor_pool_)Proc<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device_,descriptor_pool_,nullptr);
  if(sampler_)Proc<PFN_vkDestroySampler>("vkDestroySampler")(device_,sampler_,nullptr);
  if(descriptor_layout_)Proc<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device_,descriptor_layout_,nullptr);
  if(render_pass_)Proc<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(device_,render_pass_,nullptr);
  device_={};get_proc_={};format_=VK_FORMAT_UNDEFINED;pipeline_={};pipeline_layout_={};descriptor_pool_={};sampler_={};descriptor_layout_={};render_pass_={};
  for(int i=0;i<2;++i){descriptor_sets_[i]={};destinations_[i]={};widths_[i]=heights_[i]=0;}
}
#include "gaze_smaa.inc"
}
