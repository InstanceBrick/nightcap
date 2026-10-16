#include "mocktail/vr/fsr3_gaze_transport.h"
#include "fsr3_gaze_spv.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace mocktail::vr {
namespace {
std::uint32_t Align16(std::uint32_t value){return (value+15u)&~15u;}
struct Axis{std::uint32_t input{},output{};};
bool BoundedCropGrid(std::uint32_t input,std::uint32_t output,float guard_pixels) {
  const auto step=output/std::gcd(input,output);
  return step<=64u && (step<=16u?step<=guard_pixels:step*.5f<=guard_pixels);
}
Axis CropAxis(std::uint32_t input,std::uint32_t output,std::uint32_t desired,float guard_pixels) {
  const auto divisor=std::gcd(input,output);
  const auto grid_output_step=output/divisor;
  const bool aligned=BoundedCropGrid(input,output,guard_pixels);
  // A 2160->3072 crop moves in 45 input / 64 output-pixel increments. Reserve
  // half a grid step at each edge so snapping cannot clip the gaze blend guard.
  if(aligned && grid_output_step>16u)desired+=grid_output_step;
  desired=std::min(desired,output);
  if(aligned && grid_output_step>16u) {
    const auto grid_steps=(desired+grid_output_step-1)/grid_output_step;
    return {grid_steps*(input/divisor),grid_steps*grid_output_step};
  }
  const auto input_step=input/divisor*16u,output_step=output/divisor*16u;
  const auto steps=(desired+output_step-1)/output_step;
  if(input_step<=input && output_step<=desired*3/2 && steps*input_step<=input)
    return {steps*input_step,steps*output_step};
  if(aligned) {
    // FSR dispatches partial workgroups; ROI dimensions need not be multiples
    // of 16. Preserve the exact full-eye ratio instead of rounding both axes.
    const auto grid_steps=(desired+grid_output_step-1)/grid_output_step;
    return {grid_steps*(input/divisor),grid_steps*grid_output_step};
  }
  const auto crop_input=std::min(input,Align16(static_cast<unsigned>(std::ceil(double(desired)*input/output))));
  return {crop_input,std::min(output,Align16(static_cast<unsigned>(std::ceil(double(crop_input)*output/input))))};
}
bool AlignedCropGrid(std::uint32_t input,std::uint32_t output,
    std::uint32_t crop_input,std::uint32_t crop_output,float guard_pixels) {
  return std::uint64_t(crop_input)*output==std::uint64_t(crop_output)*input &&
      BoundedCropGrid(input,output,guard_pixels);
}
std::int32_t CropOrigin(float gaze,std::uint32_t input,std::uint32_t output,
    std::uint32_t crop_input,std::uint32_t crop_output,float guard_pixels) {
  const auto maximum=static_cast<int>(input-crop_input);
  const double desired=double(gaze)*input-double(crop_input)*.5;
  const auto divisor=std::gcd(input,output);
  const auto input_step=input/divisor;
  // Keep reconstructed texel centres on the final image grid. Otherwise the
  // composite adds a gaze-dependent fractional-pixel bilinear blur to FSR.
  // The blend mask follows continuous gaze, independently of the snapped ROI.
  // Unsupported large custom ratios retain continuous crop movement.
  if(AlignedCropGrid(input,output,crop_input,crop_output,guard_pixels)) {
    const auto step=static_cast<int>(input_step);
    return std::clamp(static_cast<int>(std::lround(desired/step)),0,maximum/step)*step;
  }
  return std::clamp(static_cast<int>(std::lround(desired)),0,maximum);
}
VkImageAspectFlags Aspect(VkFormat format) {
  switch(format){
    case VK_FORMAT_D16_UNORM:case VK_FORMAT_D32_SFLOAT:case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT_S8_UINT:return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:return VK_IMAGE_ASPECT_COLOR_BIT;
  }
}
}

