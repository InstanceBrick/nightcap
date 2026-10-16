bool FsrUpscaler::PrepareHistory(const VkPhysicalDeviceMemoryProperties& memory,int eye) {
  auto& e=eyes_[eye];
  if(e.history_image)return true;
  if(e.history_failed)return false;
  e.history_failed=true;
  if(!pipelines_[2])pipelines_[2]=Pipeline(passes_[1],kTemporal,sizeof(kTemporal));
  if(!pipelines_[2])return false;
  VkImage image{};VkDeviceMemory allocation{};VkImageView view{};
  const auto fail=[&]{
    if(view)Proc<PFN_vkDestroyImageView>("vkDestroyImageView")(device_,view,nullptr);
    if(image)Proc<PFN_vkDestroyImage>("vkDestroyImage")(device_,image,nullptr);
    if(allocation)Proc<PFN_vkFreeMemory>("vkFreeMemory")(device_,allocation,nullptr);
    std::fprintf(stderr,"  [vr-temporal] history unavailable; spatial FSR retained\n");
    return false;
  };
  VkImageCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ci.imageType=VK_IMAGE_TYPE_2D;ci.format=EncodedFormat(format_);ci.extent={e.width,e.height,1};
  ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
  ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if(Proc<PFN_vkCreateImage>("vkCreateImage")(device_,&ci,nullptr,&image)!=VK_SUCCESS)return fail();
  VkMemoryRequirements req{};Proc<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device_,image,&req);
  unsigned type=0;
  while(type<memory.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || !(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))++type;
  if(type==memory.memoryTypeCount)return fail();
  VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=req.size;ai.memoryTypeIndex=type;
  if(Proc<PFN_vkAllocateMemory>("vkAllocateMemory")(device_,&ai,nullptr,&allocation)!=VK_SUCCESS ||
     Proc<PFN_vkBindImageMemory>("vkBindImageMemory")(device_,image,allocation,0)!=VK_SUCCESS)return fail();
  VkImageViewCreateInfo vi{};vi.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ci.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  if(Proc<PFN_vkCreateImageView>("vkCreateImageView")(device_,&vi,nullptr,&view)!=VK_SUCCESS)return fail();
  VkDescriptorImageInfo info[2]={{sampler_,e.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                               {sampler_,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
  VkWriteDescriptorSet writes[2]{};
  for(unsigned i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=e.sets[2];writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=info+i;}
  Proc<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(device_,2,writes,0,nullptr);
  e.history_image=image;e.history_memory=allocation;e.history_view=view;e.history_failed=false;
  std::fprintf(stderr,"  [vr-temporal] eye%d history ready %ux%u\n",eye,e.width,e.height);
  return true;
}

void FsrUpscaler::RecordTemporal(VkCommandBuffer command,int eye,
    const TemporalEyePose& pose,float gaze_x,float gaze_y,float radius,std::uint64_t now) {
  if(eye<0 || eye>1 || !std::isfinite(gaze_x) || !std::isfinite(gaze_y))return;
  auto& e=eyes_[eye];const auto target=targets_.find(e.destination);
  if(!e.history_image || !pipelines_[2] || target==targets_.end())return;
  TemporalConstants pc{};pc.texel[0]=1.f/e.width;pc.texel[1]=1.f/e.height;
  pc.center[0]=std::clamp(.5f+.5f*gaze_x,0.f,1.f);pc.center[1]=std::clamp(.5f-.5f*gaze_y,0.f,1.f);
  pc.radius=internal::ClampGazeRadius(radius);pc.previous_radius=e.previous_radius;
  pc.previous_center[0]=e.previous_center[0];pc.previous_center[1]=e.previous_center[1];
  pc.blend=.20f;pc.output_srgb=Srgb(format_);
  const bool valid=e.history_initialized && now>e.previous_time && now-e.previous_time<100'000'000ULL &&
                   TemporalReprojection(pose,e.previous_pose,pc.rows);
  const int x0=std::max(0,int(std::floor((pc.center[0]-pc.radius)*e.width))),y0=std::max(0,int(std::floor((pc.center[1]-pc.radius)*e.height)));
  const int x1=std::min(int(e.width),int(std::ceil((pc.center[0]+pc.radius)*e.width))),y1=std::min(int(e.height),int(std::ceil((pc.center[1]+pc.radius)*e.height)));
  const int left=std::max(0,x0-2),top=std::max(0,y0-2),right=std::min(int(e.width),x1+2),bottom=std::min(int(e.height),y1+2);
  Barrier(command,e.destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  Barrier(command,e.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.srcOffset=copy.dstOffset={left,top,0};copy.extent={unsigned(right-left),unsigned(bottom-top),1};
  Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,e.destination,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,e.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  if(valid) {
    Barrier(command,e.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    Barrier(command,e.destination,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    Draw(command,passes_[1],target->second.framebuffer,pipelines_[2],e.sets[2],&pc,e,{{x0,y0},{unsigned(x1-x0),unsigned(y1-y0)}});
    Barrier(command,e.destination,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    Barrier(command,e.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  } else {
    Barrier(command,e.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    Barrier(command,e.destination,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  }
  Barrier(command,e.history_image,e.history_initialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,e.history_initialized?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
  // Save the unsharpened, spatial frame. Temporal filtering never feeds itself
  // or RCAS back into history, so errors cannot accumulate over many frames.
  Proc<PFN_vkCmdCopyImage>("vkCmdCopyImage")(command,e.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,e.history_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  Barrier(command,e.history_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  e.previous_pose=pose;e.previous_time=now;e.previous_radius=pc.radius;
  e.previous_center[0]=pc.center[0];e.previous_center[1]=pc.center[1];e.history_initialized=true;
}
