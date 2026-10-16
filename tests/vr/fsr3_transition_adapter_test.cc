#include "mocktail/vr/fsr3_scene_inputs.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Exercise the actual adapter with host operations replaced by CPU records.
// No loader, GPU resources, command submission or XR runtime is used.
bool available = true, capturing = false;
unsigned next_handle = 100, fail_pass_call = 0;
struct CreatedPass {
  VkDevice device{};
  VkRenderPass handle{};
  std::vector<VkAttachmentDescription> attachments;
};
struct DestroyedPass { VkDevice device{}; VkRenderPass handle{}; void* allocator_data{}; };
std::vector<CreatedPass> created_passes;
std::vector<DestroyedPass> destroyed_passes;
std::vector<VkRenderPass> forgotten_foveation_passes;
std::vector<mocktail::vr::TemporalSceneInputs> scene_captures;
VkImageUsageFlags created_image_usage{};

bool Available() { return available; }
bool Capturing() { return capturing; }
bool CurrentEye(void** owner, int* eye, void** framebuffer) {
  *owner=reinterpret_cast<void*>(1);*eye=0;*framebuffer=reinterpret_cast<void*>(2);
  return true;
}
void CaptureScene(VkDevice,VkCommandBuffer,const mocktail::vr::TemporalSceneInputs* inputs) {
  scene_captures.push_back(*inputs);
}
void CaptureMotion(VkDevice,VkCommandBuffer,const mocktail::vr::TemporalMotionInputs*) {}
template<class Function> Function ResolveProcessFunction(const char* name) {
  if constexpr(std::is_same_v<Function,bool(*)()>) {
    if(!std::strcmp(name,"mocktail_vr_xr_fsr3_inputs_available"))return &Available;
    if(!std::strcmp(name,"mocktail_vr_xr_fsr3_inputs_enabled"))return &Capturing;
  } else if constexpr(std::is_same_v<Function,decltype(&CurrentEye)>) {
    if(!std::strcmp(name,"mocktail_vr_current_eye"))return &CurrentEye;
  } else if constexpr(std::is_same_v<Function,decltype(&CaptureScene)>) {
    if(!std::strcmp(name,"mocktail_vr_xr_fsr3_scene_end"))return &CaptureScene;
  } else if constexpr(std::is_same_v<Function,decltype(&CaptureMotion)>) {
    if(!std::strcmp(name,"mocktail_vr_xr_fsr3_motion_end"))return &CaptureMotion;
  }
  return nullptr;
}
struct FakeEtc2 {
  VkResult CreateImage(VkDevice,const VkImageCreateInfo* info,const VkAllocationCallbacks*,VkImage* image) {
    created_image_usage=info->usage;*image=reinterpret_cast<VkImage>(std::uintptr_t(next_handle++));return VK_SUCCESS;
  }
};
struct FakeAdapterState { FakeEtc2 etc2; };
FakeAdapterState& State() { static FakeAdapterState state;return state; }
VkResult FovCreateRenderPass(VkDevice device,const VkRenderPassCreateInfo* info,
    const VkAllocationCallbacks*,VkRenderPass* pass) {
  const auto handle=reinterpret_cast<VkRenderPass>(std::uintptr_t(next_handle++));
  created_passes.push_back({device,handle,{info->pAttachments,info->pAttachments+info->attachmentCount}});
  if(fail_pass_call && created_passes.size()==fail_pass_call){*pass=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_HOST_MEMORY;}
  *pass=handle;return VK_SUCCESS;
}
void FovForgetRenderPass(VkRenderPass pass) { forgotten_foveation_passes.push_back(pass); }
VKAPI_ATTR void VKAPI_CALL DestroyPass(VkDevice device,VkRenderPass pass,const VkAllocationCallbacks* allocator) {
  destroyed_passes.push_back({device,pass,allocator?allocator->pUserData:nullptr});
}
struct FakeDispatch { PFN_vkDestroyRenderPass destroy_render_pass=&DestroyPass; };
const FakeDispatch& HostDispatchForDevice(VkDevice) { static FakeDispatch dispatch;return dispatch; }
bool Fsr3ShaderDumpEnabled();
#include "graphics/vulkan_temporal_inputs_adapter.inc"

