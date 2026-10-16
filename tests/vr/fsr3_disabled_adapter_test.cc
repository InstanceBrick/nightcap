#include <vulkan/vulkan.h>
#include <gtest/gtest.h>

namespace {
int image_creates=0,pass_creates=0;
VkImageCreateInfo observed_image{};
VkAttachmentDescription observed_attachment{};
struct Etc2 {
  VkResult CreateImage(VkDevice,const VkImageCreateInfo* info,const VkAllocationCallbacks*,VkImage*) {
    ++image_creates;if(info)observed_image=*info;return VK_SUCCESS;
  }
};
struct FakeState { Etc2 etc2; };
FakeState& State() { static FakeState state;return state; }
VkResult FovCreateRenderPass(VkDevice,const VkRenderPassCreateInfo* info,
    const VkAllocationCallbacks*,VkRenderPass*) {
  ++pass_creates;if(info && info->attachmentCount)observed_attachment=info->pAttachments[0];
  return VK_SUCCESS;
}
#include "graphics/vulkan_temporal_inputs_adapter.inc"

TEST(NativeTemporalAdapter, CaptureAndShaderDumpAreUnavailable) {
  EXPECT_FALSE(Fsr3InputsAvailable());EXPECT_FALSE(Fsr3InputsEnabled());
  EXPECT_FALSE(Fsr3ShaderDumpEnabled());
}
TEST(NativeTemporalAdapter, TransientDepthRetainsOriginalUsageAndAllocationContract) {
  VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.format=VK_FORMAT_D32_SFLOAT;info.extent={2160,2160,1};
  info.samples=VK_SAMPLE_COUNT_1_BIT;
  info.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
  VkImageCreateInfo effective{};VkImage image{};const auto before=image_creates;
  EXPECT_EQ(Fsr3CreateImage(VK_NULL_HANDLE,&info,nullptr,&image,&effective),VK_SUCCESS);
  EXPECT_EQ(image_creates,before+1);EXPECT_EQ(effective.usage,info.usage);
  EXPECT_EQ(observed_image.usage,info.usage);
  EXPECT_EQ(observed_image.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT,0u);
}
TEST(NativeTemporalAdapter, DiscardStoresAndRenderPassIdentityRemainNative) {
  VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;
  attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
  VkRenderPassCreateInfo info{};info.attachmentCount=1;info.pAttachments=&attachment;
  VkRenderPass pass{};const auto before=pass_creates;
  EXPECT_EQ(Fsr3CreateRenderPass(VK_NULL_HANDLE,&info,nullptr,&pass),VK_SUCCESS);
  EXPECT_EQ(pass_creates,before+1);EXPECT_EQ(observed_attachment.storeOp,VK_ATTACHMENT_STORE_OP_DONT_CARE);
  EXPECT_EQ(Fsr3RenderPassForBegin(VK_NULL_HANDLE,pass),pass);
}
}  // namespace