struct Fsr3GazeTransport::Impl {
  struct Resource {Fsr3Upscaler::Image image;VkDeviceMemory memory{};VkImageView view{};};
  struct ExternalView {VkImage image{};VkFormat format{};VkImageView view{};};
  struct Eye {
    Resource full,color,depth,motion,output,encoded;
    ExternalView input_depth,input_motion;
    VkDescriptorSet crop_set{},composite_set{};
    VkFramebuffer framebuffer{};
    std::int32_t origin[2]{};
    float gaze[2]{.5f,.5f};
    bool native_motion=false,reset=true,completed=false;
  };
  VkDevice device{};PFN_vkGetDeviceProcAddr proc{};
  VkPhysicalDeviceMemoryProperties memory;
  VkFormat source_format{},srgb_format{};
  std::uint32_t full_width{},full_height{},output_width{},output_height{};
  Dimensions crop;
  float radius=.15f,guard=.03f;
  VkDescriptorSetLayout crop_descriptor{},composite_descriptor{};
  VkDescriptorPool descriptor_pool{};
  VkPipelineLayout crop_layout{},composite_layout{};
  VkPipeline crop_pipeline{},composite_pipeline{};
  VkRenderPass render_pass{};VkSampler sampler{};
  Eye eyes[2];
  template<class T>T Proc(const char* name)const{return reinterpret_cast<T>(proc(device,name));}
  bool View(VkImage image,VkFormat format,VkImageView* view) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image=image;info.viewType=VK_IMAGE_VIEW_TYPE_2D;info.format=format;
    info.subresourceRange={Aspect(format),0,1,0,1};
    return Proc<PFN_vkCreateImageView>("vkCreateImageView")(device,&info,nullptr,view)==VK_SUCCESS;
  }
  bool Create(Resource& resource,VkFormat format,unsigned width,unsigned height,VkImageUsageFlags usage) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={width,height,1};
    info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.usage=usage;
    if(Proc<PFN_vkCreateImage>("vkCreateImage")(device,&info,nullptr,&resource.image.image)!=VK_SUCCESS)return false;
    VkMemoryRequirements requirements{};
    Proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device,resource.image.image,&requirements);
    unsigned type=0;
    while(type<memory.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type)) ||
        !(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))++type;
    if(type==memory.memoryTypeCount)return false;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
    if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device,&allocation,nullptr,&resource.memory)!=VK_SUCCESS ||
       Proc<PFN_vkBindImageMemory>("vkBindImageMemory")(device,resource.image.image,resource.memory,0)!=VK_SUCCESS ||
       !View(resource.image.image,format,&resource.view))return false;
    resource.image.format=format;resource.image.width=width;resource.image.height=height;
    return true;
  }
  void Destroy(Resource& resource) {
    if(resource.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device,resource.view,nullptr);
    if(resource.image.image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device,resource.image.image,nullptr);
    if(resource.memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device,resource.memory,nullptr);
    resource={};
  }
  bool External(ExternalView& view,const Fsr3Upscaler::Image& image) {
    if(view.image==image.image && view.format==image.format && view.view)return true;
    if(view.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device,view.view,nullptr);
    view={};
    if(!View(image.image,image.format,&view.view))return false;
    view.image=image.image;view.format=image.format;return true;
  }
  void Barrier(VkCommandBuffer command,VkImage image,VkImageLayout from,VkImageLayout to,
               VkAccessFlags source,VkAccessFlags destination) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image=image;barrier.oldLayout=from;barrier.newLayout=to;
    barrier.srcAccessMask=source;barrier.dstAccessMask=destination;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
  }
  VkShaderModule Shader(const std::uint32_t* data,std::size_t bytes) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=bytes;info.pCode=data;
    VkShaderModule shader{};
    if(Proc<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device,&info,nullptr,&shader)!=VK_SUCCESS)return {};
    return shader;
  }
  bool Pipelines() {
    VkDescriptorSetLayoutBinding crop_bindings[6]{};
    for(unsigned i=0;i<6;++i){crop_bindings[i]={i,i<3?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};}
    VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor.bindingCount=6;descriptor.pBindings=crop_bindings;
    if(Proc<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device,&descriptor,nullptr,&crop_descriptor)!=VK_SUCCESS)return false;
    VkDescriptorSetLayoutBinding composite_bindings[2]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    descriptor.bindingCount=2;descriptor.pBindings=composite_bindings;
    if(Proc<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device,&descriptor,nullptr,&composite_descriptor)!=VK_SUCCESS)return false;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,48};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=1;layout.pSetLayouts=&crop_descriptor;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
    if(Proc<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device,&layout,nullptr,&crop_layout)!=VK_SUCCESS)return false;
    push.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;push.size=64;layout.pSetLayouts=&composite_descriptor;
    if(Proc<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device,&layout,nullptr,&composite_layout)!=VK_SUCCESS)return false;
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,6},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,6},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=4;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
    if(Proc<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(device,&pool,nullptr,&descriptor_pool)!=VK_SUCCESS)return false;
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.minFilter=sampler_info.magFilter=VK_FILTER_LINEAR;
    sampler_info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU=sampler_info.addressModeV=sampler_info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(Proc<PFN_vkCreateSampler>("vkCreateSampler")(device,&sampler_info,nullptr,&sampler)!=VK_SUCCESS)return false;
    VkAttachmentDescription attachment{};attachment.format=srgb_format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
    VkSubpassDependency dependency{};dependency.srcSubpass=0;dependency.dstSubpass=VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&subpass;
    pass.dependencyCount=1;pass.pDependencies=&dependency;
    if(Proc<PFN_vkCreateRenderPass>("vkCreateRenderPass")(device,&pass,nullptr,&render_pass)!=VK_SUCCESS)return false;
    const VkShaderModule compute=Shader(kFsr3GazeCrop,sizeof(kFsr3GazeCrop));
    if(!compute)return false;
    VkComputePipelineCreateInfo compute_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    compute_info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,compute,"main",nullptr};
    compute_info.layout=crop_layout;
    auto result=Proc<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(device,{},1,&compute_info,nullptr,&crop_pipeline);
    Proc<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device,compute,nullptr);
    if(result!=VK_SUCCESS)return false;
    VkShaderModule shaders[2]={Shader(kFsr3GazeVertex,sizeof(kFsr3GazeVertex)),Shader(kFsr3GazeComposite,sizeof(kFsr3GazeComposite))};
    if(!shaders[0] || !shaders[1]){
      for(auto shader:shaders)if(shader)Proc<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device,shader,nullptr);
      return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(unsigned i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=shaders[i];stages[i].pName="main";}
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};blend_attachment.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&blend_attachment;
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;
    VkGraphicsPipelineCreateInfo graphics{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    graphics.stageCount=2;graphics.pStages=stages;graphics.pVertexInputState=&vertex;graphics.pInputAssemblyState=&assembly;
    graphics.pViewportState=&viewport;graphics.pRasterizationState=&raster;graphics.pMultisampleState=&multisample;
    graphics.pColorBlendState=&blend;graphics.pDynamicState=&dynamic;graphics.layout=composite_layout;graphics.renderPass=render_pass;
    result=Proc<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(device,{},1,&graphics,nullptr,&composite_pipeline);
    for(auto shader:shaders)Proc<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device,shader,nullptr);
    return result==VK_SUCCESS;
  }
  bool InitializeEye(Eye& eye) {
    const auto sampled=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
    if(!Create(eye.full,srgb_format,full_width,full_height,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT) ||
       !Create(eye.color,VK_FORMAT_R16G16B16A16_SFLOAT,crop.input_width,crop.input_height,sampled) ||
       !Create(eye.depth,VK_FORMAT_R32_SFLOAT,crop.input_width,crop.input_height,sampled) ||
       !Create(eye.motion,VK_FORMAT_R16G16_SFLOAT,crop.input_width,crop.input_height,sampled|VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
       !Create(eye.output,VK_FORMAT_R16G16B16A16_SFLOAT,crop.output_width,crop.output_height,sampled|VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
       !Create(eye.encoded,srgb_format,output_width,output_height,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT))return false;
    VkDescriptorSetLayout layouts[]={crop_descriptor,composite_descriptor};VkDescriptorSet sets[2]{};
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool=descriptor_pool;allocate.descriptorSetCount=2;allocate.pSetLayouts=layouts;
    if(Proc<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(device,&allocate,sets)!=VK_SUCCESS)return false;
    eye.crop_set=sets[0];eye.composite_set=sets[1];
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass=render_pass;framebuffer.attachmentCount=1;framebuffer.pAttachments=&eye.encoded.view;
    framebuffer.width=output_width;framebuffer.height=output_height;framebuffer.layers=1;
    if(Proc<PFN_vkCreateFramebuffer>("vkCreateFramebuffer")(device,&framebuffer,nullptr,&eye.framebuffer)!=VK_SUCCESS)return false;
    VkDescriptorImageInfo images[]={{sampler,eye.full.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler,eye.output.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[2]{};
    for(unsigned i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=eye.composite_set;
      writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&images[i];}
    Proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device,2,writes,0,nullptr);
    return true;
  }
  void Destroy() {
    if(!device || !proc)return;
    for(auto& eye:eyes){
      if(eye.framebuffer)Proc<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(device,eye.framebuffer,nullptr);
      for(auto* resource:{&eye.full,&eye.color,&eye.depth,&eye.motion,&eye.output,&eye.encoded})Destroy(*resource);
      for(auto* external:{&eye.input_depth,&eye.input_motion})
        if(external->view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device,external->view,nullptr);
      eye=Eye{};
    }
    for(auto pipeline:{crop_pipeline,composite_pipeline})if(pipeline)Proc<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device,pipeline,nullptr);
    for(auto layout:{crop_layout,composite_layout})if(layout)Proc<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device,layout,nullptr);
    if(render_pass)Proc<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(device,render_pass,nullptr);
    if(descriptor_pool)Proc<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device,descriptor_pool,nullptr);
    for(auto layout:{crop_descriptor,composite_descriptor})if(layout)Proc<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device,layout,nullptr);
    if(sampler)Proc<PFN_vkDestroySampler>("vkDestroySampler")(device,sampler,nullptr);
    device={};proc={};crop_pipeline=composite_pipeline={};crop_layout=composite_layout={};render_pass={};
    descriptor_pool={};crop_descriptor=composite_descriptor={};sampler={};
    source_format=srgb_format=VK_FORMAT_UNDEFINED;
    full_width=full_height=output_width=output_height=0;crop={};
  }
};