class Fsr3TransitionAdapter : public testing::Test {
 protected:
  VkDevice device=reinterpret_cast<VkDevice>(1);
  VkCommandBuffer command=reinterpret_cast<VkCommandBuffer>(2);
  VkRenderPass pass{};
  VkFramebuffer framebuffer=reinterpret_cast<VkFramebuffer>(3);
  VkAttachmentDescription attachments[2]{};
  VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  VkRenderPassCreateInfo info{};
  void SetUp() override {
    available=true;capturing=false;fail_pass_call=0;
    created_passes.clear();destroyed_passes.clear();forgotten_foveation_passes.clear();scene_captures.clear();
    auto& state=Fsr3State();state.images.clear();state.views.clear();state.passes.clear();
    state.framebuffers.clear();state.active.clear();state.pipeline_motion.clear();
    state.bound_graphics.clear();state.command_devices.clear();state.diagnostic_formats.clear();state.diagnostic_shaders.clear();
    attachments[0].format=VK_FORMAT_R8G8B8A8_UNORM;attachments[1].format=VK_FORMAT_D32_SFLOAT;
    for(auto& attachment:attachments){attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;}
    attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;
    subpass.pColorAttachments=&color;subpass.pDepthStencilAttachment=&depth;
    info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;info.attachmentCount=2;
    info.pAttachments=attachments;info.subpassCount=1;info.pSubpasses=&subpass;
  }
  void TearDown() override { Fsr3ForgetDevice(device); }
  void CreateTargets() {
    VkImageView views[2]{};
    for(unsigned i=0;i<2;++i){
      VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.imageType=VK_IMAGE_TYPE_2D;
      image.format=attachments[i].format;image.extent={2160,2160,1};image.mipLevels=image.arrayLayers=1;
      image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
      image.usage=i?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      VkImage handle{};VkImageCreateInfo effective{};
      ASSERT_EQ(Fsr3CreateImage(device,&image,nullptr,&handle,&effective),VK_SUCCESS);
      EXPECT_TRUE(effective.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
      VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      view.image=handle;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=image.format;
      view.subresourceRange={i?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      views[i]=reinterpret_cast<VkImageView>(std::uintptr_t(next_handle++));Fsr3RecordView(views[i],&view);
    }
    ASSERT_EQ(Fsr3CreateRenderPass(device,&info,nullptr,&pass),VK_SUCCESS);
    VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass=pass;fb.width=fb.height=2160;fb.layers=1;fb.attachmentCount=2;fb.pAttachments=views;
    Fsr3RecordFramebuffer(device,framebuffer,&fb);
  }
  void Scene() {
    VkClearValue clear[2]{};clear[1].depthStencil.depth=0.f;
    VkRenderPassBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass=pass;begin.framebuffer=framebuffer;begin.renderArea.extent={2160,2160};
    begin.clearValueCount=2;begin.pClearValues=clear;
    Fsr3SceneBegin(device,command,&begin,VK_SUBPASS_CONTENTS_INLINE);
    Fsr3SceneEnd(device,command);
  }
};

TEST_F(Fsr3TransitionAdapter, NativeToFsrAndBackUsesExistingTargetsWithoutNativeCopies) {
  CreateTargets();ASSERT_EQ(created_passes.size(),2u);
  EXPECT_EQ(created_passes[0].attachments[1].storeOp,VK_ATTACHMENT_STORE_OP_DONT_CARE);
  EXPECT_EQ(created_passes[1].attachments[1].storeOp,VK_ATTACHMENT_STORE_OP_STORE);
  EXPECT_EQ(attachments[1].storeOp,VK_ATTACHMENT_STORE_OP_DONT_CARE);
  EXPECT_EQ(Fsr3RenderPassForBegin(device,pass),pass);Scene();EXPECT_TRUE(scene_captures.empty());
  capturing=true;
  EXPECT_EQ(Fsr3RenderPassForBegin(device,pass),created_passes[1].handle);
  Scene();ASSERT_EQ(scene_captures.size(),1u);EXPECT_NE(scene_captures[0].depth,VK_NULL_HANDLE);
  EXPECT_TRUE(scene_captures[0].depth_clear_valid);EXPECT_EQ(scene_captures[0].depth_clear,0.f);
  capturing=false;EXPECT_EQ(Fsr3RenderPassForBegin(device,pass),pass);Scene();EXPECT_EQ(scene_captures.size(),1u);
  Fsr3ForgetPass(pass);ASSERT_EQ(destroyed_passes.size(),1u);
  EXPECT_EQ(destroyed_passes[0].handle,created_passes[1].handle);
  Fsr3ForgetPass(pass);EXPECT_EQ(destroyed_passes.size(),1u);
}

TEST_F(Fsr3TransitionAdapter, AlreadyPreservedPassOwnsNoDuplicate) {
  attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_STORE;CreateTargets();
  ASSERT_EQ(created_passes.size(),1u);capturing=true;
  EXPECT_EQ(Fsr3RenderPassForBegin(device,pass),pass);Scene();EXPECT_EQ(scene_captures.size(),1u);
  Fsr3ForgetPass(pass);EXPECT_TRUE(destroyed_passes.empty());
}

TEST_F(Fsr3TransitionAdapter, VariantChangesOnlyColorAndDepthStoreOperations) {
  attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
  ASSERT_EQ(Fsr3CreateRenderPass(device,&info,nullptr,&pass),VK_SUCCESS);
  ASSERT_EQ(created_passes.size(),2u);
  for(unsigned i=0;i<2;++i){
    auto preserving=created_passes[1].attachments[i];
    EXPECT_EQ(preserving.storeOp,VK_ATTACHMENT_STORE_OP_STORE);
    preserving.storeOp=created_passes[0].attachments[i].storeOp;
    EXPECT_EQ(std::memcmp(&preserving,&created_passes[0].attachments[i],sizeof(preserving)),0);
  }
}

TEST_F(Fsr3TransitionAdapter, VariantFailureKeepsNativePassAndRejectsDepthCapture) {
  fail_pass_call=2;CreateTargets();capturing=true;
  EXPECT_EQ(Fsr3RenderPassForBegin(device,pass),pass);Scene();EXPECT_TRUE(scene_captures.empty());
  Fsr3ForgetPass(pass);EXPECT_TRUE(destroyed_passes.empty());
}

TEST_F(Fsr3TransitionAdapter, OriginalFailureCreatesNoVariantOrTracker) {
  fail_pass_call=1;
  EXPECT_EQ(Fsr3CreateRenderPass(device,&info,nullptr,&pass),VK_ERROR_OUT_OF_HOST_MEMORY);
  EXPECT_EQ(created_passes.size(),1u);EXPECT_TRUE(Fsr3State().passes.empty());
}

TEST_F(Fsr3TransitionAdapter, VariantDestructionUsesCopiedOriginalAllocator) {
  VkAllocationCallbacks allocator{};allocator.pUserData=reinterpret_cast<void*>(123);
  ASSERT_EQ(Fsr3CreateRenderPass(device,&info,&allocator,&pass),VK_SUCCESS);
  allocator.pUserData=reinterpret_cast<void*>(456);Fsr3ForgetPass(pass);
  ASSERT_EQ(destroyed_passes.size(),1u);EXPECT_EQ(destroyed_passes[0].allocator_data,reinterpret_cast<void*>(123));
  EXPECT_EQ(forgotten_foveation_passes.size(),1u);
}

TEST_F(Fsr3TransitionAdapter, CleanupStillRemovesMetadataWhenTemporalModeIsOff) {
  CreateTargets();capturing=true;
  const auto image=Fsr3State().images.begin()->first;
  Fsr3State().active[command].inputs.color=image;Fsr3State().bound_graphics[command]=1;
  capturing=false;Fsr3ForgetImage(image);EXPECT_FALSE(Fsr3State().images.count(image));EXPECT_TRUE(Fsr3State().active.empty());
  Fsr3ResetCommand(command);EXPECT_TRUE(Fsr3State().bound_graphics.empty());
  const auto view=Fsr3State().views.begin()->first;Fsr3ForgetView(view);EXPECT_FALSE(Fsr3State().views.count(view));
  Fsr3ForgetFramebuffer(framebuffer);EXPECT_TRUE(Fsr3State().framebuffers.empty());
  Fsr3ForgetDevice(device);EXPECT_TRUE(Fsr3State().passes.empty());EXPECT_EQ(destroyed_passes.size(),1u);
}

TEST_F(Fsr3TransitionAdapter, DeviceCleanupRetainsOtherDevicesOwnedVariants) {
  ASSERT_EQ(Fsr3CreateRenderPass(device,&info,nullptr,&pass),VK_SUCCESS);
  const auto other_device=reinterpret_cast<VkDevice>(9);VkRenderPass other{};
  ASSERT_EQ(Fsr3CreateRenderPass(other_device,&info,nullptr,&other),VK_SUCCESS);
  Fsr3ForgetDevice(device);EXPECT_EQ(destroyed_passes.size(),1u);EXPECT_TRUE(Fsr3State().passes.count(other));
  Fsr3ForgetDevice(other_device);EXPECT_EQ(destroyed_passes.size(),2u);
}

TEST_F(Fsr3TransitionAdapter, NonVrDoesNotAddTransferUsageOrPreservingVariants) {
  available=false;
  ASSERT_EQ(Fsr3CreateRenderPass(device,&info,nullptr,&pass),VK_SUCCESS);
  EXPECT_EQ(created_passes.size(),1u);EXPECT_TRUE(Fsr3State().passes.empty());
  VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkImage handle{};VkImageCreateInfo effective{};Fsr3CreateImage(device,&image,nullptr,&handle,&effective);
  EXPECT_EQ(created_image_usage,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);EXPECT_TRUE(Fsr3State().images.empty());
}

TEST_F(Fsr3TransitionAdapter, PipelineAndShaderCleanupWorksAfterModeChange) {
  const auto pipeline=reinterpret_cast<VkPipeline>(44);const auto shader=reinterpret_cast<VkShaderModule>(45);
  Fsr3State().pipeline_motion[pipeline]={device,1};Fsr3State().diagnostic_shaders[shader]={device,46};
  Fsr3ForgetPipeline(pipeline);Fsr3ForgetShader(shader);
  EXPECT_TRUE(Fsr3State().pipeline_motion.empty());EXPECT_TRUE(Fsr3State().diagnostic_shaders.empty());
}

TEST_F(Fsr3TransitionAdapter, DeviceCleanupRemovesOnlyItsShaderPipelineAndCommandMetadata) {
  const auto pipeline=reinterpret_cast<VkPipeline>(44),other_pipeline=reinterpret_cast<VkPipeline>(45);
  const auto shader=reinterpret_cast<VkShaderModule>(46),other_shader=reinterpret_cast<VkShaderModule>(47);
  const auto other_device=reinterpret_cast<VkDevice>(9);
  Fsr3State().pipeline_motion[pipeline]={device,1};Fsr3State().pipeline_motion[other_pipeline]={other_device,1};
  Fsr3State().diagnostic_shaders[shader]={device,48};Fsr3State().diagnostic_shaders[other_shader]={other_device,49};
  Fsr3State().command_devices[command]=device;Fsr3State().bound_graphics[command]=1;
  Fsr3ForgetDevice(device);
  EXPECT_FALSE(Fsr3State().pipeline_motion.count(pipeline));EXPECT_TRUE(Fsr3State().pipeline_motion.count(other_pipeline));
  EXPECT_FALSE(Fsr3State().diagnostic_shaders.count(shader));EXPECT_TRUE(Fsr3State().diagnostic_shaders.count(other_shader));
  EXPECT_TRUE(Fsr3State().command_devices.empty());EXPECT_TRUE(Fsr3State().bound_graphics.empty());
  Fsr3ForgetDevice(other_device);
}
