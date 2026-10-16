#include "mocktail/vr/fsr3_scene_inputs.h"
#include <gtest/gtest.h>

namespace mocktail::vr {
namespace {
VkImageCreateInfo SceneDepth() {
  VkImageCreateInfo info{};
  info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType=VK_IMAGE_TYPE_2D;
  info.format=VK_FORMAT_D32_SFLOAT;
  info.extent={1728,1728,1};
  info.mipLevels=info.arrayLayers=1;
  info.samples=VK_SAMPLE_COUNT_1_BIT;
  info.tiling=VK_IMAGE_TILING_OPTIMAL;
  info.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  return info;
}
}
TEST(Fsr3SceneInputs, DistinguishesDepthFromStencilAndColor) {
  EXPECT_EQ(TemporalDepthAspect(VK_FORMAT_D32_SFLOAT),VK_IMAGE_ASPECT_DEPTH_BIT);
  EXPECT_EQ(TemporalDepthAspect(VK_FORMAT_D24_UNORM_S8_UINT),VK_IMAGE_ASPECT_DEPTH_BIT);
  EXPECT_EQ(TemporalDepthAspect(VK_FORMAT_S8_UINT),0u);
  EXPECT_EQ(TemporalDepthAspect(VK_FORMAT_R32_SFLOAT),0u);
  EXPECT_EQ(TemporalDepthAspect(VK_FORMAT_R8G8B8A8_UNORM),0u);
}
TEST(Fsr3SceneInputs, AcceptsSingleSampleSceneAttachments) {
  auto info=SceneDepth();EXPECT_TRUE(IsTemporalImageCandidate(info));
  info.format=VK_FORMAT_R16G16B16A16_SFLOAT;
  info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  EXPECT_TRUE(IsTemporalImageCandidate(info));
}
TEST(Fsr3SceneInputs, LeavesNativeTransientMemoryContractsUnchanged) {
  for (const auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_FORMAT_R16G16B16A16_SFLOAT}) {
    auto info = SceneDepth();
    info.format = format;
    info.usage = (format == VK_FORMAT_D32_SFLOAT
        ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    EXPECT_FALSE(IsTemporalImageCandidate(info));
    // Merely adding an existing transfer bit cannot make a memoryless target
    // safe to preserve; its engine allocation policy remains authoritative.
    info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    EXPECT_FALSE(IsTemporalImageCandidate(info));
  }
}
TEST(Fsr3SceneInputs, RejectsTexturesThatAreNotSceneAttachments) {
  auto info=SceneDepth();info.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
  EXPECT_FALSE(IsTemporalImageCandidate(info));
  info=SceneDepth();info.format=VK_FORMAT_R8G8B8A8_UNORM;
  EXPECT_FALSE(IsTemporalImageCandidate(info));
  info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  EXPECT_TRUE(IsTemporalImageCandidate(info));
}
TEST(Fsr3SceneInputs, RejectsMultisampleAndArrayContracts) {
  auto info=SceneDepth();info.samples=VK_SAMPLE_COUNT_4_BIT;
  EXPECT_FALSE(IsTemporalImageCandidate(info));
  info=SceneDepth();info.arrayLayers=2;EXPECT_FALSE(IsTemporalImageCandidate(info));
  info=SceneDepth();info.mipLevels=2;EXPECT_FALSE(IsTemporalImageCandidate(info));
  info=SceneDepth();info.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
  EXPECT_FALSE(IsTemporalImageCandidate(info));
}
TEST(Fsr3SceneInputs, RejectsLinearSmallAndNonTwoDimensionalTargets) {
  auto info=SceneDepth();info.tiling=VK_IMAGE_TILING_LINEAR;
  EXPECT_FALSE(IsTemporalImageCandidate(info));
  info=SceneDepth();info.extent.width=128;EXPECT_FALSE(IsTemporalImageCandidate(info));
  info=SceneDepth();info.extent.depth=2;EXPECT_FALSE(IsTemporalImageCandidate(info));
  info=SceneDepth();info.imageType=VK_IMAGE_TYPE_3D;
  EXPECT_FALSE(IsTemporalImageCandidate(info));
}
TEST(Fsr3SceneInputs, CandidateStartsWithoutClaimingValidFrameOrEye) {
  TemporalSceneInputs inputs;
  EXPECT_EQ(inputs.version,1u);EXPECT_EQ(inputs.size,sizeof(inputs));
  EXPECT_EQ(inputs.eye,-1);EXPECT_EQ(inputs.frame,0u);
  EXPECT_EQ(inputs.owner,nullptr);EXPECT_EQ(inputs.guest_framebuffer,nullptr);
  EXPECT_EQ(inputs.color,VK_NULL_HANDLE);EXPECT_EQ(inputs.depth,VK_NULL_HANDLE);
}
TEST(Fsr3SceneInputs, NativeMotionRequiresBothVerifiedShaderStages) {
  EXPECT_TRUE(IsTemporalClipMotionShaders(0x9b0d6a7041cad338ull,0x821fff499ec5bdbbull));
  EXPECT_TRUE(IsTemporalClipMotionShaders(0x4a99a561ef9805b6ull,0x821fff499ec5bdbbull));
  EXPECT_TRUE(IsTemporalClipMotionShaders(0x6dcaa6712cd5a2cdull,0x821fff499ec5bdbbull));
  EXPECT_FALSE(IsTemporalClipMotionShaders(0,0x821fff499ec5bdbbull));
  EXPECT_FALSE(IsTemporalClipMotionShaders(0x4a99a561ef9805b6ull,0));
}
TEST(Fsr3SceneInputs, WorldSpaceMotionIsNeverTreatedAsClipMotion) {
  EXPECT_FALSE(IsTemporalClipMotionShaders(0x42e46afbad744fb7ull,0x821fff499ec5bdbbull));
  EXPECT_FALSE(IsTemporalClipMotionShaders(0x2dbda082b2102e77ull,0x821fff499ec5bdbbull));
  EXPECT_FALSE(IsTemporalClipMotionShaders(0xd169aafd25de0cdcull,0x821fff499ec5bdbbull));
}
TEST(Fsr3SceneInputs, NativeMotionRequiresSignedFloatingPointTargets) {
  EXPECT_TRUE(IsTemporalMotionFormat(VK_FORMAT_R16G16_SFLOAT));
  EXPECT_TRUE(IsTemporalMotionFormat(VK_FORMAT_R32G32B32A32_SFLOAT));
  EXPECT_FALSE(IsTemporalMotionFormat(VK_FORMAT_R16G16_UNORM));
  EXPECT_FALSE(IsTemporalMotionFormat(VK_FORMAT_R8G8B8A8_UNORM));
  auto info=SceneDepth();info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  info.format=VK_FORMAT_R32G32B32A32_SFLOAT;
  EXPECT_TRUE(IsTemporalImageCandidate(info));
}
TEST(Fsr3SceneInputs, NativeClipMotionConvertsToPreviousMinusCurrentUv) {
  TemporalMotionInputs inputs;
  // A feature moving right/up has positive native X/Y displacement. Its old
  // pixel is left/down of the new pixel after the vertex shader's Y negation.
  EXPECT_FLOAT_EQ(.2f*inputs.previous_uv_scale_x,-.1f);
  EXPECT_FLOAT_EQ(.2f*inputs.previous_uv_scale_y,.1f);
}
}
