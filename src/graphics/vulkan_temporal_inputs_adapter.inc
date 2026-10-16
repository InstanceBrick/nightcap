#if !defined(MOCKTAIL_ENABLE_FSR3)
// Native builds preserve guest allocations/stores and perform no temporal
// resource, shader, pipeline or command tracking.
[[maybe_unused]] bool Fsr3InputsAvailable() { return false; }
[[maybe_unused]] bool Fsr3InputsEnabled() { return false; }
[[maybe_unused]] bool Fsr3ShaderDumpEnabled() { return false; }
inline VkResult Fsr3CreateImage(VkDevice device,const VkImageCreateInfo* info,
    const VkAllocationCallbacks* allocator,VkImage* image,VkImageCreateInfo* effective) {
  if(info && effective)*effective=*info;
  return State().etc2.CreateImage(device,info,allocator,image);
}
inline VkResult Fsr3CreateRenderPass(VkDevice device,const VkRenderPassCreateInfo* info,
    const VkAllocationCallbacks* allocator,VkRenderPass* pass) {
  return FovCreateRenderPass(device,info,allocator,pass);
}
inline VkRenderPass Fsr3RenderPassForBegin(VkDevice,VkRenderPass pass) { return pass; }
inline void Fsr3RecordView(VkImageView,const VkImageViewCreateInfo*) {}
inline void Fsr3RecordFramebuffer(VkDevice,VkFramebuffer,const VkFramebufferCreateInfo*) {}
inline void Fsr3SceneBegin(VkDevice,VkCommandBuffer,const VkRenderPassBeginInfo*,VkSubpassContents) {}
inline void Fsr3SceneEnd(VkDevice,VkCommandBuffer) {}
inline void Fsr3NotePipeline(VkDevice,VkCommandBuffer,VkPipelineBindPoint,VkPipeline) {}
inline void Fsr3NoteDraw(VkCommandBuffer) {}
inline void Fsr3ForgetPipeline(VkPipeline) {}
inline void Fsr3ForgetShader(VkShaderModule) {}
inline void Fsr3ForgetImage(VkImage) {}
inline void Fsr3ForgetView(VkImageView) {}
inline void Fsr3ForgetFramebuffer(VkFramebuffer) {}
inline void Fsr3ForgetPass(VkRenderPass) {}
inline void Fsr3ForgetDevice(VkDevice) {}
inline void Fsr3ResetCommand(VkCommandBuffer) {}
inline void Fsr3DumpShader(VkDevice,VkShaderModule,const VkShaderModuleCreateInfo*) {}
inline void Fsr3RecordDiagnosticPass(VkRenderPass,const VkRenderPassCreateInfo*) {}
inline void Fsr3DumpGraphicsPipelines(VkDevice,unsigned,const VkGraphicsPipelineCreateInfo*,const VkPipeline*) {}
#else
// Temporal scene resources are captured on the guest command that produced
// them. Nothing in this adapter assumes an image remains valid after return.
struct Fsr3ImageRecord {
  VkDevice device{};
  VkImageCreateInfo info{};
  bool transferable = false;
};
struct Fsr3ViewRecord { VkImageViewCreateInfo info{}; };
struct Fsr3ShaderRecord { VkDevice device{}; std::uint64_t hash=0; };
struct Fsr3PipelineRecord { VkDevice device{}; int kind=0; };
struct Fsr3PassRecord {
  VkDevice device{};
  unsigned color = VK_ATTACHMENT_UNUSED, depth = VK_ATTACHMENT_UNUSED;
  VkImageLayout color_layout{}, depth_layout{};
  VkAttachmentLoadOp depth_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  VkRenderPass capture_pass = VK_NULL_HANDLE;
  bool owns_capture_pass = false;
  bool has_allocator = false;
  VkAllocationCallbacks allocator{};
};
struct Fsr3FramebufferRecord {
  VkDevice device{};
  VkRenderPass pass{};
  unsigned width = 0, height = 0;
  std::vector<VkImageView> views;
};
struct Fsr3ActiveScene {
  mocktail::vr::TemporalSceneInputs inputs;
  bool motion_bound=false,other_bound=false,unsupported_motion=false;
};
struct Fsr3InputsState {
  std::mutex mutex;
  std::unordered_map<VkImage,Fsr3ImageRecord> images;
  std::unordered_map<VkImageView,Fsr3ViewRecord> views;
  std::unordered_map<VkRenderPass,Fsr3PassRecord> passes;
  std::unordered_map<VkFramebuffer,Fsr3FramebufferRecord> framebuffers;
  std::unordered_map<VkCommandBuffer,Fsr3ActiveScene> active;
  std::unordered_map<VkPipeline,Fsr3PipelineRecord> pipeline_motion;
  std::unordered_map<VkCommandBuffer,VkDevice> command_devices;
  std::unordered_map<VkCommandBuffer,int> bound_graphics;
  std::unordered_map<VkRenderPass,std::vector<VkFormat>> diagnostic_formats;
  std::unordered_map<VkShaderModule,Fsr3ShaderRecord> diagnostic_shaders;
};
Fsr3InputsState& Fsr3State() { static Fsr3InputsState state; return state; }