Fsr3GazeTransport::Fsr3GazeTransport():impl_(std::make_unique<Impl>()){}
Fsr3GazeTransport::~Fsr3GazeTransport(){Reset();}
void Fsr3GazeTransport::Reset(){impl_->Destroy();}
Fsr3GazeTransport::Dimensions Fsr3GazeTransport::dimensions()const{return impl_->crop;}
float Fsr3GazeTransport::vertical_fov_scale()const{return impl_->full_height?float(impl_->crop.input_height)/impl_->full_height:1.f;}
bool Fsr3GazeTransport::reset_history(int eye)const{return eye<0 || eye>1 || impl_->eyes[eye].reset;}
Fsr3Upscaler::Image Fsr3GazeTransport::color(int eye)const{return eye>=0&&eye<2?impl_->eyes[eye].color.image:Fsr3Upscaler::Image{};}
Fsr3Upscaler::Image Fsr3GazeTransport::depth(int eye)const{return eye>=0&&eye<2?impl_->eyes[eye].depth.image:Fsr3Upscaler::Image{};}
Fsr3Upscaler::Image Fsr3GazeTransport::motion(int eye)const{return eye>=0&&eye<2&&impl_->eyes[eye].native_motion?impl_->eyes[eye].motion.image:Fsr3Upscaler::Image{};}
Fsr3Upscaler::Image Fsr3GazeTransport::output(int eye)const{return eye>=0&&eye<2?impl_->eyes[eye].output.image:Fsr3Upscaler::Image{};}
VkImage Fsr3GazeTransport::encoded(int eye)const{return eye>=0&&eye<2?impl_->eyes[eye].encoded.image.image:VkImage{};}
VkFormat Fsr3GazeTransport::encoded_format()const{return impl_->srgb_format;}

