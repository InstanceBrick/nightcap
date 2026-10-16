#include "resolution_default_policy.h"
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace nightcap::video {
namespace {
constexpr const char* kApp = "steam.app.123";
using Key = std::pair<std::string, std::string>;
struct Value { int value = 0; vr::EVRSettingsError error = vr::VRSettingsError_None; };
struct FakeSettings {
  std::map<Key, Value> values;
  std::vector<Key> reads, writes;
  vr::EVRSettingsError write_error = vr::VRSettingsError_None;
  int GetInt32(const char* section, const char* name, vr::EVRSettingsError* error) {
    const Key key{section, name}; reads.push_back(key);
    const auto it = values.find(key);
    *error = it == values.end() ? vr::VRSettingsError_UnsetSettingHasNoDefault : it->second.error;
    return it == values.end() ? 0 : it->second.value;
  }
  void SetInt32(const char* section, const char* name, int value, vr::EVRSettingsError* error) {
    const Key key{section, name}; writes.push_back(key);
    *error = write_error;
    if (*error == vr::VRSettingsError_None) values[key] = {value};
  }
};

struct FloatValue { float value = 0; vr::EVRSettingsError error = vr::VRSettingsError_None; };
// Deliberately no integer API: refresh rate must use OpenVR's float setting.
struct FakeRefreshSettings {
  std::map<Key, FloatValue> values;
  std::vector<Key> reads, writes;
  vr::EVRSettingsError write_error = vr::VRSettingsError_None;
  float GetFloat(const char* section, const char* name, vr::EVRSettingsError* error) {
    const Key key{section, name}; reads.push_back(key);
    const auto it = values.find(key);
    *error = it == values.end() ? vr::VRSettingsError_UnsetSettingHasNoDefault : it->second.error;
    return it == values.end() ? 0 : it->second.value;
  }
  void SetFloat(const char* section, const char* name, float value, vr::EVRSettingsError* error) {
    const Key key{section, name}; writes.push_back(key);
    *error = write_error;
    if (*error == vr::VRSettingsError_None) values[key] = {value};
  }
};

TEST(ResolutionDefaultPolicy, MissingAppOverrideSeeds2160WithoutChangingGlobalOrLegacy) {
  FakeSettings settings;
  settings.values[{"steamvr", "resolutionOverride"}] = {1760};
  settings.values[{"io.github.coderdayton.nightcap.vr", "resolutionOverride"}] = {1728};
  EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, true), ResolutionDefaultResult::kSeeded);
  ASSERT_EQ(settings.writes.size(), 1u);
  EXPECT_EQ(settings.writes[0], (Key{kApp, "resolutionOverride"}));
  EXPECT_EQ(settings.values.at(settings.writes[0]).value, 2160);
  EXPECT_EQ(settings.values.at({"steamvr", "resolutionOverride"}).value, 1760);
  EXPECT_EQ(settings.values.at({"io.github.coderdayton.nightcap.vr", "resolutionOverride"}).value, 1728);
  EXPECT_EQ(settings.reads.size(), 1u);
}

TEST(ResolutionDefaultPolicy, AutomaticZeroSelectsTheSame2160Default) {
  FakeSettings settings;
  settings.values[{kApp, "resolutionOverride"}] = {0};
  EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, true), ResolutionDefaultResult::kSeeded);
  EXPECT_EQ(settings.values.at({kApp, "resolutionOverride"}).value, 2160);
}

TEST(ResolutionDefaultPolicy, PreservesEveryExplicitNonzeroSelection) {
  for (const int pixels : {-1, 1, 128, 1728, 2160, 3216, 8192, 8193}) {
    FakeSettings settings;
    settings.values[{kApp, "resolutionOverride"}] = {pixels};
    EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, true), ResolutionDefaultResult::kUnchanged);
    EXPECT_TRUE(settings.writes.empty());
    EXPECT_EQ(settings.values.at({kApp, "resolutionOverride"}).value, pixels);
  }
}

