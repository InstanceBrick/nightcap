#include "mocktail/vr/foveation_msaa_contract.h"

#include <gtest/gtest.h>

namespace mocktail::vr {
namespace {

struct Pass {
  VkAttachmentDescription attachments[3]{};
  VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkAttachmentReference resolve{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};

  explicit Pass(VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_2_BIT,
                bool resolved = true) {
    attachments[0].format = attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[0].samples = attachments[1].samples = samples;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    subpass.pResolveAttachments = resolved ? &resolve : nullptr;
    info.attachmentCount = resolved ? 3 : 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
  }
};

TEST(FoveationMsaaContract, SupportsSingleSampleAndNativeTwoFourEightSamplePasses) {
  for (const auto samples : {VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT,
                             VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT}) {
    Pass pass(samples, false);
    EXPECT_TRUE(IsFoveationScenePassCompatible(pass.info));
  }
}

TEST(FoveationMsaaContract, RetainsTheNativeSingleSampleResolve) {
  for (const auto samples : {VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT,
                             VK_SAMPLE_COUNT_8_BIT}) {
    Pass pass(samples);
    EXPECT_TRUE(IsFoveationScenePassCompatible(pass.info));
    EXPECT_EQ(pass.attachments[0].samples, samples);
    EXPECT_EQ(pass.attachments[1].samples, samples);
    EXPECT_EQ(pass.attachments[2].samples, VK_SAMPLE_COUNT_1_BIT);
    EXPECT_EQ(pass.info.attachmentCount, 3u);
  }
}

TEST(FoveationMsaaContract, UnvalidatedSampleCountsAndMixedSamplesRemainNative) {
  for (const auto samples : {VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT,
                             VK_SAMPLE_COUNT_64_BIT}) {
    Pass pass(samples);
    EXPECT_FALSE(IsFoveationScenePassCompatible(pass.info));
  }
  Pass mixed;
  mixed.attachments[1].samples = VK_SAMPLE_COUNT_4_BIT;
  EXPECT_FALSE(IsFoveationScenePassCompatible(mixed.info));
}

TEST(FoveationMsaaContract, RejectsInvalidResolveIndicesCountsAndFormats) {
  for (const auto attachment : {0u, 1u, 3u, 99u}) {
    Pass pass;
    pass.resolve.attachment = attachment;
    EXPECT_FALSE(IsFoveationScenePassCompatible(pass.info));
  }
  Pass multisample_resolve;
  multisample_resolve.attachments[2].samples = VK_SAMPLE_COUNT_2_BIT;
  EXPECT_FALSE(IsFoveationScenePassCompatible(multisample_resolve.info));
  Pass mismatched_format;
  mismatched_format.attachments[2].format = VK_FORMAT_B8G8R8A8_UNORM;
  EXPECT_FALSE(IsFoveationScenePassCompatible(mismatched_format.info));
  Pass single_sample(VK_SAMPLE_COUNT_1_BIT);
  EXPECT_FALSE(IsFoveationScenePassCompatible(single_sample.info));
  Pass extra_attachment;
  extra_attachment.info.attachmentCount = 4;
  EXPECT_FALSE(IsFoveationScenePassCompatible(extra_attachment.info));
}

TEST(FoveationMsaaContract, UnusedResolveIsEquivalentToExternalResolve) {
  Pass pass;
  pass.resolve.attachment = VK_ATTACHMENT_UNUSED;
  pass.info.attachmentCount = 2;
  EXPECT_TRUE(IsFoveationScenePassCompatible(pass.info));
}

TEST(FoveationMsaaContract, RejectsIncompleteAndAliasedSceneAttachments) {
  Pass missing;
  missing.subpass.pDepthStencilAttachment = nullptr;
  EXPECT_FALSE(IsFoveationScenePassCompatible(missing.info));
  Pass alias;
  alias.depth.attachment = alias.color.attachment;
  EXPECT_FALSE(IsFoveationScenePassCompatible(alias.info));
  Pass invalid;
  invalid.depth.attachment = VK_ATTACHMENT_UNUSED;
  EXPECT_FALSE(IsFoveationScenePassCompatible(invalid.info));
  Pass missing_colors;
  missing_colors.subpass.pColorAttachments = nullptr;
  EXPECT_FALSE(IsFoveationScenePassCompatible(missing_colors.info));
  Pass missing_attachments;
  missing_attachments.info.pAttachments = nullptr;
  EXPECT_FALSE(IsFoveationScenePassCompatible(missing_attachments.info));
}

TEST(FoveationMsaaContract, UnknownMrtInputPreserveAndExtensionContractsRemainNative) {
  Pass mrt;
  mrt.subpass.colorAttachmentCount = 2;
  EXPECT_FALSE(IsFoveationScenePassCompatible(mrt.info));
  Pass input;
  input.subpass.inputAttachmentCount = 1;
  EXPECT_FALSE(IsFoveationScenePassCompatible(input.info));
  Pass preserve;
  preserve.subpass.preserveAttachmentCount = 1;
  EXPECT_FALSE(IsFoveationScenePassCompatible(preserve.info));
  Pass extension;
  extension.info.pNext = &extension;
  EXPECT_FALSE(IsFoveationScenePassCompatible(extension.info));
  Pass multipass;
  multipass.info.subpassCount = 2;
  EXPECT_FALSE(IsFoveationScenePassCompatible(multipass.info));
  Pass flags;
  flags.attachments[2].flags = VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;
  EXPECT_FALSE(IsFoveationScenePassCompatible(flags.info));
}

}  // namespace
}  // namespace mocktail::vr
