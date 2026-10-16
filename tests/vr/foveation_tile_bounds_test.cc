#include "mocktail/vr/foveation_tile_bounds.h"

#include <gtest/gtest.h>

namespace mocktail::vr {
namespace {

FoveationTileDriverIdentity CalibratedDriver() {
  FoveationTileDriverIdentity identity;
  identity.vendor_id = 0x5143;
  identity.device_id = 0x43051401;
  identity.driver_id = 18;
  identity.driver_version = 109060195;
  identity.driver_name = "turnip Mesa driver";
  identity.driver_info = "Mesa 26.3.0-devel (git-abc426bb62)";
  identity.driver_uuid = {0x58, 0x6b, 0x9f, 0xa4, 0x51, 0x4c, 0xb8, 0x8b,
                          0x3c, 0x11, 0xa9, 0x68, 0xef, 0x4d, 0x62, 0xe4};
  return identity;
}

FoveationTilePassIdentity CalibratedPass() {
  FoveationTilePassIdentity pass;
  pass.width = pass.height = 2160;
  pass.color_format = 37;
  pass.depth_format = 126;
  pass.color_samples = pass.depth_samples = 1;
  pass.framebuffer_layers = pass.subpass_count = 1;
  pass.total_attachment_count = 3;
  pass.color_attachment_count = 1;
  pass.gmem_attachment_count = 2;
  pass.has_depth_attachment = pass.has_density_attachment = true;
  return pass;
}

TEST(FoveationTileBounds, ExactCalibratedTupleHasAnisotropicBound) {
  const auto bound = KnownFoveationTileBound(CalibratedDriver(), CalibratedPass());
  ASSERT_TRUE(bound);
  EXPECT_EQ(bound->width, 768u);
  EXPECT_EQ(bound->height, 448u);
}

TEST(FoveationTileBounds, UnknownDriverDoesNotClaimACalibratedBound) {
  EXPECT_FALSE(KnownFoveationTileBound({}, CalibratedPass()));
  for (auto member : {&FoveationTileDriverIdentity::vendor_id,
                      &FoveationTileDriverIdentity::device_id,
                      &FoveationTileDriverIdentity::driver_id,
                      &FoveationTileDriverIdentity::driver_version}) {
    auto driver = CalibratedDriver();
    ++(driver.*member);
    EXPECT_FALSE(KnownFoveationTileBound(driver, CalibratedPass()));
  }
}

TEST(FoveationTileBounds, RequiresExactDriverNameRevisionAndUuid) {
  auto driver = CalibratedDriver();
  driver.driver_name = "another Mesa driver";
  EXPECT_FALSE(KnownFoveationTileBound(driver, CalibratedPass()));
  driver = CalibratedDriver();
  driver.driver_info = "Mesa 26.3.0-devel (git-abc426bb63)";
  EXPECT_FALSE(KnownFoveationTileBound(driver, CalibratedPass()));
  for (std::size_t i = 0; i < driver.driver_uuid.size(); ++i) {
    driver = CalibratedDriver();
    driver.driver_uuid[i] ^= 1;
    EXPECT_FALSE(KnownFoveationTileBound(driver, CalibratedPass()));
  }
}

TEST(FoveationTileBounds, OtherResolutionsAndFormatsRemainUnknown) {
  for (auto member : {&FoveationTilePassIdentity::width,
                      &FoveationTilePassIdentity::height,
                      &FoveationTilePassIdentity::color_format,
                      &FoveationTilePassIdentity::depth_format}) {
    auto pass = CalibratedPass();
    ++(pass.*member);
    EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), pass));
  }
  auto pass = CalibratedPass();
  pass.width = pass.height = 1728;
  EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), pass));
}

TEST(FoveationTileBounds, RejectsDifferentSampleLayerAndSubpassContracts) {
  for (auto member : {&FoveationTilePassIdentity::color_samples,
                      &FoveationTilePassIdentity::depth_samples,
                      &FoveationTilePassIdentity::framebuffer_layers,
                      &FoveationTilePassIdentity::subpass_count}) {
    auto pass = CalibratedPass();
    ++(pass.*member);
    EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), pass));
  }
}

TEST(FoveationTileBounds, RequiresExactlyColorDepthAndDensityAttachments) {
  for (auto member : {&FoveationTilePassIdentity::total_attachment_count,
                      &FoveationTilePassIdentity::color_attachment_count,
                      &FoveationTilePassIdentity::gmem_attachment_count}) {
    for (int difference : {-1, 1}) {
      auto pass = CalibratedPass();
      pass.*member += difference;
      EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), pass));
    }
  }
  auto pass = CalibratedPass();
  pass.has_depth_attachment = false;
  EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), pass));
  pass = CalibratedPass();
  pass.has_density_attachment = false;
  EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), pass));
}

TEST(FoveationTileBounds, RejectsEveryResolveInputFeedbackAndViewMaskBit) {
  for (auto member : {&FoveationTilePassIdentity::resolve_attachment_mask,
                      &FoveationTilePassIdentity::input_attachment_mask,
                      &FoveationTilePassIdentity::feedback_attachment_mask,
                      &FoveationTilePassIdentity::view_mask}) {
    for (unsigned bit = 0; bit < 32; ++bit) {
      auto pass = CalibratedPass();
      pass.*member = std::uint32_t{1} << bit;
      EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), pass));
    }
  }
}

TEST(FoveationTileBounds, RejectsOffsetMsrtssCustomResolveAndShadingRate) {
  for (auto member : {&FoveationTilePassIdentity::density_offsets_enabled,
                      &FoveationTilePassIdentity::multisampled_render_to_single_sampled,
                      &FoveationTilePassIdentity::custom_resolve,
                      &FoveationTilePassIdentity::has_shading_rate_attachment}) {
    auto pass = CalibratedPass();
    pass.*member = true;
    EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), pass));
  }
}

TEST(FoveationTileBounds, MissingPassMetadataRetainsConservativePolicy) {
  EXPECT_FALSE(KnownFoveationTileBound(CalibratedDriver(), {}));
}

}  // namespace
}  // namespace mocktail::vr
