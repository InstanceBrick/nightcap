#include <algorithm>
#include "openvr.h"
#include "pacing_policy.h"
#include "mocktail/vr/performance_telemetry.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <fontconfig/fontconfig.h>
#include <dlfcn.h>
#include <signal.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int kWidth = 720, kHeight = 204;
volatile sig_atomic_t stopped = 0;
void Stop(int) { stopped = 1; }
std::uint64_t Now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

class Panel {
 public:
  ~Panel() {
    if (font_) TTF_CloseFont(font_);
    if (surface_) SDL_DestroySurface(surface_);
    if (initialized_) TTF_Quit();
  }
  bool Init() {
    if (surface_) return true;
    if (!TTF_Init()) return false;
    initialized_ = true;
    if (!FcInit()) return false;
    FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>("sans:weight=medium"));
    if (!pattern) return false;
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    if (!match) return false;
    FcChar8* file = nullptr;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
      font_ = TTF_OpenFont(reinterpret_cast<const char*>(file), 32.f);
    FcPatternDestroy(match);
    if (!font_) return false;
    surface_ = SDL_CreateSurface(kWidth, kHeight, SDL_PIXELFORMAT_RGBA32);
    return surface_ != nullptr;
  }
  bool Draw(double fps, double hz, double gpu_ms,
            const mocktail::vr::PerformanceSnapshot& frame, bool stale,
            double target_fps = 0.) {
    const double target = target_fps > 0 ? target_fps : hz;
    if (!Init()) return false;
    SDL_FillSurfaceRect(surface_, nullptr, SDL_MapSurfaceRGBA(surface_, 15, 19, 26, 255));
    char a[128], b[128], c[128], d[128], fps_text[24], hz_text[24], gpu_text[24];
    std::snprintf(fps_text, sizeof fps_text, fps >= 0 ? "%.1f" : "--", fps);
    std::snprintf(hz_text, sizeof hz_text, hz > 0 ? "%.0f" : "--", hz);
    std::snprintf(gpu_text, sizeof gpu_text, gpu_ms >= 0 ? "%.2f" : "--", gpu_ms);
    std::snprintf(a, sizeof a, "APP %s FPS     HMD %s Hz", fps_text, hz_text);
    if (stale) std::snprintf(b, sizeof b, "NO RECENT GAME FRAMES");
    else if (!frame.visible) std::snprintf(b, sizeof b, "STANDBY");
    else if (fps > 0 && target > 0) std::snprintf(b, sizeof b, "FRAME %.2f ms   TARGET %.0f FPS", 1000. / fps, target);
    else if (fps > 0) std::snprintf(b, sizeof b, "FRAME %.2f ms", 1000. / fps);
    else std::snprintf(b, sizeof b, fps == 0 ? "FRAME -- ms" : "MEASURING...");
    if (target > 0) std::snprintf(c, sizeof c, "GPU %s ms     BUDGET %.2f ms", gpu_text, 1000. / target);
    else std::snprintf(c, sizeof c, "GPU %s ms     BUDGET -- ms", gpu_text);
    if (frame.render_width && frame.render_width != frame.width)
      std::snprintf(d, sizeof d, "RENDER %llu  >  OUTPUT %llu",
                    (unsigned long long)frame.render_width, (unsigned long long)frame.width);
    else if (frame.width && frame.height)
      std::snprintf(d, sizeof d, "%llu x %llu per eye", (unsigned long long)frame.width,
                    (unsigned long long)frame.height);
    else std::snprintf(d, sizeof d, "EYE RESOLUTION --");
    SDL_Color accent{133, 238, 196, 255};
    if (fps >= 0 && target > 0 && fps < target * .97) accent = {255, 200, 112, 255};
    return Text(a, 10, accent) && Text(b, 56, {237, 242, 248, 255}) &&
           Text(c, 102, {198, 210, 226, 255}) && Text(d, 148, {198, 210, 226, 255});
  }
  SDL_Surface* surface() const { return surface_; }
 private:
  bool Text(const char* text, int y, SDL_Color color) {
    SDL_Surface* rendered = TTF_RenderText_Blended(font_, text, 0, color);
    if (!rendered) return false;
    SDL_Rect target{20, y, rendered->w, rendered->h};
    const bool ok = SDL_BlitSurface(rendered, nullptr, surface_, &target);
    SDL_DestroySurface(rendered);
    return ok;
  }
  bool initialized_ = false;
  TTF_Font* font_ = nullptr;
  SDL_Surface* surface_ = nullptr;
};

