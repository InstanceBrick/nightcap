// Native SMAA 1x resources and three gaze-scissored passes. The upstream shader
// and LUTs are pinned in smaa/ with their MIT license and source revision.
bool GazeAa::MakeTexture(Texture& texture,VkFormat format,unsigned width,unsigned height,VkImageUsageFlags usage) {
  VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={width,height,1};info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;
  if(Proc<PFN_vkCreateImage>("vkCreateImage")(device_,&info,nullptr,&texture.image)!=VK_SUCCESS)return false;
  VkMemoryRequirements req{};Proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device_,texture.image,&req);
  unsigned type=0;while(type<32 && !(req.memoryTypeBits&(1u<<type)))++type;
  if(type==32)return false;
  VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;
  if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device_,&allocation,nullptr,&texture.memory)!=VK_SUCCESS || Proc<PFN_vkBindImageMemory>("vkBindImageMemory")(device_,texture.image,texture.memory,0)!=VK_SUCCESS)return false;
  VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=texture.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  return Proc<PFN_vkCreateImageView>("vkCreateImageView")(device_,&view,nullptr,&texture.view)==VK_SUCCESS;
}
void GazeAa::FreeTexture(Texture& texture) {
  if(texture.view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,texture.view,nullptr);
  if(texture.image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,texture.image,nullptr);
  if(texture.memory)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,texture.memory,nullptr);
  texture={};
}
bool GazeAa::PrepareSmaa(int eye,const VkPhysicalDeviceMemoryProperties& memory,int mode) {
  const auto fail=[&]{DestroySmaa();return false;};
  if(!smaa_pass_) {
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
    VkSubpassDependency dependency{};dependency.srcSubpass=0;dependency.dstSubpass=VK_SUBPASS_EXTERNAL;dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo pass{};pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&subpass;pass.dependencyCount=1;pass.pDependencies=&dependency;
    if(Proc<PFN_vkCreateRenderPass>("vkCreateRenderPass")(device_,&pass,nullptr,&smaa_pass_)!=VK_SUCCESS)return fail();
    if(!MakeTexture(smaa_luts_[0],VK_FORMAT_R8G8_UNORM,AREATEX_WIDTH,AREATEX_HEIGHT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT) || !MakeTexture(smaa_luts_[1],VK_FORMAT_R8_UNORM,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT))return fail();
    VkBufferCreateInfo buffer{};buffer.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;buffer.size=AREATEX_SIZE+SEARCHTEX_SIZE;buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(Proc<PFN_vkCreateBuffer>("vkCreateBuffer")(device_,&buffer,nullptr,&smaa_staging_)!=VK_SUCCESS)return fail();
    VkMemoryRequirements req{};Proc<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device_,smaa_staging_,&req);
    unsigned type=0;constexpr auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while(type<memory.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || (memory.memoryTypes[type].propertyFlags&flags)!=flags))++type;
    if(type==memory.memoryTypeCount)return fail();
    VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;
    if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device_,&allocation,nullptr,&smaa_staging_memory_)!=VK_SUCCESS || Proc<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device_,smaa_staging_,smaa_staging_memory_,0)!=VK_SUCCESS)return fail();
    void* mapped=nullptr;if(Proc<PFN_vkMapMemory>("vkMapMemory")(device_,smaa_staging_memory_,0,buffer.size,0,&mapped)!=VK_SUCCESS)return fail();
    std::memcpy(mapped,areaTexBytes,AREATEX_SIZE);std::memcpy(static_cast<char*>(mapped)+AREATEX_SIZE,searchTexBytes,SEARCHTEX_SIZE);Proc<PFN_vkUnmapMemory>("vkUnmapMemory")(device_,smaa_staging_memory_);
    std::fprintf(stderr,"  [vr-aa] SMAA resources and reference LUTs ready\n");
  }
  // Compile only the selected quality; other modes stay available on demand.
    const std::uint32_t* shaders[2][3]={{kGazeAaSmaaEdgeLow,kGazeAaSmaaWeightsLow,kGazeAaSmaaNeighborhood},{kGazeAaSmaaEdgeHigh,kGazeAaSmaaWeightsHigh,kGazeAaSmaaNeighborhood}};
    const std::size_t sizes[2][3]={{sizeof(kGazeAaSmaaEdgeLow),sizeof(kGazeAaSmaaWeightsLow),sizeof(kGazeAaSmaaNeighborhood)},{sizeof(kGazeAaSmaaEdgeHigh),sizeof(kGazeAaSmaaWeightsHigh),sizeof(kGazeAaSmaaNeighborhood)}};
    const int quality=mode==3?1:0;
    for(int part=0;part<3;++part) {
      if(smaa_pipelines_[quality][part])continue;
      smaa_pipelines_[quality][part]=MakePipeline(part==2?render_pass_:smaa_pass_,shaders[quality][part],sizes[quality][part]);
      if(!smaa_pipelines_[quality][part])return fail();
    }
  auto& data=smaa_eyes_[eye];
  if(data.width!=widths_[eye] || data.height!=heights_[eye]) {
    for(auto& framebuffer:data.framebuffers)if(framebuffer){Proc<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(device_,framebuffer,nullptr);framebuffer={};}
    for(auto& texture:data.textures)FreeTexture(texture);
    if(data.descriptors[0])Proc<PFN_vkFreeDescriptorSets>("vkFreeDescriptorSets")(device_,descriptor_pool_,2,data.descriptors);
    data={};
    for(int part=0;part<2;++part) {
      if(!MakeTexture(data.textures[part],VK_FORMAT_R8G8B8A8_UNORM,widths_[eye],heights_[eye],VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT))return fail();
      VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=smaa_pass_;fb.attachmentCount=1;fb.pAttachments=&data.textures[part].view;fb.width=widths_[eye];fb.height=heights_[eye];fb.layers=1;
      if(Proc<PFN_vkCreateFramebuffer>("vkCreateFramebuffer")(device_,&fb,nullptr,&data.framebuffers[part])!=VK_SUCCESS)return fail();
    }
    VkDescriptorSetLayout layouts[]={descriptor_layout_,descriptor_layout_};
    VkDescriptorSetAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate.descriptorPool=descriptor_pool_;allocate.descriptorSetCount=2;allocate.pSetLayouts=layouts;
    if(Proc<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(device_,&allocate,data.descriptors)!=VK_SUCCESS)return fail();
    data.width=widths_[eye];data.height=heights_[eye];
  }
  // Input and LUT views are stable across normal swapchain rotation. The
  // owning copy fence has completed before a changed source is rebound.
  if(data.descriptor_source==sources_[eye].view)return true;
  const VkImageView views[2][3]={{data.textures[0].view,smaa_luts_[0].view,smaa_luts_[1].view},{sources_[eye].view,data.textures[1].view,smaa_luts_[1].view}};
  VkDescriptorImageInfo images[6]{};VkWriteDescriptorSet writes[6]{};
  for(unsigned pass=0;pass<2;++pass)for(unsigned binding=0;binding<3;++binding){unsigned i=pass*3+binding;images[i]={sampler_,views[pass][binding],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=data.descriptors[pass];w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&images[i];}
  Proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device_,6,writes,0,nullptr);
  data.descriptor_source=sources_[eye].view;
  return true;
}
void GazeAa::RecordPass(VkCommandBuffer command,int eye,VkPipeline pipeline,VkRenderPass pass,VkFramebuffer framebuffer,VkDescriptorSet descriptors,const void* focus,const VkRect2D& region,bool clear) {
  VkClearValue zero{};
  VkRenderPassBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=pass;begin.framebuffer=framebuffer;begin.renderArea=region;begin.clearValueCount=clear?1:0;begin.pClearValues=clear?&zero:nullptr;
  Proc<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass")(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport{0,0,float(widths_[eye]),float(heights_[eye]),0,1};
  Proc<PFN_vkCmdSetViewport>("vkCmdSetViewport")(command,0,1,&viewport);Proc<PFN_vkCmdSetScissor>("vkCmdSetScissor")(command,0,1,&region);
  Proc<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
  Proc<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout_,0,1,&descriptors,0,nullptr);
  Proc<PFN_vkCmdPushConstants>("vkCmdPushConstants")(command,pipeline_layout_,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Focus),focus);
  Proc<PFN_vkCmdDraw>("vkCmdDraw")(command,3,1,0,0);Proc<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass")(command);
}
void GazeAa::RecordSmaa(VkCommandBuffer command,int eye,int mode,const void* focus,const VkRect2D& region) {
  if(!smaa_uploaded_) {
    VkImageMemoryBarrier barriers[2]{};
    for(int i=0;i<2;++i){auto& b=barriers[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=smaa_luts_[i].image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;}
    Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,barriers);
    for(int i=0;i<2;++i){VkBufferImageCopy copy{};copy.bufferOffset=i?AREATEX_SIZE:0;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={unsigned(i?SEARCHTEX_WIDTH:AREATEX_WIDTH),unsigned(i?SEARCHTEX_HEIGHT:AREATEX_HEIGHT),1};Proc<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(command,smaa_staging_,smaa_luts_[i].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);}
    for(auto& b:barriers){b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;}
    Proc<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,2,barriers);smaa_uploaded_=true;
  }
  const auto expand=[&](int margin){int x=std::max(0,region.offset.x-margin),y=std::max(0,region.offset.y-margin);int right=std::min(int(widths_[eye]),region.offset.x+int(region.extent.width)+margin),bottom=std::min(int(heights_[eye]),region.offset.y+int(region.extent.height)+margin);return VkRect2D{{x,y},{unsigned(right-x),unsigned(bottom-y)}};};
  const int quality=mode==3?1:0;auto& data=smaa_eyes_[eye];
  // Low performs four two-pixel search steps, with no diagonal or corner
  // search. Twelve pixels include those samples, the weight ROI and neighbours.
  // Keep the existing larger guard for High. Avoid shading an unnecessary
  // 64-pixel border in the default Low preset without changing its output.
  RecordPass(command,eye,smaa_pipelines_[quality][0],smaa_pass_,data.framebuffers[0],descriptor_sets_[eye],focus,expand(quality?64:12),true);
  RecordPass(command,eye,smaa_pipelines_[quality][1],smaa_pass_,data.framebuffers[1],data.descriptors[0],focus,expand(2),true);
  RecordPass(command,eye,smaa_pipelines_[quality][2],render_pass_,targets_.at(destinations_[eye]).framebuffer,data.descriptors[1],focus,region,false);
}
void GazeAa::DestroySmaa() {
  for(auto& eye:smaa_eyes_){for(auto framebuffer:eye.framebuffers)if(framebuffer)Proc<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(device_,framebuffer,nullptr);for(auto& texture:eye.textures)FreeTexture(texture);if(eye.descriptors[0])Proc<PFN_vkFreeDescriptorSets>("vkFreeDescriptorSets")(device_,descriptor_pool_,2,eye.descriptors);eye={};}
  for(auto& quality:smaa_pipelines_)for(auto& pipeline:quality){if(pipeline)Proc<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device_,pipeline,nullptr);pipeline={};}
  if(smaa_pass_)Proc<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(device_,smaa_pass_,nullptr);
  smaa_pass_={};
  for(auto& texture:smaa_luts_)FreeTexture(texture);
  if(smaa_staging_)Proc<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device_,smaa_staging_,nullptr);
  smaa_staging_={};
  if(smaa_staging_memory_)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,smaa_staging_memory_,nullptr);
  smaa_staging_memory_={};smaa_uploaded_=false;
}
