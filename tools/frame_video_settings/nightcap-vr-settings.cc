#include "openvr.h"
#include "pacing_policy.h"
#include "resolution_default_policy.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

int main(int argc, char** argv) {
  void* library = dlopen("/opt/steamvr/bin/linuxarm64/libopenvr_api.so", RTLD_NOW | RTLD_LOCAL);
  if (!library) { std::fprintf(stderr, "OpenVR: %s\n", dlerror()); return 2; }
  using Init = std::uint32_t (*)(vr::EVRInitError*, vr::EVRApplicationType, const char*);
  using Interface = void* (*)(const char*, vr::EVRInitError*);
  using Shutdown = void (*)();
  auto init = reinterpret_cast<Init>(dlsym(library, "VR_InitInternal2"));
  auto get = reinterpret_cast<Interface>(dlsym(library, "VR_GetGenericInterface"));
  auto shutdown = reinterpret_cast<Shutdown>(dlsym(library, "VR_ShutdownInternal"));
  if (!init || !get || !shutdown) return 2;
  vr::EVRInitError error = vr::VRInitError_None;
  init(&error, vr::VRApplication_Background, nullptr);
  if (error != vr::VRInitError_None) { std::fprintf(stderr, "OpenVR init error %d\n", error); return 3; }
  if(argc>=2 && (!std::strcmp(argv[1],"--register-app") || !std::strcmp(argv[1],"--identify"))) {
    auto* applications=static_cast<vr::IVRApplications*>(get(vr::IVRApplications_Version,&error));
    if(!applications || error!=vr::VRInitError_None){shutdown();return 3;}
    std::string identity="io.github.CoderDayton.nightcap.vr";
    if(const char* app=std::getenv("NIGHTCAP_STEAM_APP_ID")) {
      char* end=nullptr;const auto id=std::strtoul(app,&end,10);
      if(end && *end=='\0' && id>0 && id<=UINT32_MAX)identity="steam.app."+std::to_string(id);
    }
    const char* key=identity.c_str();
    const char* manifest_override=std::getenv("NIGHTCAP_VR_MANIFEST");
    const char* home=std::getenv("HOME");
    std::string manifest_path;
    if(manifest_override && manifest_override[0]=='/')manifest_path=manifest_override;
    else if(home && home[0]=='/')manifest_path=std::string(home)+"/.local/share/nightcap-vr-arm64/nightcap.vrmanifest";
    else {std::fprintf(stderr,"[vr-identity] application manifest path unavailable\n");shutdown();return 6;}
    auto result=applications->AddApplicationManifest(manifest_path.c_str(),false);
    if(result!=vr::VRApplicationError_None){std::fprintf(stderr,"[vr-identity] manifest error %d\n",result);shutdown();return 6;}
    if(!std::strcmp(argv[1],"--identify")) {
      if(argc!=3){shutdown();return 4;}
      char* end=nullptr;const auto pid=std::strtoul(argv[2],&end,10);
      if(!end || *end || !pid || pid>UINT32_MAX){shutdown();return 4;}
      result=applications->IdentifyApplication(static_cast<std::uint32_t>(pid),key);
      if(result!=vr::VRApplicationError_None){std::fprintf(stderr,"[vr-identity] identify error %d\n",result);shutdown();return 7;}
      std::printf("[vr-identity] Nightcap Roblox VR registered for process %lu\n",pid);
      auto* pacing=static_cast<vr::IVRSettings*>(get(vr::IVRSettings_Version,&error));
      const bool have_pacing=pacing && error==vr::VRInitError_None;
      if(have_pacing && nightcap::video::SeedAppResolutionDefault(*pacing,key,true) ==
          nightcap::video::ResolutionDefaultResult::kSeeded)
        std::puts("[vr-video] application default resolution set to 2160 x 2160");
      if(have_pacing && nightcap::video::SeedAppRefreshRateDefault(*pacing,key,true) ==
          nightcap::video::RefreshRateDefaultResult::kSeeded)
        std::puts("[vr-video] application default refresh rate set to 72 Hz");
      auto* compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
      if(have_pacing && compositor && error==vr::VRInitError_None &&
          nightcap::video::RestoreUncappedPacing(*pacing,key,static_cast<std::uint32_t>(pid),
              compositor->GetCurrentSceneFocusProcess()) == nightcap::video::PacingPolicyResult::kRestored)
        std::puts("[vr-pacing] automatic half-rate throttle disabled");
    } else std::puts("[vr-identity] Nightcap Roblox VR manifest registered");
    shutdown();dlclose(library);return 0;
  }
  auto* settings = static_cast<vr::IVRSettings*>(get(vr::IVRSettings_Version, &error));
  if (!settings || error != vr::VRInitError_None) { shutdown(); return 3; }
  vr::EVRSettingsError result = vr::VRSettingsError_None;
  for (int i=1; i<argc; ++i) {
    if (!std::strcmp(argv[i], "--enable")) settings->SetBool("steamvr", "sharpening", true, &result);
    else if (!std::strcmp(argv[i], "--disable")) settings->SetBool("steamvr", "sharpening", false, &result);
    else if (i+1<argc) {
      const char* key = nullptr;
      float minimum = 0.f, maximum = 1.f;
      if (!std::strcmp(argv[i], "--strength")) { key="edgeSharpness"; minimum=1.f; maximum=2.f; }
      else if (!std::strcmp(argv[i], "--amount")) key="sharpeningAlpha";
      else if (!std::strcmp(argv[i], "--fade")) key="fadeSharpness";
      if (!key) { shutdown(); return 4; }
      char* end = nullptr;
      const float value = std::strtof(argv[++i], &end);
      if (!end || *end || !(value >= minimum && value <= maximum)) { shutdown(); return 4; }
      settings->SetFloat("steamvr", key, value, &result);
    } else { shutdown(); return 4; }
    if (result != vr::VRSettingsError_None) { std::fprintf(stderr, "Settings error %d\n", result); shutdown(); return 5; }
  }
  const bool enabled = settings->GetBool("steamvr", "sharpening", &result);
  const float strength = settings->GetFloat("steamvr", "edgeSharpness", &result);
  const float amount = settings->GetFloat("steamvr", "sharpeningAlpha", &result);
  const float fade = settings->GetFloat("steamvr", "fadeSharpness", &result);
  if (result != vr::VRSettingsError_None) { shutdown(); return 5; }
  std::printf("{\"enabled\":%s,\"strength\":%.3f,\"amount\":%.3f,\"fade\":%.3f}\n",
              enabled ? "true" : "false", strength, amount, fade);
  shutdown();
  dlclose(library);
}