bool Fsr3GazeTransport::Prepare(VkDevice device,PFN_vkGetDeviceProcAddr proc,
    const VkPhysicalDeviceMemoryProperties& memory,VkFormat format,unsigned width,unsigned height,
    unsigned out_width,unsigned out_height,float radius) {
  if(!device || !proc || !width || !height || out_width<width || out_height<height ||
     !std::isfinite(radius) || radius<=0 || radius>.5f)return false;
  VkFormat srgb{};
  if(format==VK_FORMAT_R8G8B8A8_UNORM || format==VK_FORMAT_R8G8B8A8_SRGB)srgb=VK_FORMAT_R8G8B8A8_SRGB;
  else if(format==VK_FORMAT_B8G8R8A8_UNORM || format==VK_FORMAT_B8G8R8A8_SRGB)srgb=VK_FORMAT_B8G8R8A8_SRGB;
  else return false;
  auto& state=*impl_;
  const auto minimum=std::min(out_width,out_height);
  const float guard=std::max(32.f/minimum,radius*.2f);
  const auto desired=std::max(480u,Align16(static_cast<unsigned>(std::ceil(2*(radius+guard)*minimum+16))));
  const float guard_pixels=guard*minimum;
  const auto x=CropAxis(width,out_width,desired,guard_pixels),y=CropAxis(height,out_height,desired,guard_pixels);
  if(state.device==device && state.source_format==format && state.full_width==width && state.full_height==height &&
      state.output_width==out_width && state.output_height==out_height && state.crop.input_width==x.input &&
      state.crop.input_height==y.input && state.crop.output_width==x.output && state.crop.output_height==y.output &&
      state.eyes[1].encoded.image.image){state.radius=radius;state.guard=guard;return true;}
  if(state.device)return false;
  state.device=device;state.proc=proc;state.memory=memory;state.source_format=format;state.srgb_format=srgb;
  state.full_width=width;state.full_height=height;state.output_width=out_width;state.output_height=out_height;
  state.radius=radius;state.guard=guard;state.crop={x.input,y.input,x.output,y.output};
  if(!state.Pipelines() || !state.InitializeEye(state.eyes[0]) || !state.InitializeEye(state.eyes[1])){Reset();return false;}
  return true;
}