TEST(ResolutionDefaultPolicy, FailedIdentificationAndMissingOrUnrelatedIdentityNeverAccessSettings) {
  FakeSettings settings;
  EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, false), ResolutionDefaultResult::kUnchanged);
  for (const char* app : {static_cast<const char*>(nullptr), "", "steamvr", "other.app",
                         "steam.app.", "steam.app.0", "steam.app.-1", "steam.app.1x",
                         "steam.app.4294967296", "steam.app.12345678901"})
    EXPECT_EQ(SeedAppResolutionDefault(settings, app, true), ResolutionDefaultResult::kUnchanged);
  EXPECT_TRUE(settings.reads.empty());
  EXPECT_TRUE(settings.writes.empty());
}

TEST(ResolutionDefaultPolicy, BothCanonicalIdentitySpellingsAndFullSteamIdAreSupported) {
  for (const char* app : {"io.github.CoderDayton.nightcap.vr",
                         "io.github.coderdayton.nightcap.vr", "steam.app.4294967295"}) {
    FakeSettings settings;
    EXPECT_EQ(SeedAppResolutionDefault(settings, app, true), ResolutionDefaultResult::kSeeded);
    ASSERT_EQ(settings.writes.size(), 1u);
    EXPECT_EQ(settings.writes[0], (Key{app, "resolutionOverride"}));
  }
}

TEST(ResolutionDefaultPolicy, ReadErrorsArePreservedInsteadOfBeingTreatedAsAutomatic) {
  for (const auto error : {vr::VRSettingsError_IPCFailed, vr::VRSettingsError_WriteFailed,
                           vr::VRSettingsError_ReadFailed, vr::VRSettingsError_JsonParseFailed}) {
    FakeSettings settings;
    settings.values[{kApp, "resolutionOverride"}] = {0, error};
    EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, true), ResolutionDefaultResult::kReadError);
    EXPECT_TRUE(settings.writes.empty());
  }
}

TEST(ResolutionDefaultPolicy, WriteFailureDoesNotReportSuccessAndCanBeRetried) {
  FakeSettings settings;
  settings.write_error = vr::VRSettingsError_WriteFailed;
  EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, true), ResolutionDefaultResult::kWriteError);
  EXPECT_EQ(settings.values.count({kApp, "resolutionOverride"}), 0u);
  settings.write_error = vr::VRSettingsError_None;
  EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, true), ResolutionDefaultResult::kSeeded);
}

TEST(ResolutionDefaultPolicy, SubsequentIdentificationPreservesTheSavedDefault) {
  FakeSettings settings;
  EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, true), ResolutionDefaultResult::kSeeded);
  EXPECT_EQ(SeedAppResolutionDefault(settings, kApp, true), ResolutionDefaultResult::kUnchanged);
  EXPECT_EQ(settings.writes.size(), 1u);
}

TEST(RefreshRateDefaultPolicy, MissingAppPreferenceSeeds72WithoutChangingGlobalOrLegacy) {
  FakeRefreshSettings settings;
  settings.values[{"steamvr", "preferredRefreshRate"}] = {90.f};
  settings.values[{"io.github.coderdayton.nightcap.vr", "preferredRefreshRate"}] = {96.f};
  EXPECT_EQ(SeedAppRefreshRateDefault(settings, kApp, true), RefreshRateDefaultResult::kSeeded);
  ASSERT_EQ(settings.writes.size(), 1u);
  EXPECT_EQ(settings.writes[0], (Key{kApp, "preferredRefreshRate"}));
  EXPECT_FLOAT_EQ(settings.values.at(settings.writes[0]).value, 72.f);
  EXPECT_FLOAT_EQ(settings.values.at({"steamvr", "preferredRefreshRate"}).value, 90.f);
  EXPECT_FLOAT_EQ(settings.values.at({"io.github.coderdayton.nightcap.vr", "preferredRefreshRate"}).value, 96.f);
  EXPECT_EQ(settings.reads.size(), 1u);
}

