#include "mocktail/vr/anti_aliasing_settings.h"
#include <gtest/gtest.h>
#include <limits>

namespace mocktail::vr::internal {
TEST(AntiAliasingSettings, PreservesTheFourSpatialChoices) {
  for (int mode=0;mode<=3;++mode) EXPECT_EQ(NativeAntiAliasingMode(mode),mode);
}
TEST(AntiAliasingSettings, RetiredMultisampleAndHybridChoicesUseSmaaHigh) {
  for (const int mode : {4,5,6,7,8,16,100}) EXPECT_EQ(NativeAntiAliasingMode(mode),3);
}
TEST(AntiAliasingSettings, InvalidValuesCannotWrapIntoAnotherChoice) {
  EXPECT_EQ(NativeAntiAliasingMode(-1),3);
  EXPECT_EQ(NativeAntiAliasingMode(std::numeric_limits<std::int64_t>::max()),3);
  EXPECT_EQ(NativeAntiAliasingMode(std::numeric_limits<std::int64_t>::min()),3);
  EXPECT_EQ(NativeAntiAliasingMode(0x100000001LL),3);
}
}  // namespace mocktail::vr::internal
