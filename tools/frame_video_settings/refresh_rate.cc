// Query the active headset rate as a background OpenVR client. No scene focus.
#include "openvr.h"
#include <dlfcn.h>
#include <cstdio>
#include <cmath>
#include <cstdint>
int main(){
  void* library=dlopen("/opt/steamvr/bin/linuxarm64/libopenvr_api.so",RTLD_NOW|RTLD_LOCAL);
  if(!library)return 2;
  auto init=reinterpret_cast<std::uint32_t(*)(vr::EVRInitError*,vr::EVRApplicationType,const char*)>(dlsym(library,"VR_InitInternal2"));
  auto get=reinterpret_cast<void*(*)(const char*,vr::EVRInitError*)>(dlsym(library,"VR_GetGenericInterface"));
  auto shutdown=reinterpret_cast<void(*)()>(dlsym(library,"VR_ShutdownInternal"));
  if(!init||!get||!shutdown)return 2;
  vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Background,nullptr);
  if(error!=vr::VRInitError_None)return 3;
  auto* system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
  if(!system||error!=vr::VRInitError_None){shutdown();return 4;}
  vr::ETrackedPropertyError property=vr::TrackedProp_Success;
  const float rate=system->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,vr::Prop_DisplayFrequency_Float,&property);
  shutdown();dlclose(library);
  if(property!=vr::TrackedProp_Success||!std::isfinite(rate)||rate<30.f||rate>240.f)return 5;
  std::printf("%d\n",int(std::lround(rate)));return 0;
}
