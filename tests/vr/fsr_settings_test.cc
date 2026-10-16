#include "mocktail/vr/fsr_settings.h"
#include <gtest/gtest.h>
#include "mocktail/vr/temporal_reprojection.h"
namespace mocktail::vr {
TEST(FsrSettings, StandardQualityPresetsUseReferenceRatios) {
  EXPECT_EQ(FsrRenderSize(2160,1,{1}),1662);
  EXPECT_EQ(FsrRenderSize(2160,1,{2}),1440);
  EXPECT_EQ(FsrRenderSize(2160,1,{3}),1270);
  EXPECT_EQ(FsrRenderSize(2160,1,{4}),1080);
  EXPECT_EQ(FsrRenderSize(2160,2,{4}),1080);
  EXPECT_EQ(FsrRenderSize(2160,3,{2}),1440);
  EXPECT_EQ(FsrRenderSize(2160,3,{0,80}),1728);
}
TEST(FsrSettings, CustomScaleTracksAnyOutputAndRoundsToEvenPixels) {
  EXPECT_EQ(FsrRenderSize(2160,1,{0,80}),1728);
  EXPECT_EQ(FsrRenderSize(2880,1,{0,80}),2304);
  EXPECT_EQ(FsrRenderSize(3072,1,{0,80}),2458);
  EXPECT_EQ(FsrRenderSize(2160,1,{0,90}),1944);
  EXPECT_EQ(FsrRenderSize(2160,1,{0,25}),540);
}
TEST(FsrSettings, ExactInputCanBeUpscaledToDifferentOutputSizes) {
  EXPECT_EQ(FsrRenderSize(2160,1,{5,80,1728}),1728);
  EXPECT_EQ(FsrRenderSize(2880,1,{5,80,1728}),1728);
  EXPECT_EQ(FsrRenderSize(1440,1,{5,80,1728}),1440);
  EXPECT_EQ(FsrRenderSize(2160,1,{5,80,1513}),1512);
  EXPECT_EQ(FsrRenderSize(2160,3,{5,80,1728}),1728);
}
TEST(FsrSettings, NativeIgnoresFsrScaleAndQuality) {
  EXPECT_EQ(FsrRenderSize(2160,0,{4,25,128}),2160);
  EXPECT_EQ(FsrRenderSize(0,1),0);
  EXPECT_EQ(FsrRenderSize(1,1),1);
  EXPECT_EQ(FsrRenderSize(2,1),2);
}
TEST(FsrSettings, InputNeverExceedsOutputOrUnderflows) {
  for(unsigned output=2;output<8192;++output)for(int quality=0;quality<=5;++quality) {
    const auto size=FsrRenderSize(output,1,{quality,99,8192});
    ASSERT_LE(size,output);ASSERT_GE(size,2);ASSERT_EQ(size%2,0);
  }
}
TEST(FsrSettings, SettingsComparisonIncludesEveryResizeInput) {
  FsrSettings base;
  EXPECT_EQ(base,FsrSettings{});
  auto changed=base;changed.quality=2;EXPECT_NE(base,changed);
  changed=base;changed.scale_percent=90;EXPECT_NE(base,changed);
  changed=base;changed.input_resolution=1600;EXPECT_NE(base,changed);
}
TEST(TemporalReprojection, IdentityPreservesEveryScreenPosition) {
  TemporalEyePose pose;pose.fov[0]=-.8f;pose.fov[1]=1.1f;
  float matrix[3][4];ASSERT_TRUE(TemporalReprojection(pose,pose,matrix));
  for(float u:{.1f,.5f,.9f})for(float v:{.1f,.5f,.9f}) {
    const float d=matrix[2][0]*u+matrix[2][1]*v+matrix[2][2];
    EXPECT_NEAR((matrix[0][0]*u+matrix[0][1]*v+matrix[0][2])/d,u,1e-6f);
    EXPECT_NEAR((matrix[1][0]*u+matrix[1][1]*v+matrix[1][2])/d,v,1e-6f);
  }
}
TEST(TemporalReprojection, HeadRotationMapsCurrentRayIntoPreviousEye) {
  TemporalEyePose current,previous;
  const auto q=pose::QuatFromAxisAngle({0,1,0},.03f);
  current.orientation[1]=q.y;current.orientation[3]=q.w;
  float m[3][4];ASSERT_TRUE(TemporalReprojection(current,previous,m));
  const auto ray=pose::QuatRotate(q,{0,0,-1});
  const float expected=(ray.x/-ray.z+std::tan(1.f))/(2*std::tan(1.f));
  EXPECT_NEAR((m[0][0]*.5f+m[0][1]*.5f+m[0][2])/(m[2][0]*.5f+m[2][1]*.5f+m[2][2]),expected,1e-6f);
}
TEST(TemporalReprojection, TranslationAndLargeRotationRejectHistory) {
  TemporalEyePose current,previous;float m[3][4];
  current.position[0]=.003f;EXPECT_FALSE(TemporalReprojection(current,previous,m));
  current.position[0]=0;current.orientation[1]=std::sin(.2f);current.orientation[3]=std::cos(.2f);
  EXPECT_FALSE(TemporalReprojection(current,previous,m));
}
TEST(TemporalReprojection, InvalidPoseAndProjectionRejectHistory) {
  TemporalEyePose current,previous;float m[3][4];
  current.orientation[3]=NAN;EXPECT_FALSE(TemporalReprojection(current,previous,m));
  current=previous;current.fov[0]=NAN;EXPECT_FALSE(TemporalReprojection(current,previous,m));
  current=previous;current.fov[1]=current.fov[0];EXPECT_FALSE(TemporalReprojection(current,previous,m));
}

TEST(FsrSettings, ExactWidthPreservesNonsquareEyeAspect) {
  EXPECT_EQ(FsrRenderSize(2560,1,{5,80,1920}),1920);
  EXPECT_EQ(FsrRenderHeight(2560,1440,1,{5,80,1920}),1080);
  EXPECT_EQ(FsrRenderHeight(2560,1440,0,{5,80,1920}),1440);
  EXPECT_EQ(FsrRenderHeight(2160,2160,2,{5,80,1728}),1728);
}

}