bool Fsr3GazeTransport::Begin(VkCommandBuffer command,int index,const BeginInfo& info) {
  auto& state=*impl_;
  if(!state.device || !command || index<0 || index>1 || !info.encoded_source || !info.depth.image ||
     info.depth.width!=state.full_width || info.depth.height!=state.full_height ||
     (info.motion.image && (info.motion.width!=state.full_width || info.motion.height!=state.full_height)) ||
     !std::isfinite(info.gaze_x) || !std::isfinite(info.gaze_y))return false;
  auto& eye=state.eyes[index];
  if(!state.External(eye.input_depth,info.depth) || (info.motion.image && !state.External(eye.input_motion,info.motion)))return false;
  eye.gaze[0]=std::clamp(info.gaze_x,0.f,1.f);eye.gaze[1]=std::clamp(info.gaze_y,0.f,1.f);
  const std::int32_t origin[2]={
      CropOrigin(eye.gaze[0],state.full_width,state.output_width,state.crop.input_width,state.crop.output_width,
          state.guard*std::min(state.output_width,state.output_height)),
      CropOrigin(eye.gaze[1],state.full_height,state.output_height,state.crop.input_height,state.crop.output_height,
          state.guard*std::min(state.output_width,state.output_height))};
  const auto dx=origin[0]-eye.origin[0],dy=origin[1]-eye.origin[1];
  eye.reset=info.reset || !eye.completed || std::abs(dx)>int(state.crop.input_width/4) || std::abs(dy)>int(state.crop.input_height/4);
  eye.native_motion=info.motion.image!=VK_NULL_HANDLE;
  VkDescriptorImageInfo images[6]={{VK_NULL_HANDLE,eye.full.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
      {VK_NULL_HANDLE,eye.input_depth.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
      {VK_NULL_HANDLE,eye.native_motion?eye.input_motion.view:eye.input_depth.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
      {VK_NULL_HANDLE,eye.color.view,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,eye.depth.view,VK_IMAGE_LAYOUT_GENERAL},
      {VK_NULL_HANDLE,eye.motion.view,VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet writes[6]{};
  for(unsigned i=0;i<6;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=eye.crop_set;
    writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i<3?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[i].pImageInfo=&images[i];}
  state.Proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(state.device,6,writes,0,nullptr);
  state.Barrier(command,eye.full.image.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={state.full_width,state.full_height,1};
  state.Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,info.encoded_source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      eye.full.image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  state.Barrier(command,eye.full.image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  for(auto* resource:{&eye.color,&eye.depth,&eye.motion,&eye.output})
    state.Barrier(command,resource->image.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
  struct Constants{std::int32_t full[2],crop[2],origin[2],previous[2];std::uint32_t native,reserved[3];};
  const Constants constants{{int(state.full_width),int(state.full_height)},
      {int(state.crop.input_width),int(state.crop.input_height)},
      {origin[0],origin[1]},
      {eye.completed?eye.origin[0]:origin[0],eye.completed?eye.origin[1]:origin[1]},eye.native_motion,{0,0,0}};
  static_assert(sizeof(Constants)==48);
  state.Proc<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_COMPUTE,state.crop_pipeline);
  state.Proc<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_COMPUTE,state.crop_layout,
      0,1,&eye.crop_set,0,nullptr);
  state.Proc<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command,state.crop_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(constants),&constants);
  state.Proc<PFN_vkCmdDispatch>("vkCmdDispatch")(command,(state.crop.input_width+7)/8,(state.crop.input_height+7)/8,1);
  for(auto* resource:{&eye.color,&eye.depth,&eye.motion})
    state.Barrier(command,resource->image.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  eye.origin[0]=origin[0];eye.origin[1]=origin[1];eye.completed=false;
  return true;
}

void Fsr3GazeTransport::Finish(VkCommandBuffer command,int index) {
  auto& state=*impl_;
  if(!state.device || !command || index<0 || index>1)return;
  auto& eye=state.eyes[index];
  state.Barrier(command,eye.output.image.image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
  VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass=state.render_pass;pass.framebuffer=eye.framebuffer;pass.renderArea={{0,0},{state.output_width,state.output_height}};
  state.Proc<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass")(command,&pass,VK_SUBPASS_CONTENTS_INLINE);
  state.Proc<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,state.composite_pipeline);
  state.Proc<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,state.composite_layout,
      0,1,&eye.composite_set,0,nullptr);
  struct Constants{float full[2],output[2],origin[2],crop[2],gaze[2],radius,guard;
      std::uint32_t aligned_grid,reserved[3];};
  const Constants constants{{float(state.full_width),float(state.full_height)},
      {float(state.output_width),float(state.output_height)},
      {float(eye.origin[0]),float(eye.origin[1])},
      {float(state.crop.input_width),float(state.crop.input_height)},
      {eye.gaze[0],eye.gaze[1]},state.radius,state.guard,
      AlignedCropGrid(state.full_width,state.output_width,state.crop.input_width,state.crop.output_width,
          state.guard*std::min(state.output_width,state.output_height)) &&
      AlignedCropGrid(state.full_height,state.output_height,state.crop.input_height,state.crop.output_height,
          state.guard*std::min(state.output_width,state.output_height)),{0,0,0}};
  static_assert(sizeof(Constants)==64);
  state.Proc<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command,state.composite_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(constants),&constants);
  VkViewport viewport{0,0,float(state.output_width),float(state.output_height),0,1};
  VkRect2D scissor{{0,0},{state.output_width,state.output_height}};
  state.Proc<PFN_vkCmdSetViewport>("vkCmdSetViewport")(command,0,1,&viewport);
  state.Proc<PFN_vkCmdSetScissor>("vkCmdSetScissor")(command,0,1,&scissor);
  state.Proc<PFN_vkCmdDraw>("vkCmdDraw")(command,3,1,0,0);
  state.Proc<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass")(command);
  eye.completed=true;
}
}