bool Fsr3InputsAvailable() {
  static const auto available=ResolveProcessFunction<bool(*)()>(
      "mocktail_vr_xr_fsr3_inputs_available");
  return available && available();
}

bool Fsr3InputsEnabled() {
  static const auto enabled=ResolveProcessFunction<bool(*)()>(
      "mocktail_vr_xr_fsr3_inputs_enabled");
  return enabled && enabled();
}

VkResult Fsr3CreateImage(VkDevice device,const VkImageCreateInfo* info,
    const VkAllocationCallbacks* allocator,VkImage* image,
    VkImageCreateInfo* effective) {
  if(!info)return State().etc2.CreateImage(device,info,allocator,image);
  *effective=*info;
  const bool candidate=Fsr3InputsAvailable() &&
      mocktail::vr::IsTemporalImageCandidate(*info);
  if(candidate) {
    effective->usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  auto status=State().etc2.CreateImage(device,effective,allocator,image);
  // Optional preservation must never make a previously supported render
  // target fail creation. The callback will reject an uncapturable target.
  if(status!=VK_SUCCESS && candidate && effective->usage!=info->usage) {
    *effective=*info;
    status=State().etc2.CreateImage(device,info,allocator,image);
  }
  if(status==VK_SUCCESS && image && Fsr3InputsAvailable()) {
    auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
    auto saved=*effective;saved.pNext=nullptr;saved.pQueueFamilyIndices=nullptr;
    state.images[*image]={device,saved,candidate &&
        bool(effective->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)};
  }
  return status;
}

void Fsr3RecordView(VkImageView view,const VkImageViewCreateInfo* info) {
  if(!info || !Fsr3InputsAvailable())return;
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  auto saved=*info;saved.pNext=nullptr;
  state.views[view]={saved};
}

VkResult Fsr3CreateRenderPass(VkDevice device,const VkRenderPassCreateInfo* info,
    const VkAllocationCallbacks* allocator,VkRenderPass* pass) {
  if(!Fsr3InputsAvailable() || !info || info->pNext || info->flags ||
      info->subpassCount!=1 || !info->pSubpasses || !info->pAttachments)
    return FovCreateRenderPass(device,info,allocator,pass);
  const auto& subpass=info->pSubpasses[0];
  const auto depth=subpass.pDepthStencilAttachment;
  if(subpass.pipelineBindPoint!=VK_PIPELINE_BIND_POINT_GRAPHICS ||
      subpass.colorAttachmentCount!=1 || !subpass.pColorAttachments ||
      subpass.inputAttachmentCount || subpass.pResolveAttachments)
    return FovCreateRenderPass(device,info,allocator,pass);
  const unsigned color_index=subpass.pColorAttachments[0].attachment;
  const unsigned depth_index=depth ? depth->attachment : VK_ATTACHMENT_UNUSED;
  const bool has_depth=depth_index<info->attachmentCount;
  if(color_index>=info->attachmentCount || color_index==depth_index ||
      (!mocktail::vr::IsTemporalColorFormat(info->pAttachments[color_index].format) &&
       !mocktail::vr::IsTemporalMotionFormat(info->pAttachments[color_index].format)) ||
      (has_depth && !mocktail::vr::IsTemporalDepthFormat(info->pAttachments[depth_index].format)) ||
      info->pAttachments[color_index].samples!=VK_SAMPLE_COUNT_1_BIT ||
      (has_depth && info->pAttachments[depth_index].samples!=VK_SAMPLE_COUNT_1_BIT))
    return FovCreateRenderPass(device,info,allocator,pass);
  // Keep Native's original stores. Load/store operations do not participate in
  // render-pass compatibility, so its framebuffer and pipelines also support
  // a preserving variant selected only when temporal capture is active.
  const auto status=FovCreateRenderPass(device,info,allocator,pass);
  if(status!=VK_SUCCESS || !pass)return status;
  std::vector<VkAttachmentDescription> attachments(
      info->pAttachments,info->pAttachments+info->attachmentCount);
  if(has_depth)attachments[depth_index].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
  attachments[color_index].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
  auto adjusted=*info;adjusted.pAttachments=attachments.data();
  Fsr3PassRecord record;
  record.device=device;record.color=color_index;record.depth=depth_index;
  record.color_layout=attachments[color_index].finalLayout;
  record.depth_layout=has_depth?attachments[depth_index].finalLayout:VK_IMAGE_LAYOUT_UNDEFINED;
  record.depth_load_op=has_depth?attachments[depth_index].loadOp:VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  record.has_allocator=allocator!=nullptr;if(allocator)record.allocator=*allocator;
  const bool already_preserved=info->pAttachments[color_index].storeOp==VK_ATTACHMENT_STORE_OP_STORE &&
      (!has_depth || info->pAttachments[depth_index].storeOp==VK_ATTACHMENT_STORE_OP_STORE);
  if(already_preserved)record.capture_pass=*pass;
  else {
    VkRenderPass preserving{};
    if(FovCreateRenderPass(device,&adjusted,allocator,&preserving)==VK_SUCCESS) {
      record.capture_pass=preserving;record.owns_capture_pass=true;
    } else {
      std::fprintf(stderr,"  [vr-fsr3] preserving render-pass variant unavailable; retaining native pass\n");
    }
  }
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  state.passes[*pass]=record;
  return status;
}

VkRenderPass Fsr3RenderPassForBegin(VkDevice device,VkRenderPass pass) {
  if(!Fsr3InputsEnabled())return pass;
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  const auto found=state.passes.find(pass);
  return found!=state.passes.end() && found->second.device==device &&
      found->second.capture_pass?found->second.capture_pass:pass;
}

void Fsr3RecordFramebuffer(VkDevice device,VkFramebuffer framebuffer,
    const VkFramebufferCreateInfo* info) {
  if(!Fsr3InputsAvailable() || !info || info->pNext || info->flags ||
      info->layers!=1 || !info->pAttachments)return;
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  state.framebuffers[framebuffer]={device,info->renderPass,info->width,info->height,
      {info->pAttachments,info->pAttachments+info->attachmentCount}};
}

bool Fsr3Source(VkDevice device,VkImageView view,unsigned width,unsigned height,
    bool depth,VkImage* image,VkFormat* format,VkImageSubresourceRange* range) {
  // Caller owns Fsr3State().mutex.
  const auto& state=Fsr3State();const auto found=state.views.find(view);
  if(found==state.views.end())return false;
  const auto& info=found->second.info;const auto source=state.images.find(info.image);
  if(source==state.images.end() || source->second.device!=device ||
      !source->second.transferable || info.viewType!=VK_IMAGE_VIEW_TYPE_2D ||
      info.flags || info.subresourceRange.baseMipLevel ||
      info.subresourceRange.baseArrayLayer ||
      (info.subresourceRange.levelCount!=1 &&
       info.subresourceRange.levelCount!=VK_REMAINING_MIP_LEVELS) ||
      (info.subresourceRange.layerCount!=1 &&
       info.subresourceRange.layerCount!=VK_REMAINING_ARRAY_LAYERS) ||
      source->second.info.extent.width!=width ||
      source->second.info.extent.height!=height)return false;
  const auto aspect=depth ? mocktail::vr::TemporalDepthAspect(info.format)
                          : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
  if(!aspect || !(info.subresourceRange.aspectMask & aspect) ||
      (depth ? !mocktail::vr::IsTemporalDepthFormat(info.format)
             : (!mocktail::vr::IsTemporalColorFormat(info.format) &&
                !mocktail::vr::IsTemporalMotionFormat(info.format))))return false;
  *image=info.image;*format=info.format;
  *range={aspect,0,1,0,1};return true;
}

void Fsr3SceneBegin(VkDevice device,VkCommandBuffer command,
    const VkRenderPassBeginInfo* begin,VkSubpassContents contents) {
  if(!Fsr3InputsEnabled())return;
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  state.active.erase(command);state.command_devices[command]=device;
  if(!begin || begin->pNext || contents!=VK_SUBPASS_CONTENTS_INLINE ||
      begin->renderArea.offset.x || begin->renderArea.offset.y)return;
  const auto fb=state.framebuffers.find(begin->framebuffer);
  const auto pass=state.passes.find(begin->renderPass);
  if(fb==state.framebuffers.end() || pass==state.passes.end() ||
      !pass->second.capture_pass ||
      fb->second.device!=device || pass->second.device!=device ||
      fb->second.pass!=begin->renderPass ||
      fb->second.width!=begin->renderArea.extent.width ||
      fb->second.height!=begin->renderArea.extent.height ||
      fb->second.width<256 || fb->second.height<256 ||
      pass->second.color>=fb->second.views.size() ||
      (pass->second.depth!=VK_ATTACHMENT_UNUSED &&
       pass->second.depth>=fb->second.views.size()) ||
      pass->second.color_layout==VK_IMAGE_LAYOUT_UNDEFINED ||
      (pass->second.depth!=VK_ATTACHMENT_UNUSED &&
       pass->second.depth_layout==VK_IMAGE_LAYOUT_UNDEFINED))return;
  mocktail::vr::TemporalSceneInputs inputs;
  static const auto current=ResolveProcessFunction<bool(*)(void**,int*,void**)>(
      "mocktail_vr_current_eye");
  if(!current || !current(&inputs.owner,&inputs.eye,&inputs.guest_framebuffer) ||
      !inputs.owner || !inputs.guest_framebuffer || inputs.eye<0 || inputs.eye>1)return;
  inputs.framebuffer=begin->framebuffer;
  inputs.width=fb->second.width;inputs.height=fb->second.height;
  inputs.color_layout=pass->second.color_layout;
  inputs.depth_layout=pass->second.depth_layout;
  if(pass->second.depth_load_op==VK_ATTACHMENT_LOAD_OP_CLEAR &&
      begin->pClearValues && pass->second.depth<begin->clearValueCount) {
    inputs.depth_clear=begin->pClearValues[pass->second.depth].depthStencil.depth;
    inputs.depth_clear_valid=std::isfinite(inputs.depth_clear);
  }
  if(!Fsr3Source(device,fb->second.views[pass->second.color],inputs.width,
      inputs.height,false,&inputs.color,&inputs.color_format,&inputs.color_range))return;
  if(pass->second.depth!=VK_ATTACHMENT_UNUSED &&
      !Fsr3Source(device,fb->second.views[pass->second.depth],inputs.width,
      inputs.height,true,&inputs.depth,&inputs.depth_format,&inputs.depth_range))return;
  state.active[command]={inputs};

}

void Fsr3NotePipeline(VkDevice device,VkCommandBuffer command,VkPipelineBindPoint point,
                      VkPipeline pipeline) {
  if(!Fsr3InputsEnabled() || point!=VK_PIPELINE_BIND_POINT_GRAPHICS)return;
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  const auto found=state.pipeline_motion.find(pipeline);
  const int kind=found==state.pipeline_motion.end()?0:found->second.kind;
  state.bound_graphics[command]=kind;state.command_devices[command]=device;
}

void Fsr3NoteDraw(VkCommandBuffer command) {
  if(!Fsr3InputsEnabled())return;
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  const auto active=state.active.find(command);if(active==state.active.end())return;
  const auto bound=state.bound_graphics.find(command);
  const int kind=bound==state.bound_graphics.end()?0:bound->second;
  if(kind==1)active->second.motion_bound=true;
  else if(kind==2)active->second.unsupported_motion=true;
  else active->second.other_bound=true;
}

void Fsr3ForgetPipeline(VkPipeline pipeline) {
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  state.pipeline_motion.erase(pipeline);
}

void Fsr3SceneEnd(VkDevice device,VkCommandBuffer command) {
  if(!Fsr3InputsEnabled())return;
  Fsr3ActiveScene active;
  {
    auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
    const auto found=state.active.find(command);if(found==state.active.end())return;
    active=found->second;state.active.erase(found);
  }
  const auto& inputs=active.inputs;
  if(active.unsupported_motion)return;
  if(active.motion_bound) {
    if(active.other_bound || !mocktail::vr::IsTemporalMotionFormat(inputs.color_format))return;
    mocktail::vr::TemporalMotionInputs motion;
    motion.framebuffer=inputs.framebuffer;motion.image=inputs.color;
    motion.format=inputs.color_format;motion.layout=inputs.color_layout;
    motion.range=inputs.color_range;motion.width=inputs.width;motion.height=inputs.height;
    motion.owner=inputs.owner;motion.guest_framebuffer=inputs.guest_framebuffer;motion.eye=inputs.eye;
    static const auto capture=ResolveProcessFunction<void(*)(VkDevice,VkCommandBuffer,
        const mocktail::vr::TemporalMotionInputs*)>("mocktail_vr_xr_fsr3_motion_end");
    if(capture)capture(device,command,&motion);
    return;
  }
  if(!inputs.depth || !mocktail::vr::IsTemporalColorFormat(inputs.color_format))return;
  // Never hold the adapter mutex while entering the backend (its submission
  // completion paths can notify the adapter in the opposite direction).
  static const auto capture=ResolveProcessFunction<void(*)(VkDevice,
      VkCommandBuffer,const mocktail::vr::TemporalSceneInputs*)>(
          "mocktail_vr_xr_fsr3_scene_end");
  if(capture)capture(device,command,&inputs);
}

void Fsr3ForgetImage(VkImage image) {
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  state.images.erase(image);
  for(auto it=state.active.begin();it!=state.active.end();)
    if(it->second.inputs.color==image || it->second.inputs.depth==image)it=state.active.erase(it);
    else ++it;
}
void Fsr3ForgetView(VkImageView view) {
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);state.views.erase(view);
}
void Fsr3ForgetFramebuffer(VkFramebuffer framebuffer) {
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  state.framebuffers.erase(framebuffer);
  for(auto it=state.active.begin();it!=state.active.end();)
    if(it->second.inputs.framebuffer==framebuffer)it=state.active.erase(it);else ++it;
}
void Fsr3DestroyPassVariant(const Fsr3PassRecord& record) {
  if(!record.owns_capture_pass || !record.capture_pass)return;
  FovForgetRenderPass(record.capture_pass);
  const auto destroy=HostDispatchForDevice(record.device).destroy_render_pass;
  if(destroy)destroy(record.device,record.capture_pass,
      record.has_allocator?&record.allocator:nullptr);
}
void Fsr3ForgetPass(VkRenderPass pass) {
  Fsr3PassRecord record;
  {
    auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
    const auto found=state.passes.find(pass);
    if(found!=state.passes.end()){record=found->second;state.passes.erase(found);}
    state.diagnostic_formats.erase(pass);
  }
  Fsr3DestroyPassVariant(record);
}
void Fsr3ResetCommand(VkCommandBuffer command) {
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  state.active.erase(command);state.bound_graphics.erase(command);state.command_devices.erase(command);
}
void Fsr3ForgetDevice(VkDevice device) {
  std::vector<Fsr3PassRecord> variants;
  auto& state=Fsr3State();std::unique_lock<std::mutex> lock(state.mutex);
  const auto belongs=[&](VkImage image) {
    const auto found=state.images.find(image);
    return found!=state.images.end() && found->second.device==device;
  };
  for(auto it=state.active.begin();it!=state.active.end();)
    if(belongs(it->second.inputs.color) || belongs(it->second.inputs.depth))it=state.active.erase(it);else ++it;
  for(auto it=state.views.begin();it!=state.views.end();)
    if(belongs(it->second.info.image))it=state.views.erase(it);else ++it;
  for(auto it=state.images.begin();it!=state.images.end();)
    if(it->second.device==device)it=state.images.erase(it);else ++it;
  for(auto it=state.passes.begin();it!=state.passes.end();)
    if(it->second.device==device) {
      variants.push_back(it->second);state.diagnostic_formats.erase(it->first);
      it=state.passes.erase(it);
    } else ++it;
  for(auto it=state.framebuffers.begin();it!=state.framebuffers.end();)
    if(it->second.device==device)it=state.framebuffers.erase(it);else ++it;
  for(auto it=state.command_devices.begin();it!=state.command_devices.end();)
    if(it->second==device) {
      state.active.erase(it->first);state.bound_graphics.erase(it->first);
      it=state.command_devices.erase(it);
    } else ++it;
  for(auto it=state.pipeline_motion.begin();it!=state.pipeline_motion.end();)
    if(it->second.device==device)it=state.pipeline_motion.erase(it);else ++it;
  for(auto it=state.diagnostic_shaders.begin();it!=state.diagnostic_shaders.end();)
    if(it->second.device==device)it=state.diagnostic_shaders.erase(it);else ++it;
  lock.unlock();
  for(const auto& record:variants)Fsr3DestroyPassVariant(record);
}