// Raw uploads finish asynchronously. Keep the displayed panel untouched
// while its hidden partner loads, then show the ready texture before hiding
// the old panel. This avoids blank frames during statistics updates.
class BufferedPanel {
 public:
  BufferedPanel(vr::IVROverlay* api, std::string key) : api_(api), key_(std::move(key)) {}
  bool Create() {
    for (int i=0; i<2; ++i) {
      if (handles_[i] != vr::k_ulOverlayHandleInvalid) continue;
      const auto name = key_ + (i ? ".back" : "");
      const auto error = api_->CreateOverlay(name.c_str(), "Performance Overlay", &handles_[i]);
      if (error != vr::VROverlayError_None) return false;
      vr::HmdMatrix34_t pose{};
      pose.m[0][0] = pose.m[1][1] = pose.m[2][2] = 1.f;
      pose.m[1][3] = -.34f; pose.m[2][3] = -1.20f;
      api_->SetOverlayTransformTrackedDeviceRelative(handles_[i], vr::k_unTrackedDeviceIndex_Hmd, &pose);
      api_->SetOverlayWidthInMeters(handles_[i], .68f);
      api_->SetOverlayInputMethod(handles_[i], vr::VROverlayInputMethod_None);
      api_->SetOverlayTextureColorSpace(handles_[i], vr::ColorSpace_Gamma);
      api_->SetOverlaySortOrder(handles_[i], 5);
    }
    return true;
  }
  void Close() {
    for (auto& handle : handles_) {
      if (handle == vr::k_ulOverlayHandleInvalid) continue;
      api_->HideOverlay(handle);
      api_->DestroyOverlay(handle);
      handle = vr::k_ulOverlayHandleInvalid;
    }
    front_ = pending_ = -1; ready_ = false; shown_[0] = shown_[1] = false;
  }
  void ImageEvent(const vr::VREvent_t& event) {
    if (pending_ < 0) return;
    if (event.eventType == vr::VREvent_ImageLoaded) ready_ = true;
    if (event.eventType == vr::VREvent_ImageFailed) {
      pending_ = -1; ready_ = false;
      std::fprintf(stderr, "[performance-overlay] image upload deferred; retrying\n");
    }
  }
  void Pump(bool active) {
    for (int i=0; i<2; ++i) {
      if (handles_[i] == vr::k_ulOverlayHandleInvalid) continue;
      vr::VREvent_t event{};
      while (api_->PollNextOverlayEvent(handles_[i], &event, sizeof event)) {
        ImageEvent(event);
      }
    }
    // Some runtime builds do not deliver ImageLoaded to an OpenVR overlay
    // client. Confirm a ready texture after at least one update interval.
    if (pending_ >= 0 && !ready_ && Now() - pending_at_ >= 200'000'000ULL) {
      std::uint32_t width=0, height=0;
      ready_ = api_->GetOverlayTextureSize(handles_[pending_], &width, &height) == vr::VROverlayError_None &&
               width == kWidth && height == kHeight;
    }
    if (pending_ >= 0 && ready_ && active) {
      const int next = pending_;
      api_->SetOverlaySortOrder(handles_[next], 6);
      if (api_->ShowOverlay(handles_[next]) == vr::VROverlayError_None) {
        shown_[next] = true;
        if (front_ >= 0 && front_ != next) {
          // An opaque panel covers its predecessor for this one frame.
          api_->WaitFrameSync(50);
          api_->HideOverlay(handles_[front_]);
          api_->SetOverlaySortOrder(handles_[front_], 5);
          shown_[front_] = false;
        }
        front_ = next; pending_ = -1; ready_ = false;
      }
    }
    if (active && front_ >= 0 && !shown_[front_])
      shown_[front_] = api_->ShowOverlay(handles_[front_]) == vr::VROverlayError_None;
    if (!active) for (int i=0; i<2; ++i) {
      if (shown_[i]) api_->HideOverlay(handles_[i]);
      shown_[i] = false;
    }
    if (pending_ >= 0 && !ready_ && Now() - pending_at_ > 2'000'000'000ULL)
      pending_ = -1;  // Keep the last good panel if an image event was lost.
  }
  void Upload(SDL_Surface* surface) {
    if (pending_ >= 0 || !Create()) return;
    const int next = front_ == 0 ? 1 : 0;
    const auto error = api_->SetOverlayRaw(handles_[next], surface->pixels, kWidth, kHeight, 4);
    if (error != last_error_) {
      std::fprintf(stderr, "[performance-overlay] upload %s (%d)\n",
                   api_->GetOverlayErrorNameFromEnum(error), error);
      last_error_ = error;
    }
    if (error == vr::VROverlayError_None) {
      pending_ = next; ready_ = false; pending_at_ = Now();
    } else if (error == vr::VROverlayError_UnknownOverlay || error == vr::VROverlayError_InvalidHandle) {
      Close();
    }
  }
  bool CanUpload() const { return pending_ < 0; }
 private:
  vr::IVROverlay* api_;
  std::string key_;
  vr::VROverlayHandle_t handles_[2]{vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid};
  bool shown_[2]{false, false}, ready_ = false;
  int front_ = -1, pending_ = -1;
  std::uint64_t pending_at_ = 0;
  vr::EVROverlayError last_error_ = vr::VROverlayError_None;
};

bool Alive(unsigned pid) { return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM; }
}  // namespace

