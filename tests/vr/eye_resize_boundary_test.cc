#include "mocktail/vr/eye_resize_boundary.h"
#include <gtest/gtest.h>

namespace mocktail::vr::internal {
TEST(EyeResizeBoundary, RequiresASeenPresentOnThisRenderThread) {
  EyeResizeBoundary boundary;
  EXPECT_FALSE(boundary.CanResize());
  boundary.OnPresent();
  EXPECT_TRUE(boundary.CanResize());
}
TEST(EyeResizeBoundary, BorrowedEyeBlocksResizeForTheRestOfTheFrame) {
  EyeResizeBoundary boundary;
  boundary.OnPresent();
  boundary.OnEyeGetter();
  for(int state_read=0;state_read<20;++state_read) EXPECT_FALSE(boundary.CanResize());
  boundary.OnEyeGetter();
  EXPECT_FALSE(boundary.CanResize());
}
TEST(EyeResizeBoundary, NextPresentOpensANewSafeBoundary) {
  EyeResizeBoundary boundary;
  for(int frame=0;frame<120;++frame) {
    boundary.OnPresent();
    EXPECT_TRUE(boundary.CanResize());
    boundary.OnEyeGetter();
    EXPECT_FALSE(boundary.CanResize());
  }
}
TEST(EyeResizeBoundary, AnEarlyEyeReadDoesNotAuthorizeResizeBeforePresent) {
  EyeResizeBoundary boundary;
  boundary.OnEyeGetter();
  EXPECT_FALSE(boundary.CanResize());
  boundary.OnPresent();
  EXPECT_TRUE(boundary.CanResize());
}
TEST(EyeResizeBoundary, AnotherRenderThreadsPresentCannotAuthorizeAReader) {
  EyeResizeBoundary presenting,reader;
  presenting.OnPresent();
  reader.OnEyeGetter();
  EXPECT_TRUE(presenting.CanResize());
  EXPECT_FALSE(reader.CanResize());
}
}  // namespace mocktail::vr::internal