void Fsr3ForgetShader(VkShaderModule shader) {
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  state.diagnostic_shaders.erase(shader);
}

bool Fsr3ShaderDumpEnabled() {
  static const bool enabled=[] {
    const auto* directory=std::getenv("MOCKTAIL_VR_FSR3_SHADER_DUMP");
    return directory && directory[0]=='/';
  }();return enabled;
}
void Fsr3DumpShader(VkDevice device,VkShaderModule module,const VkShaderModuleCreateInfo* info) {
  if((!Fsr3ShaderDumpEnabled() && !Fsr3InputsAvailable()) || !info || !info->pCode ||
      info->codeSize<20 || info->codeSize%4 || info->pCode[0]!=0x07230203u ||
      info->codeSize>16*1024*1024)return;
  std::uint64_t hash=14695981039346656037ull;
  const auto* bytes=reinterpret_cast<const unsigned char*>(info->pCode);
  for(std::size_t i=0;i<info->codeSize;++i){hash^=bytes[i];hash*=1099511628211ull;}
  {
    auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
    state.diagnostic_shaders[module]={device,hash};
  }
  if(!Fsr3ShaderDumpEnabled())return;
  static std::mutex mutex;static std::unordered_map<std::uint64_t,bool> dumped;
  std::lock_guard<std::mutex> lock(mutex);
  if(dumped.count(hash) || dumped.size()>=4096)return;
  char name[64];std::snprintf(name,sizeof(name),"/%016llx.spv",
      static_cast<unsigned long long>(hash));
  const std::string path=std::string(std::getenv("MOCKTAIL_VR_FSR3_SHADER_DUMP"))+name;
  if(auto* file=std::fopen(path.c_str(),"wb")) {
    const bool written=std::fwrite(bytes,1,info->codeSize,file)==info->codeSize;
    std::fclose(file);if(written)dumped[hash]=true;
  }
}

