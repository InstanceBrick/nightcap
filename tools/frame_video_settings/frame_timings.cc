#include "openvr.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

int main(int argc,char** argv) {
  void* lib=dlopen("/opt/steamvr/bin/linuxarm64/libopenvr_api.so",RTLD_NOW|RTLD_LOCAL);
  if(!lib)return 2;
  auto init=reinterpret_cast<std::uint32_t(*)(vr::EVRInitError*,vr::EVRApplicationType,const char*)>(dlsym(lib,"VR_InitInternal2"));
  auto get=reinterpret_cast<void*(*)(const char*,vr::EVRInitError*)>(dlsym(lib,"VR_GetGenericInterface"));
  auto shutdown=reinterpret_cast<void(*)()>(dlsym(lib,"VR_ShutdownInternal"));
  if(!init||!get||!shutdown)return 2;
  vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Background,nullptr);
  if(error!=vr::VRInitError_None)return 3;
  if(argc==2 && std::strcmp(argv[1],"--remove-ui-probe")==0) {
    auto* settings=static_cast<vr::IVRSettings*>(get(vr::IVRSettings_Version,&error));
    if(!settings || error!=vr::VRInitError_None){shutdown();return 4;}
    vr::EVRSettingsError result=vr::VRSettingsError_None;
    settings->RemoveKeyInSection("nightcap","uiHitboxDebug",&result);
    shutdown();dlclose(lib);return result==vr::VRSettingsError_None?0:6;
  }
  auto* compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
  if(!compositor||error!=vr::VRInitError_None){shutdown();return 4;}
  auto* system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
  auto* overlay=static_cast<vr::IVROverlay*>(get(vr::IVROverlay_Version,&error));
  std::printf("scene_pid=%u dashboard_visible=%d activity=%d\n",
      compositor->GetCurrentSceneFocusProcess(),overlay?int(overlay->IsDashboardVisible()):-1,
      system?int(system->GetTrackedDeviceActivityLevel(vr::k_unTrackedDeviceIndex_Hmd)):-1);
  std::vector<vr::Compositor_FrameTiming> frames(120);
  for(auto& frame:frames)frame.m_nSize=sizeof(frame);
  const auto count=compositor->GetFrameTimings(frames.data(),frames.size());
  if(!count){shutdown();return 5;}
  const auto report=[&](const char* name,auto member){std::vector<float> data;double sum=0;for(unsigned i=0;i<count;++i){float v=frames[i].*member;if(std::isfinite(v)&&v>=0){data.push_back(v);sum+=v;}}if(data.empty())return;std::sort(data.begin(),data.end());std::printf("%s avg=%.2f p50=%.2f p95=%.2f max=%.2f ms\n",name,sum/data.size(),data[data.size()/2],data[(data.size()-1)*95/100],data.back());};
  std::printf("Frames=%u first=%u last=%u\n",count,frames[0].m_nFrameIndex,frames[count-1].m_nFrameIndex);
  report("app_gpu",&vr::Compositor_FrameTiming::m_flTotalRenderGpuMs);
  report("pre_submit_gpu",&vr::Compositor_FrameTiming::m_flPreSubmitGpuMs);
  report("post_submit_gpu",&vr::Compositor_FrameTiming::m_flPostSubmitGpuMs);
  report("client_interval",&vr::Compositor_FrameTiming::m_flClientFrameIntervalMs);
  report("present_cpu",&vr::Compositor_FrameTiming::m_flPresentCallCpuMs);
  report("compositor_gpu",&vr::Compositor_FrameTiming::m_flCompositorRenderGpuMs);
  unsigned dropped=0,reprojected=0;for(unsigned i=0;i<count;++i){dropped+=frames[i].m_nNumDroppedFrames;reprojected+=frames[i].m_nNumFramePresents>1;}
  std::printf("dropped=%u multi_present_frames=%u\n",dropped,reprojected);
  unsigned throttled=0,motion=0,max_throttle=0,max_prediction=0;
  for(unsigned i=0;i<count;++i) {
    const auto throttle=VR_COMPOSITOR_NUMBER_OF_THROTTLED_FRAMES(frames[i]);
    const auto prediction=VR_COMPOSITOR_ADDITIONAL_PREDICTED_FRAMES(frames[i]);
    throttled+=throttle!=0;motion+=(frames[i].m_nReprojectionFlags&vr::VRCompositor_ReprojectionMotion)!=0;
    max_throttle=std::max(max_throttle,throttle);max_prediction=std::max(max_prediction,prediction);
  }
  std::printf("runtime_throttled_frames=%u max_throttle=%u motion_smoothed_frames=%u max_additional_prediction=%u\n",throttled,max_throttle,motion,max_prediction);

  shutdown();dlclose(lib);
}
