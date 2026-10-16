#include "mocktail/vr/foveation_focus.h"
#include <gtest/gtest.h>
#include <limits>

namespace mocktail::vr::internal {
namespace {
FramebufferDensityLayout Layout(unsigned width,unsigned height=0) {
  return TurnipDensityLayout(68,68,width,height?height:width,32,32,1024,1024);
}
TEST(FramebufferDensityMap, LookupMatchesPixelGridAndKeepsPartialLastCell) {
  for(const auto pair:{std::pair<unsigned,unsigned>{864,27},{1728,54},{1944,61},{2160,68}}) {
    const auto layout=Layout(pair.first);
    ASSERT_TRUE(layout.valid);
    EXPECT_EQ(layout.texel_width,32u);EXPECT_EQ(layout.texel_height,32u);
    EXPECT_EQ(layout.active_width,pair.second);EXPECT_EQ(layout.active_height,pair.second);
  }
}
TEST(FramebufferDensityMap, LookupUsesIntegerRatioThenPowerOfTwoLimits) {
  const auto layout=Layout(4096,1944);
  ASSERT_TRUE(layout.valid);
  EXPECT_EQ(layout.texel_width,64u);EXPECT_EQ(layout.active_width,64u);
  EXPECT_EQ(layout.texel_height,32u);EXPECT_EQ(layout.active_height,61u);
}
TEST(FramebufferDensityMap, RejectsUnsupportedGridContracts) {
  EXPECT_FALSE(TurnipDensityLayout(0,68,1728,1728,32,32,1024,1024).valid);
  EXPECT_FALSE(TurnipDensityLayout(68,68,0,1728,32,32,1024,1024).valid);
  EXPECT_FALSE(TurnipDensityLayout(68,68,1728,1728,31,32,1024,1024).valid);
  EXPECT_FALSE(TurnipDensityLayout(68,68,1728,1728,32,32,16,1024).valid);
  EXPECT_FALSE(TurnipDensityLayout(1,1,4096,4096,32,32,1024,1024).valid);
}
TEST(FramebufferDensityMap, QuantizesTheScenePixelCenterRatherThanAllocationCenter) {
  const auto layout=Layout(1728);
  const auto center=QuantizeFramebufferGaze(layout,0,0);
  EXPECT_EQ(center.x,108u);EXPECT_EQ(center.y,108u);
  EXPECT_EQ(center.x*layout.texel_width/4u,864u);
  EXPECT_EQ(QuantizeGazeCenter(68,68,0,0).x,136u);
  const auto partial=Layout(1944,1728);
  const auto midpoint=QuantizeFramebufferGaze(partial,0,0);
  EXPECT_EQ(midpoint.x,122u);EXPECT_EQ(midpoint.y,108u);
  EXPECT_EQ(midpoint.x*partial.texel_width/4u,976u);
  const auto edge=QuantizeFramebufferGaze(partial,1,-1);
  EXPECT_EQ(edge.x*partial.texel_width/4u,1944u);
  EXPECT_EQ(edge.y*partial.texel_height/4u,1728u);
}
TEST(FramebufferDensityMap, OversizedAllocationExactlyMatchesNativeSceneGrid) {
  const auto layout=Layout(1728);
  for(int level:{1,2,3})for(float radius:{.08f,.15f,.35f})
      for(float y:{-.73f,0.f,.51f})for(float x:{-.61f,0.f,.87f}) {
    const auto reference=GazeDensityMap(54,54,x,y,level,radius,.1f,.2f);
    std::vector<std::uint8_t> actual;
    ASSERT_TRUE(FillFramebufferGazeDensityMap(actual,layout,x,y,level,radius,172.8f,345.6f));
    for(unsigned row=0;row<68;++row)for(unsigned column=0;column<68;++column)
      for(unsigned channel=0;channel<2;++channel) {
        const auto index=(row*68+column)*2+channel;
        const auto expected=row<54 && column<54?reference[(row*54+column)*2+channel]:255;
        ASSERT_EQ(actual[index],expected) << row << ',' << column;
      }
  }
}
TEST(FramebufferDensityMap, NonMultipleExtentUsesPhysicalCellsAndNeutralPadding) {
  const auto layout=Layout(1944);
  std::vector<std::uint8_t> pixels;
  ASSERT_TRUE(FillFramebufferGazeDensityMap(pixels,layout,1,-1,3,.08f,0,0));
  EXPECT_EQ(pixels[(60*68+60)*2],255);
  EXPECT_EQ(pixels[0],63);
  for(unsigned y=0;y<68;++y)for(unsigned x=0;x<68;++x)
    if(x>=61 || y>=61) {EXPECT_EQ(pixels[(y*68+x)*2],255);}
}
TEST(FramebufferDensityMap, EveryIntersectingHardwareRectangleKeepsFullDensity) {
  const auto layout=Layout(1944,1728);
  for(float gaze_x:{-.8f,.11f,.7f})for(float gaze_y:{-.7f,.23f,.8f}) {
    const float cx=(gaze_x+1)*.5f*layout.width,cy=(1-gaze_y)*.5f*layout.height;
    std::vector<std::uint8_t> pixels;
    ASSERT_TRUE(FillFramebufferGazeDensityMap(pixels,layout,gaze_x,gaze_y,3,.15f,512,512));
    for(unsigned w:{384u,576u,1024u})for(unsigned h:{448u,576u,1024u})
      for(unsigned y=0;y<layout.height;y+=h)for(unsigned x=0;x<layout.width;x+=w) {
        const auto right=std::min(x+w,layout.width),bottom=std::min(y+h,layout.height);
        const float dx=std::max({float(x)-cx,0.f,cx-right})/layout.width;
        const float dy=std::max({float(y)-cy,0.f,cy-bottom})/layout.height;
        if(std::hypot(dx,dy)>.15f)continue;
        const auto sample_x=((x+right)/2)/layout.texel_width;
        const auto sample_y=((y+bottom)/2)/layout.texel_height;
        EXPECT_EQ(pixels[(sample_y*68+sample_x)*2],255);
        EXPECT_EQ(pixels[(sample_y*68+sample_x)*2+1],255);
      }
  }
}
TEST(FramebufferDensityMap, QuantizationCacheAndFilledCoordinatesAgree) {
  const auto layout=Layout(1728);
  const auto a=QuantizeFramebufferGaze(layout,.173f,-.217f);
  const auto b=QuantizeFramebufferGaze(layout,.173001f,-.217001f);
  ASSERT_EQ(a.x,b.x);ASSERT_EQ(a.y,b.y);
  std::vector<std::uint8_t> first,second;
  ASSERT_TRUE(FillFramebufferGazeDensityMap(first,layout,.173f,-.217f,3,.15f,128,128));
  ASSERT_TRUE(FillFramebufferGazeDensityMap(second,layout,.173001f,-.217001f,3,.15f,128,128));
  EXPECT_EQ(first,second);
}
TEST(FramebufferDensityMap, CacheNeverReusesAnotherExtentOrOverwritesWithinFrame) {
  GazeDensityMapKey previous{108,108,1728,1728,68,68,32,32,3,.15f};
  EXPECT_EQ(DensityCacheAction(true,true,9,10,previous,previous),GazeDensityCacheAction::kReuse);
  auto requested=previous;requested.width=1944;
  EXPECT_EQ(DensityCacheAction(true,true,9,9,previous,requested),GazeDensityCacheAction::kNeutral);
  EXPECT_EQ(DensityCacheAction(true,true,9,10,previous,requested),GazeDensityCacheAction::kUpload);
  requested=previous;requested.texel_width=64;
  EXPECT_EQ(DensityCacheAction(true,true,9,9,previous,requested),GazeDensityCacheAction::kNeutral);
  EXPECT_EQ(DensityCacheAction(false,false,0,1,previous,previous),GazeDensityCacheAction::kUpload);
}
TEST(FramebufferDensityMap, InvalidInputsAndDisabledModeRemainNeutral) {
  std::vector<std::uint8_t> pixels;
  auto layout=Layout(1944);
  EXPECT_FALSE(FillFramebufferGazeDensityMap(pixels,layout,NAN,0,3,.15f,512,512));
  for(auto value:pixels)EXPECT_EQ(value,255);
  EXPECT_FALSE(FillFramebufferGazeDensityMap(pixels,layout,0,0,3,.15f,-1,512));
  EXPECT_TRUE(FillFramebufferGazeDensityMap(pixels,layout,0,0,0,.15f,512,512));
  for(auto value:pixels)EXPECT_EQ(value,255);
  layout.valid=false;
  EXPECT_FALSE(FillFramebufferGazeDensityMap(pixels,layout,0,0,3,.15f,512,512));
}
TEST(FramebufferDensityMap, CacheNeverSharesCalibratedGuardWithAnotherPassContract) {
  GazeDensityMapKey previous{136,136,2160,2160,68,68,32,32,3,.15f};
  previous.guard_width=768;previous.guard_height=448;
  previous.color_format=37;previous.depth_format=126;previous.samples=1;previous.attachments=2;
  for(unsigned field=0;field<6;++field) {
    auto requested=previous;
    switch(field) {
      case 0:requested.guard_width=1024;break;
      case 1:requested.guard_height=1024;break;
      case 2:requested.color_format=97;break;
      case 3:requested.depth_format=129;break;
      case 4:requested.samples=2;break;
      case 5:requested.attachments=3;break;
    }
    EXPECT_EQ(DensityCacheAction(true,true,9,9,previous,requested),GazeDensityCacheAction::kNeutral);
    EXPECT_EQ(DensityCacheAction(true,true,9,10,previous,requested),GazeDensityCacheAction::kUpload);
  }
  EXPECT_EQ(DensityCacheAction(true,true,9,9,previous,previous),GazeDensityCacheAction::kReuse);
}
TEST(FramebufferDensityMap, ReusesAllocatedPixelStorage) {
  std::vector<std::uint8_t> pixels;
  const auto layout=Layout(1944);
  ASSERT_TRUE(FillFramebufferGazeDensityMap(pixels,layout,0,0,3,.15f,512,512));
  const auto* storage=pixels.data();
  ASSERT_TRUE(FillFramebufferGazeDensityMap(pixels,layout,.7f,-.7f,3,.15f,512,512));
  EXPECT_EQ(pixels.data(),storage);
}
}  // namespace
}  // namespace mocktail::vr::internal
