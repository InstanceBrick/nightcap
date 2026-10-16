#include "pacing_policy.h"
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace nightcap::video {
namespace {
constexpr const char* kApp = "steam.app.123";
constexpr std::uint32_t kPid = 1234;
using Key = std::pair<std::string, std::string>;
struct Value { int value = 0; vr::EVRSettingsError error = vr::VRSettingsError_None; };
struct FakeSettings {
  FakeSettings() { values[{kApp, "motionSmoothingOverride"}] = {2}; }
  std::map<Key, Value> values;
  std::vector<Key> reads, writes;
  vr::EVRSettingsError write_error = vr::VRSettingsError_None;
  int GetInt32(const char* section, const char* name, vr::EVRSettingsError* error) {
    Key key{section, name}; reads.push_back(key);
    const auto it = values.find(key);
    *error = it == values.end() ? vr::VRSettingsError_UnsetSettingHasNoDefault : it->second.error;
    return it == values.end() ? 0 : it->second.value;
  }
  void SetInt32(const char* section, const char* name, int value, vr::EVRSettingsError* error) {
    Key key{section, name}; writes.push_back(key);
    *error = write_error;
    if (*error == vr::VRSettingsError_None) values[key] = {value};
  }
};
PacingPolicyResult Check(FakeSettings& settings) {
  return RestoreUncappedPacing(settings, kApp, kPid, kPid);
}

TEST(PacingPolicy, RestoresAppZeroOnlyWhenBothCapsAreUnset) {
  FakeSettings settings;
  EXPECT_EQ(Check(settings), PacingPolicyResult::kRestored);
  ASSERT_EQ(settings.writes.size(), 1u);
  EXPECT_EQ(settings.writes[0], (Key{kApp, "framesToThrottle"}));
  EXPECT_EQ(settings.values.at({kApp, "framesToThrottle"}).value, 0);
  EXPECT_EQ(settings.reads.size(), 3u);
  EXPECT_EQ(settings.values.count({"steamvr", "framesToThrottle"}), 0u);
}
TEST(PacingPolicy, PreservesExplicitAppZero) {
  FakeSettings settings;
  settings.values[{kApp, "framesToThrottle"}] = {0};
  EXPECT_EQ(Check(settings), PacingPolicyResult::kUnchanged);
  EXPECT_TRUE(settings.writes.empty());
  EXPECT_EQ(settings.reads.size(), 2u);
}
TEST(PacingPolicy, PreservesExplicitAppHalfRateAndOtherCaps) {
  for (const int value : {1, 2, 5, -1}) {
    FakeSettings settings;
    settings.values[{kApp, "framesToThrottle"}] = {value};
    EXPECT_EQ(Check(settings), PacingPolicyResult::kUnchanged);
    EXPECT_TRUE(settings.writes.empty());
    EXPECT_EQ(settings.values.at({kApp, "framesToThrottle"}).value, value);
  }
}
TEST(PacingPolicy, PreservesExplicitGlobalHalfRate) {
  FakeSettings settings;
  settings.values[{"steamvr", "framesToThrottle"}] = {1};
  EXPECT_EQ(Check(settings), PacingPolicyResult::kUnchanged);
  EXPECT_TRUE(settings.writes.empty());
}
TEST(PacingPolicy, PreservesExplicitGlobalZero) {
  FakeSettings settings;
  settings.values[{"steamvr", "framesToThrottle"}] = {0};
  EXPECT_EQ(Check(settings), PacingPolicyResult::kUnchanged);
  EXPECT_TRUE(settings.writes.empty());
}
TEST(PacingPolicy, PreservesForceHalfAndAllOtherSmoothingOverrides) {
  for (const int value : {0, 1, 3, 4}) {
    FakeSettings settings;
    settings.values[{kApp, "motionSmoothingOverride"}] = {value};
    EXPECT_EQ(Check(settings), PacingPolicyResult::kUnchanged);
    EXPECT_TRUE(settings.writes.empty());
    EXPECT_EQ(settings.reads.size(), 1u);
  }
}
TEST(PacingPolicy, NeverReadsOrWritesForAnotherSceneOrMissingIdentity) {
  FakeSettings settings;
  EXPECT_EQ(RestoreUncappedPacing(settings, kApp, kPid, kPid + 1), PacingPolicyResult::kUnchanged);
  EXPECT_EQ(RestoreUncappedPacing(settings, kApp, 0, 0), PacingPolicyResult::kUnchanged);
  EXPECT_EQ(RestoreUncappedPacing(settings, "", kPid, kPid), PacingPolicyResult::kUnchanged);
  EXPECT_EQ(RestoreUncappedPacing(settings, nullptr, kPid, kPid), PacingPolicyResult::kUnchanged);
  EXPECT_TRUE(settings.reads.empty());
  EXPECT_TRUE(settings.writes.empty());
}
TEST(PacingPolicy, ReadFailuresAreNotTreatedAsMissingSettings) {
  for (const Key& key : {Key{kApp, "motionSmoothingOverride"},
                         Key{kApp, "framesToThrottle"},
                         Key{"steamvr", "framesToThrottle"}}) {
    FakeSettings settings;
    settings.values[key] = {0, vr::VRSettingsError_ReadFailed};
    EXPECT_EQ(Check(settings), PacingPolicyResult::kReadError);
    EXPECT_TRUE(settings.writes.empty());
  }
  FakeSettings settings;
  settings.values.erase({kApp, "motionSmoothingOverride"});
  EXPECT_EQ(Check(settings), PacingPolicyResult::kReadError);
  EXPECT_TRUE(settings.writes.empty());
}
TEST(PacingPolicy, WriteFailureIsReportedAndMayBeRetried) {
  FakeSettings settings;
  settings.write_error = vr::VRSettingsError_WriteFailed;
  EXPECT_EQ(Check(settings), PacingPolicyResult::kWriteError);
  EXPECT_EQ(settings.values.count({kApp, "framesToThrottle"}), 0u);
  settings.write_error = vr::VRSettingsError_None;
  EXPECT_EQ(Check(settings), PacingPolicyResult::kRestored);
  EXPECT_EQ(settings.writes.size(), 2u);
}
TEST(PacingPolicy, RepairsZeroAgainAfterSteamVrSavesVideoSettings) {
  FakeSettings settings;
  EXPECT_EQ(Check(settings), PacingPolicyResult::kRestored);
  EXPECT_EQ(Check(settings), PacingPolicyResult::kUnchanged);
  EXPECT_EQ(settings.writes.size(), 1u);
  settings.values.erase({kApp, "framesToThrottle"});
  EXPECT_EQ(Check(settings), PacingPolicyResult::kRestored);
  EXPECT_EQ(settings.writes.size(), 2u);
}
}  // namespace
}  // namespace nightcap::video
