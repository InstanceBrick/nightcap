#pragma once

#include "openvr.h"
#include <cstdint>
#include <string_view>

namespace nightcap::video {

enum class ResolutionDefaultResult { kUnchanged, kSeeded, kReadError, kWriteError };
enum class RefreshRateDefaultResult { kUnchanged, kSeeded, kReadError, kWriteError };

inline bool IsNightcapResolutionApp(std::string_view app) {
  if (app == "io.github.CoderDayton.nightcap.vr" ||
      app == "io.github.coderdayton.nightcap.vr") return true;
  constexpr std::string_view prefix = "steam.app.";
  if (app.substr(0, prefix.size()) != prefix) return false;
  const auto id = app.substr(prefix.size());
  if (id.empty() || id.size() > 10) return false;
  std::uint64_t value = 0;
  for (const char digit : id) {
    if (digit < '0' || digit > '9') return false;
    value = value * 10 + static_cast<unsigned>(digit - '0');
  }
  return value > 0 && value <= UINT32_MAX;
}

// Called after identifying our scene process. Persist the app's native default
// so Steam's Automatic GPU recommendation cannot display a different size.
template<class Settings>
ResolutionDefaultResult SeedAppResolutionDefault(Settings& settings,
    const char* app, bool identification_succeeded) {
  if (!identification_succeeded || !app || !IsNightcapResolutionApp(app))
    return ResolutionDefaultResult::kUnchanged;
  vr::EVRSettingsError error = vr::VRSettingsError_None;
  const int pixels = settings.GetInt32(app, "resolutionOverride", &error);
  if (error != vr::VRSettingsError_None &&
      error != vr::VRSettingsError_UnsetSettingHasNoDefault)
    return ResolutionDefaultResult::kReadError;
  if (error == vr::VRSettingsError_None && pixels != 0)
    return ResolutionDefaultResult::kUnchanged;
  settings.SetInt32(app, "resolutionOverride", 2160, &error);
  return error == vr::VRSettingsError_None ? ResolutionDefaultResult::kSeeded
                                          : ResolutionDefaultResult::kWriteError;
}

// Per-application automatic/missing refresh selects the native 72 Hz target.
// Explicit choices, including unsupported nonzero values, remain the user's.
template<class Settings>
RefreshRateDefaultResult SeedAppRefreshRateDefault(Settings& settings,
    const char* app, bool identification_succeeded) {
  if (!identification_succeeded || !app || !IsNightcapResolutionApp(app))
    return RefreshRateDefaultResult::kUnchanged;
  vr::EVRSettingsError error = vr::VRSettingsError_None;
  const float hz = settings.GetFloat(app, vr::k_pch_SteamVR_PreferredRefreshRate, &error);
  if (error != vr::VRSettingsError_None &&
      error != vr::VRSettingsError_UnsetSettingHasNoDefault)
    return RefreshRateDefaultResult::kReadError;
  if (error == vr::VRSettingsError_None && hz != 0.f)
    return RefreshRateDefaultResult::kUnchanged;
  settings.SetFloat(app, vr::k_pch_SteamVR_PreferredRefreshRate, 72.f, &error);
  return error == vr::VRSettingsError_None ? RefreshRateDefaultResult::kSeeded
                                          : RefreshRateDefaultResult::kWriteError;
}

}  // namespace nightcap::video