void Fsr3RecordDiagnosticPass(VkRenderPass pass,const VkRenderPassCreateInfo* info) {
  if(!Fsr3ShaderDumpEnabled() || !info || !info->pAttachments)return;
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  std::vector<VkFormat> formats;
  for(unsigned i=0;i<info->attachmentCount;++i)formats.push_back(info->pAttachments[i].format);
  state.diagnostic_formats[pass]=std::move(formats);
}

void Fsr3DumpGraphicsPipelines(VkDevice device,unsigned count,const VkGraphicsPipelineCreateInfo* infos,
                              const VkPipeline* pipelines) {
  if((!Fsr3ShaderDumpEnabled() && !Fsr3InputsAvailable()) || !infos)return;
  auto& state=Fsr3State();std::lock_guard<std::mutex> lock(state.mutex);
  if(pipelines)for(unsigned index=0;index<count;++index) {
    std::uint64_t vertex=0,fragment=0;
    for(unsigned i=0;i<infos[index].stageCount;++i) {
      const auto& stage=infos[index].pStages[i];
      const auto hash=state.diagnostic_shaders.find(stage.module);
      if(hash==state.diagnostic_shaders.end())continue;
      if(stage.stage==VK_SHADER_STAGE_VERTEX_BIT)vertex=hash->second.hash;
      if(stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT)fragment=hash->second.hash;
    }
    const int kind=mocktail::vr::IsTemporalClipMotionShaders(vertex,fragment)?1:
        fragment==0x821fff499ec5bdbbull?2:0;
    state.pipeline_motion[pipelines[index]]={device,kind};
  }
  if(!Fsr3ShaderDumpEnabled())return;
  const std::string path=std::string(std::getenv("MOCKTAIL_VR_FSR3_SHADER_DUMP"))+"/pipelines.jsonl";
  auto* file=std::fopen(path.c_str(),"a");if(!file)return;
  for(unsigned index=0;index<count;++index) {
    const auto& info=infos[index];
    std::fprintf(file,"{\"pass\":\"%p\",\"subpass\":%u,\"formats\":[",
        reinterpret_cast<void*>(info.renderPass),info.subpass);
    const auto formats=state.diagnostic_formats.find(info.renderPass);
    if(formats!=state.diagnostic_formats.end())
      for(unsigned i=0;i<formats->second.size();++i)
        std::fprintf(file,"%s%d",i?",":"",int(formats->second[i]));
    std::fprintf(file,"],\"shaders\":[");
    for(unsigned i=0;i<info.stageCount;++i) {
      const auto hash=state.diagnostic_shaders.find(info.pStages[i].module);
      std::fprintf(file,"%s{\"stage\":%u,\"hash\":\"%016llx\"}",i?",":"",
          unsigned(info.pStages[i].stage),static_cast<unsigned long long>(
              hash==state.diagnostic_shaders.end()?0:hash->second.hash));
    }
    std::fprintf(file,"]}\n");
  }
  std::fclose(file);
}

#endif