TEST(RefreshRateDefaultPolicy, AutomaticZeroSelects72) {
  for (const float hz : {0.f, -0.f}) {
    FakeRefreshSettings settings;
    settings.values[{kApp, "preferredRefreshRate"}] = {hz};
    EXPECT_EQ(SeedAppRefreshRateDefault(settings, kApp, true), RefreshRateDefaultResult::kSeeded);
    EXPECT_FLOAT_EQ(settings.values.at({kApp, "preferredRefreshRate"}).value, 72.f);
  }
}

TEST(RefreshRateDefaultPolicy, ExplicitNonzeroChoicesAreNeverChanged) {
  for (const float hz : {-1.f, 1.f, 60.f, 72.f, 90.f, 96.f, 120.f, 72.5f,
                         std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::quiet_NaN()}) {
    FakeRefreshSettings settings;
    settings.values[{kApp, "preferredRefreshRate"}] = {hz};
    EXPECT_EQ(SeedAppRefreshRateDefault(settings, kApp, true), RefreshRateDefaultResult::kUnchanged);
    EXPECT_TRUE(settings.writes.empty());
  }
}

TEST(RefreshRateDefaultPolicy, FailedIdentificationOrUntrustedAppNeverAccessesSettings) {
  FakeRefreshSettings settings;
  EXPECT_EQ(SeedAppRefreshRateDefault(settings, kApp, false), RefreshRateDefaultResult::kUnchanged);
  for (const char* app : {static_cast<const char*>(nullptr), "", "steamvr", "other.app",
                         "steam.app.", "steam.app.0", "steam.app.-1", "steam.app.1x",
                         "steam.app.4294967296", "steam.app.12345678901"})
    EXPECT_EQ(SeedAppRefreshRateDefault(settings, app, true), RefreshRateDefaultResult::kUnchanged);
  EXPECT_TRUE(settings.reads.empty()); EXPECT_TRUE(settings.writes.empty());
}

TEST(RefreshRateDefaultPolicy, BothCanonicalSpellingsAndFullSteamIdAreSupported) {
  for (const char* app : {"io.github.CoderDayton.nightcap.vr",
                         "io.github.coderdayton.nightcap.vr", "steam.app.4294967295"}) {
    FakeRefreshSettings settings;
    EXPECT_EQ(SeedAppRefreshRateDefault(settings, app, true), RefreshRateDefaultResult::kSeeded);
    ASSERT_EQ(settings.writes.size(), 1u);
    EXPECT_EQ(settings.writes[0], (Key{app, "preferredRefreshRate"}));
  }
}

TEST(RefreshRateDefaultPolicy, ReadErrorsArePreservedInsteadOfTreatedAsAutomatic) {
  for (const auto error : {vr::VRSettingsError_IPCFailed, vr::VRSettingsError_WriteFailed,
                           vr::VRSettingsError_ReadFailed, vr::VRSettingsError_JsonParseFailed}) {
    FakeRefreshSettings settings;
    settings.values[{kApp, "preferredRefreshRate"}] = {0.f, error};
    EXPECT_EQ(SeedAppRefreshRateDefault(settings, kApp, true), RefreshRateDefaultResult::kReadError);
    EXPECT_TRUE(settings.writes.empty());
  }
}

TEST(RefreshRateDefaultPolicy, WriteFailureCanBeRetriedAndRepeatedIdentificationIsIdempotent) {
  FakeRefreshSettings settings;
  settings.write_error = vr::VRSettingsError_WriteFailed;
  EXPECT_EQ(SeedAppRefreshRateDefault(settings, kApp, true), RefreshRateDefaultResult::kWriteError);
  EXPECT_TRUE(settings.values.empty());
  settings.write_error = vr::VRSettingsError_None;
  EXPECT_EQ(SeedAppRefreshRateDefault(settings, kApp, true), RefreshRateDefaultResult::kSeeded);
  EXPECT_EQ(SeedAppRefreshRateDefault(settings, kApp, true), RefreshRateDefaultResult::kUnchanged);
  EXPECT_EQ(settings.writes.size(), 2u);
}
}  // namespace
}  // namespace nightcap::video