int main(int argc, char** argv) {
  Panel panel;
  if (argc == 3 && !std::strcmp(argv[1], "--preview")) {
    mocktail::vr::PerformanceSnapshot sample{};
    sample.visible = true; sample.width = sample.height = 2160;
    return panel.Draw(44.9, 90., 17.23, sample, false, 45.) &&
           SDL_SaveBMP(panel.surface(), argv[2]) ? 0 : 1;
  }
  if (argc != 3) { std::fprintf(stderr, "Usage: nightcap-vr-performance PID STATS_FILE\n"); return 2; }
  char* end = nullptr;
  const auto parsed = std::strtoul(argv[1], &end, 10);
  if (!end || *end || parsed == 0 || parsed > 0x7fffffffUL) return 2;
  const auto pid = static_cast<unsigned>(parsed);
  mocktail::vr::PerformanceMapping mapping;
  if (!mapping.Open(argv[2], false)) return 3;
  void* library = dlopen("/opt/steamvr/bin/linuxarm64/libopenvr_api.so", RTLD_NOW | RTLD_LOCAL);
  if (!library) return 4;
  auto init = reinterpret_cast<std::uint32_t(*)(vr::EVRInitError*,vr::EVRApplicationType,const char*)>(dlsym(library,"VR_InitInternal2"));
  auto get = reinterpret_cast<void*(*)(const char*,vr::EVRInitError*)>(dlsym(library,"VR_GetGenericInterface"));
  auto shutdown = reinterpret_cast<void(*)()>(dlsym(library,"VR_ShutdownInternal"));
  if (!init || !get || !shutdown) { dlclose(library); return 4; }
  vr::EVRInitError error = vr::VRInitError_None;
  init(&error, vr::VRApplication_Overlay, nullptr);
  if (error != vr::VRInitError_None) { dlclose(library); return 5; }
  auto* overlay = static_cast<vr::IVROverlay*>(get(vr::IVROverlay_Version,&error));
  auto* compositor = static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
  auto* system = static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
  auto* settings = static_cast<vr::IVRSettings*>(get(vr::IVRSettings_Version,&error));
  if (!overlay || !compositor || !system || !settings) { shutdown(); dlclose(library); return 6; }
  std::string app_section;
  if (const char* id = std::getenv("NIGHTCAP_STEAM_APP_ID"); id && *id &&
      std::all_of(id, id + std::strlen(id), [](char c) { return c >= '0' && c <= '9'; }))
    app_section = std::string("steam.app.") + id;
  const auto target_fps = [&](double hz) {
    vr::EVRSettingsError e = vr::VRSettingsError_None;
    int throttled = settings->GetInt32("steamvr", "framesToThrottle", &e);
    if (e != vr::VRSettingsError_None || throttled < 0 || throttled > 5) throttled = 0;
    if (!app_section.empty()) {
      const int per_app = settings->GetInt32(app_section.c_str(), "framesToThrottle", &e);
      if (e == vr::VRSettingsError_None && per_app >= 0 && per_app <= 5) throttled = per_app;
      const int smoothing = settings->GetInt32(app_section.c_str(), "motionSmoothingOverride", &e);
      if (e == vr::VRSettingsError_None && smoothing == 3 && compositor->IsMotionSmoothingEnabled())
        throttled = std::max(throttled, 1);
    }
    return hz > 0 ? hz / (1 + throttled) : 0.;
  };
  const std::string key = "io.github.CoderDayton.nightcap.performance." + std::to_string(pid);
  BufferedPanel buffered(overlay, key);
  if (!buffered.Create()) { buffered.Close(); shutdown(); dlclose(library); return 7; }
  signal(SIGTERM, Stop); signal(SIGINT, Stop);
  mocktail::vr::PerformanceRate rate;
  int exit_code = 0;
  std::fprintf(stderr, "[performance-overlay] ready for process %u\n", pid);
  auto next_pacing_check = std::chrono::steady_clock::time_point::min();
  while (!stopped && Alive(pid)) {
    const auto tick = std::chrono::steady_clock::now();
    vr::VREvent_t event{};
    while (system->PollNextEvent(&event, sizeof event)) {
      if (event.eventType == vr::VREvent_Quit) stopped = 1;
      buffered.ImageEvent(event);
    }
    vr::EVRSettingsError settings_error = vr::VRSettingsError_None;
    const bool enabled = settings->GetInt32("nightcap", "performanceOverlay", &settings_error) == 1 &&
                         settings_error == vr::VRSettingsError_None;
    const auto scene_pid = compositor->GetCurrentSceneFocusProcess();
    const bool own_scene = scene_pid == pid;
    if (tick >= next_pacing_check) {
      next_pacing_check = tick + std::chrono::seconds(1);
      if (nightcap::video::RestoreUncappedPacing(*settings, app_section.c_str(), pid, scene_pid) ==
          nightcap::video::PacingPolicyResult::kRestored)
        std::fprintf(stderr, "[vr-pacing] restored uncapped application rate after video settings save\n");
    }
    mocktail::vr::PerformanceSnapshot frame{};
    const bool have_frame = mapping.Read(&frame) && frame.process == pid;
    const bool active = enabled && own_scene && have_frame && !overlay->IsDashboardVisible() && !stopped;
    buffered.Pump(active);
    if (active && buffered.CanUpload()) {
      const auto now = Now();
      const bool stale = frame.timestamp_ns > now || now - frame.timestamp_ns > 1'000'000'000ULL;
      const double fps = rate.Sample(now, frame);
      const float hz = system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
                                                             vr::Prop_DisplayFrequency_Float);
      std::vector<vr::Compositor_FrameTiming> timings(32);
      for (auto& timing : timings) timing.m_nSize = sizeof(timing);
      const auto count = compositor->GetFrameTimings(timings.data(), timings.size());
      double gpu_sum = 0.; unsigned gpu_count = 0;
      for (unsigned i = 0; i < count; ++i)
        // Reprojected compositor-only frames have no new application work.
        // Including them would understate the GPU time of real game frames.
        if (std::isfinite(timings[i].m_flTotalRenderGpuMs) &&
            timings[i].m_flTotalRenderGpuMs > 0 &&
            (timings[i].m_flPreSubmitGpuMs > 0 || timings[i].m_flClientFrameIntervalMs > 0)) {
          gpu_sum += timings[i].m_flTotalRenderGpuMs; ++gpu_count;
        }
      const double gpu = !stale && frame.visible && gpu_count ? gpu_sum / gpu_count : -1.;
      if (!panel.Draw(fps, std::isfinite(hz) ? hz : 0, gpu, frame, stale,
                      target_fps(std::isfinite(hz) ? hz : 0))) {
        std::fprintf(stderr, "[performance-overlay] font or surface unavailable: %s\n", SDL_GetError());
        exit_code = 8; break;
      }
      buffered.Upload(panel.surface());
    } else if (!active) rate.Reset();
    std::this_thread::sleep_until(tick + std::chrono::milliseconds(250));
  }
  buffered.Close();
  shutdown(); dlclose(library);
  std::fprintf(stderr, "[performance-overlay] closed (%d)\n", exit_code);
  return exit_code;
}
