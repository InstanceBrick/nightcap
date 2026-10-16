// Experimental native FDM attachment bridge. Included in the loader adapter's
// private namespace so all calls bypass guest interposition. Inert unless the
// backend has opted in and successfully created runtime-owned density maps.
struct FovRenderPass {
  VkDevice device; std::uint32_t attachments; unsigned samples=1; bool depth=false;
  unsigned subpasses=0,colors=0;VkFormat color_format=VK_FORMAT_UNDEFINED;
  VkFormat depth_format=VK_FORMAT_UNDEFINED;bool attached=false;
  mocktail::vr::FoveationTilePassIdentity tile_identity{};
};
struct FovFramebuffer {
  VkDevice device;
  VkFramebufferCreateInfo info;
  std::vector<VkImageView> attachments;
  struct Clone { VkImageView view; VkRenderPass pass; VkFramebuffer framebuffer; };
  std::vector<Clone> clones;
};
struct FovImage { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; };
struct FovDevice : FovImage {
  using FocusKey=mocktail::vr::internal::GazeDensityMapKey;
  FovImage eyes[2];
  VkBuffer staging{};VkDeviceMemory staging_memory{};void* mapped=nullptr;
  VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};VkQueue queue{};
  unsigned width=0,height=0;
  VkExtent2D min_density_region{},max_density_region{};
  bool framebuffer_density_mapping=false;
  mocktail::vr::FoveationTileDriverIdentity tile_driver{};
  std::string driver_name,driver_info;
  bool density_offsets_enabled=true;
  std::uint64_t frames[2]{};bool initialized[2]{};bool gaze_ready=false;
  bool host_copy=false;
  std::vector<std::uint8_t> uploaded[2],scratch_pixels[2];
  std::vector<std::uint16_t> scratch_densities[2];
  FocusKey focus_keys[2];bool focus_cached[2]{};
  float timestamp_period=0;
  unsigned timestamp_bits=0;bool statistics=false;
};
struct FovPassTiming {
  VkDevice device{};VkQueryPool pool{},statistics_pool{};VkFramebuffer framebuffer{};
  VkCommandBuffer command{};VkQueue submitted_queue{};bool ended=false;
  unsigned width=0,height=0,samples=1,visit=0;bool depth=false,foveated=false,reported=false;
  int eye=-1;void* owner=nullptr;void* guest_framebuffer=nullptr;
  VkFormat color_format=VK_FORMAT_UNDEFINED;
};
struct FovAdapterState {
  std::mutex mutex;
  std::unordered_map<VkDevice,FovDevice> devices;
  std::unordered_map<VkRenderPass,FovRenderPass> passes;
  std::unordered_map<VkFramebuffer,FovFramebuffer> framebuffers;
  std::uint64_t selected = 0;
  std::uint64_t msaa_selected = 0;
  std::vector<FovPassTiming> timings;
  std::unordered_map<VkCommandBuffer,std::size_t> active_timings;
  std::unordered_map<VkFramebuffer,unsigned> timing_visits;
};
FovAdapterState& FovState() { static FovAdapterState state; return state; }
template<class T> T FovProc(VkDevice device, const char* name) {
  return reinterpret_cast<T>(HostDeviceProc(device,name));
}
bool FovTraceEnabled() {
  static const bool enabled=[] {const char* value=std::getenv("MOCKTAIL_VR_TRACE_PASSES");return value&&value[0]=='1';}();
  return enabled;
}
bool FovSceneEnabled() {
  static const bool initial=[] {const char* value=std::getenv("MOCKTAIL_VR_SCENE_FOVEATION");return !value || std::strcmp(value,"0")!=0;}();
  static const bool benchmark=[] {const char* value=std::getenv("MOCKTAIL_VR_FOVEATION_BENCHMARK");return value && value[0]=='1';}();
  if(!FovTraceEnabled() || !benchmark)return initial;
  // Diagnostic-only A/B control in a single running scene; never read by the
  // normal launcher. Avoid comparing unrelated multiplayer server instances.
  static std::mutex mutex;std::lock_guard<std::mutex> lock(mutex);
  static bool value=initial;static std::uint64_t checked=0;
  const auto now=MonotonicNanos();
  if(now-checked>1000000000ull) {
    checked=now;char mode=0;std::ifstream file("/data/tools/scene-foveation-mode");
    if(file>>mode && (mode=='0' || mode=='1'))value=mode=='1';
  }
  return value;
}
// Called only after the existing eye-copy fence completed this queue. Never
// read newly created/unsubmitted queries before their recorded reset ran.
void FovTraceComplete(VkDevice device,VkQueue queue) {
  if(!FovTraceEnabled())return;
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  const auto dev=state.devices.find(device);if(dev==state.devices.end())return;
  auto results=FovProc<PFN_vkGetQueryPoolResults>(device,"vkGetQueryPoolResults");
  for(auto& timing:state.timings)if(timing.device==device && timing.submitted_queue==queue && timing.ended && !timing.reported) {
    std::uint64_t values[4]{};
    const auto ready=results(device,timing.pool,0,2,sizeof(values),values,sizeof(std::uint64_t)*2,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if(ready==VK_SUCCESS && values[1] && values[3]) {
      const auto mask=dev->second.timestamp_bits>=64?UINT64_MAX:((1ull<<dev->second.timestamp_bits)-1);
      const double ms=double((values[2]-values[0])&mask)*dev->second.timestamp_period/1e6;
      std::fprintf(stderr,"  [vr-pass-gpu] framebuffer=%p extent=%ux%u samples=%u depth=%d format=%d eye=%d owner=%p guest_fb=%p foveated=%d visit=%u gpu_ms=%.3f\n",reinterpret_cast<void*>(timing.framebuffer),timing.width,timing.height,timing.samples,timing.depth,int(timing.color_format),timing.eye,timing.owner,timing.guest_framebuffer,timing.foveated,timing.visit,ms);
      if(timing.statistics_pool) {
        std::uint64_t statistics[2]{};
        const auto statistics_ready=results(device,timing.statistics_pool,0,1,sizeof(statistics),statistics,sizeof(statistics),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if(statistics_ready==VK_SUCCESS && statistics[1])std::fprintf(stderr,"  [vr-pass-shading] framebuffer=%p extent=%ux%u depth=%d eye=%d foveated=%d visit=%u fragment_invocations=%llu\n",reinterpret_cast<void*>(timing.framebuffer),timing.width,timing.height,timing.depth,timing.eye,timing.foveated,timing.visit,static_cast<unsigned long long>(statistics[0]));
      }
      timing.reported=true;
    }
  }
}
void FovTraceSubmittedCommand(VkQueue queue,VkCommandBuffer command) {
  if(!FovTraceEnabled())return;
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  for(auto& timing:state.timings)if(timing.command==command && timing.ended && !timing.submitted_queue && !timing.reported)timing.submitted_queue=queue;
}
void FovTraceReset(VkCommandBuffer command) {
  if(!FovTraceEnabled())return;
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  for(auto& timing:state.timings)if(timing.command==command && !timing.submitted_queue)timing.reported=true;
  state.active_timings.erase(command);
}
void FovTraceSubmitted(VkQueue queue,unsigned count,const VkSubmitInfo* submits) {
  if(!FovTraceEnabled() || !submits)return;
  for(unsigned i=0;i<count;++i)for(unsigned j=0;j<submits[i].commandBufferCount;++j)FovTraceSubmittedCommand(queue,submits[i].pCommandBuffers[j]);
}
void FovTraceSubmitted(VkQueue queue,unsigned count,const VkSubmitInfo2* submits) {
  if(!FovTraceEnabled() || !submits)return;
  for(unsigned i=0;i<count;++i)for(unsigned j=0;j<submits[i].commandBufferInfoCount;++j)FovTraceSubmittedCommand(queue,submits[i].pCommandBufferInfos[j].commandBuffer);
}
void FovTraceBegin(VkCommandBuffer command,VkDevice device,const VkRenderPassBeginInfo* begin,bool foveated,bool inline_draws) {
  if(!FovTraceEnabled() || !begin)return;
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  const auto dev=state.devices.find(device);
  if(dev==state.devices.end() || !dev->second.timestamp_bits || dev->second.timestamp_period<=0)return;
  if(state.active_timings.count(command))return;
  const auto fb=state.framebuffers.find(begin->framebuffer);
  const auto pass=state.passes.find(begin->renderPass);
  if(fb==state.framebuffers.end() || pass==state.passes.end())return;
  static const bool scene_only=[] {const char* value=std::getenv("MOCKTAIL_VR_TRACE_SCENE_ONLY");return value && value[0]=='1';}();
  if(state.timings.size()>=4096u)return;
  if(scene_only && (!pass->second.depth || fb->second.info.width<1000 || fb->second.info.height<1000))return;
  const auto visit=++state.timing_visits[begin->framebuffer];
  // Warm the shaders/attachments first; retain only four measurements per
  // framebuffer. Query results are polled without WAIT and never stall frames.
  const unsigned cycle=visit%300;
  if(cycle<=6 || cycle>10)return;
  FovPassTiming timing;timing.command=command;timing.device=device;timing.framebuffer=begin->framebuffer;timing.width=fb->second.info.width;timing.height=fb->second.info.height;timing.samples=pass->second.samples;timing.depth=pass->second.depth;timing.foveated=foveated;
  timing.visit=visit;
  static const auto scope=ResolveProcessFunction<bool(*)(void**,int*,void**)>("mocktail_vr_current_eye");
  if(scope)(void)scope(&timing.owner,&timing.eye,&timing.guest_framebuffer);
  timing.color_format=pass->second.color_format;
  VkQueryPoolCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=2;
  if(FovProc<PFN_vkCreateQueryPool>(device,"vkCreateQueryPool")(device,&info,nullptr,&timing.pool)!=VK_SUCCESS)return;
  if(dev->second.statistics && inline_draws && pass->second.subpasses==1) {
    info.queryType=VK_QUERY_TYPE_PIPELINE_STATISTICS;info.queryCount=1;
    info.pipelineStatistics=VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
    if(FovProc<PFN_vkCreateQueryPool>(device,"vkCreateQueryPool")(device,&info,nullptr,&timing.statistics_pool)==VK_SUCCESS) {
      FovProc<PFN_vkCmdResetQueryPool>(device,"vkCmdResetQueryPool")(command,timing.statistics_pool,0,1);
    }
  }
  FovProc<PFN_vkCmdResetQueryPool>(device,"vkCmdResetQueryPool")(command,timing.pool,0,2);
  FovProc<PFN_vkCmdWriteTimestamp>(device,"vkCmdWriteTimestamp")(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timing.pool,0);
  state.active_timings[command]=state.timings.size();state.timings.push_back(timing);
}
void FovTraceEnd(VkCommandBuffer command,VkDevice device) {
  if(!FovTraceEnabled())return;
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  const auto active=state.active_timings.find(command);if(active==state.active_timings.end())return;
  auto& timing=state.timings[active->second];
  FovProc<PFN_vkCmdWriteTimestamp>(device,"vkCmdWriteTimestamp")(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timing.pool,1);
  timing.ended=true;state.active_timings.erase(active);
}
// Place statistics inside the pass so driver load/store reconstruction is
// not mistaken for the guest's fragment shader work. Timestamps still enclose
// the entire pass, including that cost.
void FovTraceDrawScope(VkCommandBuffer command,VkDevice device,bool begin) {
  if(!FovTraceEnabled())return;
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  const auto active=state.active_timings.find(command);if(active==state.active_timings.end())return;
  const auto pool=state.timings[active->second].statistics_pool;if(!pool)return;
  if(begin)FovProc<PFN_vkCmdBeginQuery>(device,"vkCmdBeginQuery")(command,pool,0,0);
  else FovProc<PFN_vkCmdEndQuery>(device,"vkCmdEndQuery")(command,pool,0);
}
void FovFreeNeutral(VkDevice device, FovImage& data) {
  if(data.view) FovProc<PFN_vkDestroyImageView>(device,"vkDestroyImageView")(device,data.view,nullptr);
  if(data.image) FovProc<PFN_vkDestroyImage>(device,"vkDestroyImage")(device,data.image,nullptr);
  if(data.memory) FovProc<PFN_vkFreeMemory>(device,"vkFreeMemory")(device,data.memory,nullptr);
  data={};
}
void FovFreeGaze(VkDevice device,FovDevice& data) {
  for(auto& image:data.eyes)FovFreeNeutral(device,image);
  if(data.mapped)FovProc<PFN_vkUnmapMemory>(device,"vkUnmapMemory")(device,data.staging_memory);
  if(data.staging)FovProc<PFN_vkDestroyBuffer>(device,"vkDestroyBuffer")(device,data.staging,nullptr);
  if(data.staging_memory)FovProc<PFN_vkFreeMemory>(device,"vkFreeMemory")(device,data.staging_memory,nullptr);
  if(data.fence)FovProc<PFN_vkDestroyFence>(device,"vkDestroyFence")(device,data.fence,nullptr);
  if(data.pool)FovProc<PFN_vkDestroyCommandPool>(device,"vkDestroyCommandPool")(device,data.pool,nullptr);
}
bool FovInitializeGaze(VkDevice device,const VkPhysicalDeviceMemoryProperties& memory,unsigned family,FovDevice& data) {
  static const auto extent=ResolveProcessFunction<VkExtent2D(*)(VkDevice)>("mocktail_vr_xr_foveation_extent");
  const auto size=extent?extent(device):VkExtent2D{};
  if(!size.width || !size.height || size.width>1024 || size.height>1024)return false;
  data.width=size.width;data.height=size.height;
  static const auto host_enabled=ResolveProcessFunction<bool(*)(VkDevice)>("mocktail_vr_xr_foveation_host_copy_enabled");
  data.host_copy=host_enabled && host_enabled(device) &&
    FovProc<PFN_vkTransitionImageLayoutEXT>(device,"vkTransitionImageLayoutEXT") &&
    FovProc<PFN_vkCopyMemoryToImageEXT>(device,"vkCopyMemoryToImageEXT");
  const auto allocate=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags,VkDeviceMemory* result) {
    unsigned type=0;while(type<memory.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) || (memory.memoryTypes[type].propertyFlags&flags)!=flags))++type;
    if(type==memory.memoryTypeCount)return false;
    VkMemoryAllocateInfo info{};info.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;info.allocationSize=req.size;info.memoryTypeIndex=type;
    return FovProc<PFN_vkAllocateMemory>(device,"vkAllocateMemory")(device,&info,nullptr,result)==VK_SUCCESS;
  };
  for(auto& eye:data.eyes) {
    VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R16G16_SFLOAT;image.extent={size.width,size.height,1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(data.host_copy)image.usage|=VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT;
    auto created=FovProc<PFN_vkCreateImage>(device,"vkCreateImage")(device,&image,nullptr,&eye.image);
    if(created!=VK_SUCCESS && data.host_copy) {
      data.host_copy=false;image.usage&=~VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT;
      created=FovProc<PFN_vkCreateImage>(device,"vkCreateImage")(device,&image,nullptr,&eye.image);
    }
    if(created!=VK_SUCCESS)return false;
    VkMemoryRequirements req{};FovProc<PFN_vkGetImageMemoryRequirements>(device,"vkGetImageMemoryRequirements")(device,eye.image,&req);
    if(!allocate(req,0,&eye.memory) || FovProc<PFN_vkBindImageMemory>(device,"vkBindImageMemory")(device,eye.image,eye.memory,0)!=VK_SUCCESS)return false;
    VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=eye.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    if(FovProc<PFN_vkCreateImageView>(device,"vkCreateImageView")(device,&view,nullptr,&eye.view)!=VK_SUCCESS)return false;
  }
  VkBufferCreateInfo buffer{};buffer.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;buffer.size=VkDeviceSize(size.width)*size.height*4;buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if(FovProc<PFN_vkCreateBuffer>(device,"vkCreateBuffer")(device,&buffer,nullptr,&data.staging)!=VK_SUCCESS)return false;
  VkMemoryRequirements req{};FovProc<PFN_vkGetBufferMemoryRequirements>(device,"vkGetBufferMemoryRequirements")(device,data.staging,&req);
  if(!allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&data.staging_memory) || FovProc<PFN_vkBindBufferMemory>(device,"vkBindBufferMemory")(device,data.staging,data.staging_memory,0)!=VK_SUCCESS || FovProc<PFN_vkMapMemory>(device,"vkMapMemory")(device,data.staging_memory,0,VK_WHOLE_SIZE,0,&data.mapped)!=VK_SUCCESS)return false;
  VkCommandPoolCreateInfo cp{};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.queueFamilyIndex=family;cp.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  if(FovProc<PFN_vkCreateCommandPool>(device,"vkCreateCommandPool")(device,&cp,nullptr,&data.pool)!=VK_SUCCESS)return false;
  VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=data.pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
  if(FovProc<PFN_vkAllocateCommandBuffers>(device,"vkAllocateCommandBuffers")(device,&ca,&data.command)!=VK_SUCCESS)return false;
  VkFenceCreateInfo fc{};fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if(FovProc<PFN_vkCreateFence>(device,"vkCreateFence")(device,&fc,nullptr,&data.fence)!=VK_SUCCESS)return false;
  FovProc<PFN_vkGetDeviceQueue>(device,"vkGetDeviceQueue")(device,family,0,&data.queue);
  return data.queue!=VK_NULL_HANDLE;
}
VkExtent2D FovGazeRegionBound(const FovDevice& data,const FovRenderPass& pass,
    const VkFramebufferCreateInfo& framebuffer) {
  if(framebuffer.pNext || framebuffer.flags || framebuffer.layers!=1)return data.max_density_region;
  auto driver=data.tile_driver;driver.driver_name=data.driver_name;driver.driver_info=data.driver_info;
  auto identity=pass.tile_identity;
  identity.width=framebuffer.width;identity.height=framebuffer.height;
  identity.framebuffer_layers=framebuffer.layers;
  identity.density_offsets_enabled=data.density_offsets_enabled;
  const auto known=mocktail::vr::KnownFoveationTileBound(driver,identity);
  return known?VkExtent2D{known->width,known->height}:data.max_density_region;
}
VkImageView FovUploadGaze(VkDevice device,FovDevice& data,const FovRenderPass& pass,
    VkExtent2D region_bound,int eye,std::uint64_t frame,float x,float y,int level,float radius,unsigned eye_width,unsigned eye_height) {
  if(!data.gaze_ready || eye<0 || eye>1 || !eye_width || !eye_height || !std::isfinite(x) || !std::isfinite(y))return VK_NULL_HANDLE;
  const auto layout=mocktail::vr::internal::TurnipDensityLayout(data.width,data.height,
      eye_width,eye_height,data.min_density_region.width,data.min_density_region.height,
      data.max_density_region.width,data.max_density_region.height);
  if(data.framebuffer_density_mapping && !layout.valid)return VK_NULL_HANDLE;
  const auto center=data.framebuffer_density_mapping?
      mocktail::vr::internal::QuantizeFramebufferGaze(layout,x,y):
      mocktail::vr::internal::QuantizeGazeCenter(data.width,data.height,x,y);
  FovDevice::FocusKey key{center.x,center.y,eye_width,eye_height,data.width,data.height,
      data.framebuffer_density_mapping?layout.texel_width:0,
      data.framebuffer_density_mapping?layout.texel_height:0,
      level,mocktail::vr::internal::ClampGazeRadius(radius)};
  key.guard_width=region_bound.width;key.guard_height=region_bound.height;
  key.color_format=unsigned(pass.color_format);key.depth_format=unsigned(pass.depth_format);
  key.samples=pass.samples;key.attachments=pass.attachments;
  const auto remember=[&] {data.frames[eye]=frame;data.focus_keys[eye]=key;data.focus_cached[eye]=true;};
  const auto cache=mocktail::vr::internal::DensityCacheAction(data.initialized[eye],
      data.focus_cached[eye],data.frames[eye],frame,data.focus_keys[eye],key);
  if(cache==mocktail::vr::internal::GazeDensityCacheAction::kNeutral)return VK_NULL_HANDLE;
  if(cache==mocktail::vr::internal::GazeDensityCacheAction::kReuse) {
    remember();return data.eyes[eye].view;
  }
  const float guard_x=.5f*std::min(region_bound.width,eye_width)/eye_width;
  const float guard_y=.5f*std::min(region_bound.height,eye_height)/eye_height;
  auto& pixels=data.scratch_pixels[eye];
  if(data.framebuffer_density_mapping) {
    if(!mocktail::vr::internal::FillFramebufferGazeDensityMap(pixels,layout,x,y,level,radius,
        guard_x*eye_width,guard_y*eye_height))return VK_NULL_HANDLE;
  } else mocktail::vr::internal::FillGazeDensityMap(pixels,data.width,data.height,x,y,level,radius,guard_x,guard_y);
  if(data.initialized[eye] && data.uploaded[eye]==pixels) {
    remember();return data.eyes[eye].view;
  }
  auto& densities=data.scratch_densities[eye];
  mocktail::vr::internal::FillHalfDensityMap(densities,pixels);
  if(data.host_copy) {
    // The previous frame's eye-copy fence completes all preceding graphics
    // reads before these owned maps are reused. Non-dynamic maps can be read
    // by the driver at BeginRenderPass, so finish the CPU copy here. Queue
    // submission supplies host-write visibility without an upload submission.
    VkResult status=VK_SUCCESS;
    if(!data.initialized[eye]) {
      VkHostImageLayoutTransitionInfoEXT transition{};transition.sType=VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT;
      transition.image=data.eyes[eye].image;transition.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;transition.newLayout=VK_IMAGE_LAYOUT_GENERAL;transition.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      status=FovProc<PFN_vkTransitionImageLayoutEXT>(device,"vkTransitionImageLayoutEXT")(device,1,&transition);
      if(status==VK_SUCCESS)data.initialized[eye]=true;
    }
    if(status==VK_SUCCESS) {
      VkMemoryToImageCopyEXT region{};region.sType=VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT;region.pHostPointer=densities.data();region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={data.width,data.height,1};
      VkCopyMemoryToImageInfoEXT copy{};copy.sType=VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT;copy.dstImage=data.eyes[eye].image;copy.dstImageLayout=VK_IMAGE_LAYOUT_GENERAL;copy.regionCount=1;copy.pRegions=&region;
      status=FovProc<PFN_vkCopyMemoryToImageEXT>(device,"vkCopyMemoryToImageEXT")(device,&copy);
    }
    if(status==VK_SUCCESS) {
      data.uploaded[eye]=pixels;remember();
      static unsigned reports=0;if(reports++<2)std::fprintf(stderr,"  [vr-foveation] gaze map uploaded on CPU without GPU queue/fence wait\n");
      return data.eyes[eye].view;
    }
    data.host_copy=false;
    std::fprintf(stderr,"  [vr-foveation] host map copy failed (%d); using synchronized staging fallback\n",int(status));
  }
  std::memcpy(data.mapped,densities.data(),densities.size()*sizeof(std::uint16_t));
  if(FovProc<PFN_vkResetCommandPool>(device,"vkResetCommandPool")(device,data.pool,0)!=VK_SUCCESS)return VK_NULL_HANDLE;
  VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if(FovProc<PFN_vkBeginCommandBuffer>(device,"vkBeginCommandBuffer")(data.command,&begin)!=VK_SUCCESS)return VK_NULL_HANDLE;
  VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;barrier.image=data.eyes[eye].image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.oldLayout=data.initialized[eye]?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.srcAccessMask=data.initialized[eye]?VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT:0;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  auto pipeline_barrier=FovProc<PFN_vkCmdPipelineBarrier>(device,"vkCmdPipelineBarrier");
  pipeline_barrier(data.command,data.initialized[eye]?VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={data.width,data.height,1};
  FovProc<PFN_vkCmdCopyBufferToImage>(device,"vkCmdCopyBufferToImage")(data.command,data.staging,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT|VK_ACCESS_HOST_READ_BIT;
  pipeline_barrier(data.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT|VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,0,nullptr,1,&barrier);
  if(FovProc<PFN_vkEndCommandBuffer>(device,"vkEndCommandBuffer")(data.command)!=VK_SUCCESS)return VK_NULL_HANDLE;
  if(FovProc<PFN_vkResetFences>(device,"vkResetFences")(device,1,&data.fence)!=VK_SUCCESS)return VK_NULL_HANDLE;
  VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&data.command;
  if(mocktail_vulkan_submit_synchronized(FovProc<PFN_vkQueueSubmit>(device,"vkQueueSubmit"),device,data.queue,1,&submit,data.fence)!=VK_SUCCESS)return VK_NULL_HANDLE;
  // Non-dynamic density maps may be read on the CPU at CmdBeginRenderPass.
  // A submitted copy alone is insufficient: finish it before returning a view.
  if(FovProc<PFN_vkWaitForFences>(device,"vkWaitForFences")(device,1,&data.fence,VK_TRUE,5000000000ull)!=VK_SUCCESS) {
    (void)mocktail_vulkan_idle_synchronized(FovProc<PFN_vkDeviceWaitIdle>(device,"vkDeviceWaitIdle"),device);data.gaze_ready=false;return VK_NULL_HANDLE;
  }
  data.initialized[eye]=true;data.uploaded[eye]=pixels;remember();
  return data.eyes[eye].view;
}
void FovRegisterDevice(VkDevice device, VkPhysicalDevice physical,
    const VkPhysicalDeviceMemoryProperties& memory, std::uint32_t family, bool statistics,
    mocktail::graphics::internal::FoveationDevicePolicy device_policy) {
  static const auto enabled=ResolveProcessFunction<bool(*)(VkDevice)>("mocktail_vr_xr_foveation_enabled");
  if(!enabled || !enabled(device) || family==UINT32_MAX) return;
  auto& state=FovState(); std::lock_guard<std::mutex> lock(state.mutex);
  if(state.devices.count(device)) return;
  FovDevice data;data.statistics=statistics;
  VkCommandPool pool{}; VkFence fence{}; bool submitted=false;
  const auto cleanup=[&]{
    if(submitted) (void)mocktail_vulkan_idle_synchronized(FovProc<PFN_vkDeviceWaitIdle>(device,"vkDeviceWaitIdle"),device);
    if(fence) FovProc<PFN_vkDestroyFence>(device,"vkDestroyFence")(device,fence,nullptr);
    if(pool) FovProc<PFN_vkDestroyCommandPool>(device,"vkDestroyCommandPool")(device,pool,nullptr);
  };
  VkPhysicalDeviceFragmentDensityMapPropertiesEXT density{};
  density.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT;
  VkPhysicalDeviceDriverProperties driver{};
  driver.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;density.pNext=&driver;
  VkPhysicalDeviceIDProperties id{};
  id.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;driver.pNext=&id;
  VkPhysicalDeviceProperties2 properties{};
  properties.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; properties.pNext=&density;
  const auto get_properties=HostPhysicalDeviceProc<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2","vkGetPhysicalDeviceProperties2KHR");
  if(!get_properties) return;
  get_properties(physical,&properties);
  const auto format_properties=HostPhysicalDeviceProc<PFN_vkGetPhysicalDeviceFormatProperties>("vkGetPhysicalDeviceFormatProperties",nullptr);
  VkFormatProperties format{};
  if(!format_properties)return;
  format_properties(physical,VK_FORMAT_R16G16_SFLOAT,&format);
  if(!(format.optimalTilingFeatures&VK_FORMAT_FEATURE_FRAGMENT_DENSITY_MAP_BIT_EXT))return;
  data.min_density_region=density.minFragmentDensityTexelSize;
  data.max_density_region=density.maxFragmentDensityTexelSize;
  data.framebuffer_density_mapping=driver.driverID==VK_DRIVER_ID_MESA_TURNIP;
  data.tile_driver.vendor_id=properties.properties.vendorID;
  data.tile_driver.device_id=properties.properties.deviceID;
  data.tile_driver.driver_version=properties.properties.driverVersion;
  data.tile_driver.driver_id=unsigned(driver.driverID);
  std::copy_n(id.driverUUID,data.tile_driver.driver_uuid.size(),data.tile_driver.driver_uuid.begin());
  data.driver_name.assign(driver.driverName,strnlen(driver.driverName,sizeof(driver.driverName)));
  data.driver_info.assign(driver.driverInfo,strnlen(driver.driverInfo,sizeof(driver.driverInfo)));
  // Offsets are not part of the calibrated density sampling contract. Merely
  // enabling either spelling of that extension keeps the conservative bound.
  data.density_offsets_enabled=device_policy.density_offsets_enabled;
  data.timestamp_period=properties.properties.limits.timestampPeriod;
  const auto get_families=HostPhysicalDeviceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties",nullptr);
  if(get_families) {
    unsigned count=0;get_families(physical,&count,nullptr);
    if(count<=256 && family<count){std::vector<VkQueueFamilyProperties> families(count);get_families(physical,&count,families.data());data.timestamp_bits=families[family].timestampValidBits;}
  }
  if(!density.maxFragmentDensityTexelSize.width || !density.maxFragmentDensityTexelSize.height) return;
  // One full-density map covers every legal guest framebuffer. Its contents
  // are finished on the GPU before any CmdBeginRenderPass host read can occur.
  VkImageCreateInfo image{}; image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image.imageType=VK_IMAGE_TYPE_2D; image.format=VK_FORMAT_R16G16_SFLOAT;
  image.extent={(properties.properties.limits.maxFramebufferWidth+density.maxFragmentDensityTexelSize.width-1)/density.maxFragmentDensityTexelSize.width,
    (properties.properties.limits.maxFramebufferHeight+density.maxFragmentDensityTexelSize.height-1)/density.maxFragmentDensityTexelSize.height,1};
  image.mipLevels=1;image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
  image.usage=VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  bool ok=FovProc<PFN_vkCreateImage>(device,"vkCreateImage")(device,&image,nullptr,&data.image)==VK_SUCCESS;
  if(ok) {
    VkMemoryRequirements req{};FovProc<PFN_vkGetImageMemoryRequirements>(device,"vkGetImageMemoryRequirements")(device,data.image,&req);
    std::uint32_t type=0;while(type<memory.memoryTypeCount && !(req.memoryTypeBits&(1u<<type))) ++type;
    VkMemoryAllocateInfo alloc{};alloc.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
    ok=type<memory.memoryTypeCount && FovProc<PFN_vkAllocateMemory>(device,"vkAllocateMemory")(device,&alloc,nullptr,&data.memory)==VK_SUCCESS;
    if(ok)ok=FovProc<PFN_vkBindImageMemory>(device,"vkBindImageMemory")(device,data.image,data.memory,0)==VK_SUCCESS;
  }
  if(ok) {
    VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=data.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    ok=FovProc<PFN_vkCreateImageView>(device,"vkCreateImageView")(device,&view,nullptr,&data.view)==VK_SUCCESS;
  }
  VkCommandBuffer command{};
  if(ok) {
    VkCommandPoolCreateInfo cp{};cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;cp.queueFamilyIndex=family;cp.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    ok=FovProc<PFN_vkCreateCommandPool>(device,"vkCreateCommandPool")(device,&cp,nullptr,&pool)==VK_SUCCESS;
    VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
    if(ok)ok=FovProc<PFN_vkAllocateCommandBuffers>(device,"vkAllocateCommandBuffers")(device,&ca,&command)==VK_SUCCESS;
  }
  if(ok) {
    VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ok=FovProc<PFN_vkBeginCommandBuffer>(device,"vkBeginCommandBuffer")(command,&begin)==VK_SUCCESS;
    if(ok) {
      VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;barrier.image=data.image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
      const auto pipeline_barrier=FovProc<PFN_vkCmdPipelineBarrier>(device,"vkCmdPipelineBarrier");
      pipeline_barrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
      const VkClearColorValue full{{1.f,1.f,1.f,1.f}};
      FovProc<PFN_vkCmdClearColorImage>(device,"vkCmdClearColorImage")(command,data.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&full,1,&barrier.subresourceRange);
      barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT|VK_ACCESS_HOST_READ_BIT;
      pipeline_barrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT|VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,0,nullptr,1,&barrier);
      ok=FovProc<PFN_vkEndCommandBuffer>(device,"vkEndCommandBuffer")(command)==VK_SUCCESS;
    }
  }
  if(ok) {
    VkFenceCreateInfo fc{};fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    ok=FovProc<PFN_vkCreateFence>(device,"vkCreateFence")(device,&fc,nullptr,&fence)==VK_SUCCESS;
    if(ok) {
      VkQueue queue{};FovProc<PFN_vkGetDeviceQueue>(device,"vkGetDeviceQueue")(device,family,0,&queue);
      VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
      ok=mocktail_vulkan_submit_synchronized(FovProc<PFN_vkQueueSubmit>(device,"vkQueueSubmit"),device,queue,1,&submit,fence)==VK_SUCCESS;
      submitted=ok;
      if(ok)ok=FovProc<PFN_vkWaitForFences>(device,"vkWaitForFences")(device,1,&fence,VK_TRUE,5000000000ull)==VK_SUCCESS;
    }
  }
  cleanup();
  if(ok) {data.gaze_ready=FovInitializeGaze(device,memory,family,data);state.devices.emplace(device,data);std::fprintf(stderr,"  [vr-foveation] full-detail fallback map initialized %ux%u; protected gaze maps %s\n",image.extent.width,image.extent.height,data.gaze_ready?"ready":"unavailable");}
  else {FovFreeNeutral(device,data);std::fprintf(stderr,"  [vr-foveation] fallback map failed; render-pass foveation disabled\n");}
}
void FovDestroyDevice(VkDevice device) {
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  if(!state.devices.count(device)) return;
  (void)mocktail_vulkan_idle_synchronized(FovProc<PFN_vkDeviceWaitIdle>(device,"vkDeviceWaitIdle"),device);
  for(auto& timing:state.timings)if(timing.device==device && timing.pool) {FovProc<PFN_vkDestroyQueryPool>(device,"vkDestroyQueryPool")(device,timing.pool,nullptr);timing.pool=VK_NULL_HANDLE;timing.reported=true;}
  for(auto& timing:state.timings)if(timing.device==device && timing.statistics_pool) {FovProc<PFN_vkDestroyQueryPool>(device,"vkDestroyQueryPool")(device,timing.statistics_pool,nullptr);timing.statistics_pool=VK_NULL_HANDLE;}
  for(auto it=state.framebuffers.begin();it!=state.framebuffers.end();) {
    if(it->second.device!=device) {++it;continue;}
    for(const auto& clone:it->second.clones)FovProc<PFN_vkDestroyFramebuffer>(device,"vkDestroyFramebuffer")(device,clone.framebuffer,nullptr);
    it=state.framebuffers.erase(it);
  }
  for(auto it=state.passes.begin();it!=state.passes.end();)if(it->second.device==device)it=state.passes.erase(it);else++it;
  FovFreeGaze(device,state.devices.at(device));FovFreeNeutral(device,state.devices.at(device));state.devices.erase(device);
}
bool FovColorFormat(VkFormat format) {
  return format==VK_FORMAT_R8G8B8A8_UNORM || format==VK_FORMAT_R8G8B8A8_SRGB ||
      format==VK_FORMAT_B8G8R8A8_UNORM || format==VK_FORMAT_B8G8R8A8_SRGB ||
      format==VK_FORMAT_R16G16B16A16_SFLOAT || format==VK_FORMAT_B10G11R11_UFLOAT_PACK32 ||
      format==VK_FORMAT_A2B10G10R10_UNORM_PACK32;
}
VkResult FovCreateRenderPass(VkDevice device,const VkRenderPassCreateInfo* info,
    const VkAllocationCallbacks* allocator,VkRenderPass* result) {
  const auto host=HostDispatchForDevice(device).create_render_pass;
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  if(!info || !state.devices.count(device) || info->pNext || !info->attachmentCount)
    return host(device,info,allocator,result);
  FovRenderPass pass{device,info->attachmentCount};pass.subpasses=info->subpassCount;
  for(unsigned i=0;i<info->attachmentCount;++i)
    pass.samples=std::max(pass.samples,unsigned(info->pAttachments[i].samples));
  if(info->subpassCount==1) {
    const auto& subpass=info->pSubpasses[0];
    pass.colors=subpass.colorAttachmentCount;
    pass.depth=subpass.pDepthStencilAttachment && subpass.pDepthStencilAttachment->attachment!=VK_ATTACHMENT_UNUSED;
    if(pass.depth && subpass.pDepthStencilAttachment->attachment<info->attachmentCount)
      pass.depth_format=info->pAttachments[subpass.pDepthStencilAttachment->attachment].format;
    if(pass.colors==1 && subpass.pColorAttachments[0].attachment<info->attachmentCount)
      pass.color_format=info->pAttachments[subpass.pColorAttachments[0].attachment].format;
  }
  // A neutral density map can still force tiled rendering and reconstruction.
  // Only attach one when the pass can actually receive an eye gaze map. Other
  // passes retain the driver's normal render-mode choice and native stores.
  const bool eligible=mocktail::vr::IsFoveationScenePassCompatible(*info) &&
      FovColorFormat(pass.color_format);
  if(!eligible) {
    const auto status=host(device,info,allocator,result);
    if(status==VK_SUCCESS && FovTraceEnabled())state.passes[*result]=pass;
    return status;
  }
  const auto& subpass=info->pSubpasses[0];
  const unsigned color_index=subpass.pColorAttachments[0].attachment;
  const unsigned depth_index=subpass.pDepthStencilAttachment->attachment;
  bool strict_contract=info->flags==0 && info->attachmentCount==2 &&
      subpass.flags==0 && subpass.pipelineBindPoint==VK_PIPELINE_BIND_POINT_GRAPHICS &&
      subpass.inputAttachmentCount==0 && subpass.pResolveAttachments==nullptr &&
      subpass.preserveAttachmentCount==0 && color_index<2 && depth_index<2 && color_index!=depth_index;
  for(unsigned i=0;i<info->attachmentCount;++i)strict_contract&=info->pAttachments[i].flags==0;
  if(strict_contract) {
    // This legacy render-pass path rejects pNext above, excluding multiview,
    // MSRTSS, custom resolves, density offsets and shading-rate attachments.
    auto& identity=pass.tile_identity;
    identity.color_format=unsigned(pass.color_format);identity.depth_format=unsigned(pass.depth_format);
    identity.color_samples=unsigned(info->pAttachments[color_index].samples);
    identity.depth_samples=unsigned(info->pAttachments[depth_index].samples);
    identity.subpass_count=1;identity.total_attachment_count=3;
    identity.color_attachment_count=1;identity.gmem_attachment_count=2;
    identity.has_depth_attachment=true;identity.has_density_attachment=true;
  }
  std::vector<VkAttachmentDescription> attachments(info->pAttachments,info->pAttachments+info->attachmentCount);
  VkAttachmentDescription map{};map.format=VK_FORMAT_R16G16_SFLOAT;map.samples=VK_SAMPLE_COUNT_1_BIT;map.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;map.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;map.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;map.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;map.initialLayout=map.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
  attachments.push_back(map);
  VkRenderPassFragmentDensityMapCreateInfoEXT density{};density.sType=VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT;density.fragmentDensityMapAttachment={info->attachmentCount,VK_IMAGE_LAYOUT_GENERAL};
  auto adjusted=*info;adjusted.pNext=&density;adjusted.attachmentCount=attachments.size();adjusted.pAttachments=attachments.data();
  const auto status=host(device,&adjusted,allocator,result);
  if(status==VK_SUCCESS) {pass.attached=true;state.passes[*result]=pass;}
  return status;
}
VkResult FovCreateFramebuffer(VkDevice device,const VkFramebufferCreateInfo* info,
    const VkAllocationCallbacks* allocator,VkFramebuffer* result) {
  const auto host=HostDispatchForDevice(device).create_framebuffer;
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  if(!info || !state.passes.count(info->renderPass))return host(device,info,allocator,result);
  if(!state.passes.at(info->renderPass).attached) {
    const auto status=host(device,info,allocator,result);
    if(status==VK_SUCCESS && !info->pNext && !info->flags && info->layers==1) {
      std::vector<VkImageView> views(info->pAttachments,info->pAttachments+info->attachmentCount);
      state.framebuffers.emplace(*result,FovFramebuffer{device,*info,std::move(views),{}});
    }
    return status;
  }
  if(info->pNext || info->flags || info->layers!=1 || info->attachmentCount!=state.passes.at(info->renderPass).attachments) {
    std::fprintf(stderr,"  [vr-foveation] unsupported framebuffer contract; refusing altered render pass\n");return VK_ERROR_FEATURE_NOT_PRESENT;
  }
  std::vector<VkImageView> views(info->pAttachments,info->pAttachments+info->attachmentCount);
  views.push_back(state.devices.at(device).view);
  auto adjusted=*info;adjusted.attachmentCount=views.size();adjusted.pAttachments=views.data();
  const auto status=host(device,&adjusted,allocator,result);
  if(status==VK_SUCCESS)state.framebuffers.emplace(*result,FovFramebuffer{device,*info,std::move(views),{}});
  return status;
}
void FovForgetFramebuffer(VkDevice device,VkFramebuffer framebuffer) {
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);
  auto it=state.framebuffers.find(framebuffer);if(it==state.framebuffers.end())return;
  for(const auto& clone:it->second.clones)HostDispatchForDevice(device).destroy_framebuffer(device,clone.framebuffer,nullptr);
  state.framebuffers.erase(it);
  state.timing_visits.erase(framebuffer);
}
void FovForgetRenderPass(VkRenderPass pass) {
  auto& state=FovState();std::lock_guard<std::mutex> lock(state.mutex);state.passes.erase(pass);
  // A destroyed render pass may legally leave its framebuffer alive. Retire
  // its clone cache key, retaining the framebuffer until its owner's teardown.
  for(auto& item:state.framebuffers)for(auto& clone:item.second.clones)if(clone.pass==pass)clone.pass=VK_NULL_HANDLE;
}
VkFramebuffer FovFramebufferForBegin(VkDevice device,const VkRenderPassBeginInfo* begin) {
  static const auto select=ResolveProcessFunction<bool(*)(VkDevice,VkFramebuffer,std::uint32_t,std::uint32_t,float*,float*,float*,std::uint64_t*,int*,int*)>("mocktail_vr_xr_foveation_render_focus");
  if(!select || !begin || begin->pNext)return begin?begin->framebuffer:VK_NULL_HANDLE;
  const bool scene_enabled=FovSceneEnabled();
  auto& state=FovState();
  std::uint32_t width=0,height=0;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto fb=state.framebuffers.find(begin->framebuffer);
    const auto pass=state.passes.find(begin->renderPass);
    if(fb==state.framebuffers.end() || pass==state.passes.end() ||
        fb->second.device!=device || pass->second.device!=device || !pass->second.attached || !state.devices.count(device))return begin->framebuffer;
    const auto& info=fb->second.info;const auto& rp=pass->second;
    const bool color=FovColorFormat(rp.color_format);
    // Full eye-sized native 1x/2x/4x/8x 3D passes only. Smaller effects, partial
    // render areas, depth-only shadows and unknown/MRT contracts stay neutral.
    if(scene_enabled && rp.subpasses==1 && rp.colors==1 && color && rp.depth &&
        mocktail::vr::IsFoveationSceneSampleCount(static_cast<VkSampleCountFlagBits>(rp.samples)) &&
        info.layers==1 && begin->renderArea.offset.x==0 && begin->renderArea.offset.y==0 &&
        begin->renderArea.extent.width==info.width && begin->renderArea.extent.height==info.height) {
      width=info.width;height=info.height;
    }
  }
  // Do not hold the adapter lock when calling the backend: fence completion
  // reports can acquire these locks in the opposite direction.
  float x=0,y=0,radius=.22f;std::uint64_t frame=0;int eye=0,level=1;
  if(!select(device,begin->framebuffer,width,height,&x,&y,&radius,&frame,&eye,&level))return begin->framebuffer;
  static const bool alternate=[] {const char* value=std::getenv("MOCKTAIL_VR_FOVEATION_ALTERNATE");return value && value[0]=='1';}();
  if(FovTraceEnabled() && alternate && (frame&1u))return begin->framebuffer;

  std::lock_guard<std::mutex> lock(state.mutex);
  auto it=state.framebuffers.find(begin->framebuffer);
  if(it==state.framebuffers.end() || !state.passes.count(begin->renderPass))return begin->framebuffer;
  if(!state.devices.count(device))return begin->framebuffer;
  const auto& selected_pass=state.passes.at(begin->renderPass);
  const auto region_bound=FovGazeRegionBound(state.devices.at(device),selected_pass,it->second.info);
  const auto view=FovUploadGaze(device,state.devices.at(device),selected_pass,region_bound,
      eye,frame,x,y,level,radius,it->second.info.width,it->second.info.height);if(!view)return begin->framebuffer;
  auto& fb=it->second;
  for(const auto& clone:fb.clones)if(clone.view==view && clone.pass==begin->renderPass)return clone.framebuffer;
  // The backend verifies final eye handles, or the current eye getter and
  // renderer generation for an exact eye-sized scene intermediate.
  std::vector<VkImageView> attachments=fb.attachments;attachments.back()=view;
  auto adjusted=fb.info;adjusted.renderPass=begin->renderPass;adjusted.attachmentCount=attachments.size();adjusted.pAttachments=attachments.data();
  VkFramebuffer clone{};
  if(HostDispatchForDevice(device).create_framebuffer(device,&adjusted,nullptr,&clone)!=VK_SUCCESS)return begin->framebuffer;
  fb.clones.push_back({view,begin->renderPass,clone});
  if(selected_pass.samples>1)++state.msaa_selected;
  if(++state.selected<=8 || (selected_pass.samples>1 && state.msaa_selected<=8)) {
    const auto& data=state.devices.at(device);
    const auto& pass=state.passes.at(begin->renderPass);
    const float guard_x=.5f*std::min(region_bound.width,fb.info.width)/fb.info.width;
    const float guard_y=.5f*std::min(region_bound.height,fb.info.height)/fb.info.height;
    const bool calibrated=region_bound.width!=data.max_density_region.width || region_bound.height!=data.max_density_region.height;
    std::fprintf(stderr,"  [vr-foveation] verified %s render pass uses gaze map with full-density focus disk; eye=%d samples=%u color_resolve=%d extent=%ux%u color_format=%d depth_format=%d density_extent=%ux%u density_mapping=%s density_max_region=%ux%u selected_tile_bound=%ux%u calibrated=%d guard_uv=%.6f,%.6f guard_pixels=%.1f,%.1f gaze_ndc=%.4f,%.4f focus_radius=%.3f profile=%d\n",
        width?"3D scene":"eye output",eye,pass.samples,int(pass.attachments==3),fb.info.width,fb.info.height,
        int(pass.color_format),int(pass.depth_format),data.width,data.height,
        data.framebuffer_density_mapping?"framebuffer":"normalized",
        data.max_density_region.width,data.max_density_region.height,
        region_bound.width,region_bound.height,int(calibrated),
        guard_x,guard_y,guard_x*fb.info.width,guard_y*fb.info.height,x,y,radius,level);
  }
  return clone;
}
