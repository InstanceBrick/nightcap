#pragma once

#include "openvr.h"
#include <cstdint>

namespace nightcap::video {

enum class PacingPolicyResult { kUnchanged, kRestored, kReadError, kWriteError };

// SteamVR may remove an explicit zero when saving video settings. An absent
// app/global throttle permits automatic half rate even with smoothing off.
// Restore zero only for our focused scene and an entirely unspecified cap;
// every explicit app/global cap and every other smoothing mode is preserved.
template<class Settings>
PacingPolicyResult RestoreUncappedPacing(Settings& settings, const char* app,
                                       std::uint32_t expected_pid,
                                       std::uint32_t scene_pid) {
  if (!app || !*app || !expected_pid || scene_pid != expected_pid)
    return PacingPolicyResult::kUnchanged;
  vr::EVRSettingsError error = vr::VRSettingsError_None;
  const int smoothing = settings.GetInt32(app, "motionSmoothingOverride", &error);
  if (error != vr::VRSettingsError_None) return PacingPolicyResult::kReadError;
  if (smoothing != 2) return PacingPolicyResult::kUnchanged;
  (void)settings.GetInt32(app, "framesToThrottle", &error);
  if (error == vr::VRSettingsError_None) return PacingPolicyResult::kUnchanged;
  if (error != vr::VRSettingsError_UnsetSettingHasNoDefault)
    return PacingPolicyResult::kReadError;
  (void)settings.GetInt32("steamvr", "framesToThrottle", &error);
  if (error == vr::VRSettingsError_None) return PacingPolicyResult::kUnchanged;
  if (error != vr::VRSettingsError_UnsetSettingHasNoDefault)
    return PacingPolicyResult::kReadError;
  settings.SetInt32(app, "framesToThrottle", 0, &error);
  return error == vr::VRSettingsError_None ? PacingPolicyResult::kRestored
                                          : PacingPolicyResult::kWriteError;
}

}  // namespace nightcap::video
