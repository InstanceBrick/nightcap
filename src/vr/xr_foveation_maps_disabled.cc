#include "mocktail/vr/xr_foveation_maps.h"
namespace mocktail::vr {
struct XrFoveationMaps::Impl {};
XrFoveationMaps::XrFoveationMaps():impl_(std::make_unique<Impl>()){}
XrFoveationMaps::~XrFoveationMaps()=default;
Status XrFoveationMaps::Initialize(void*,std::uint64_t,void*,VkDevice,PFN_vkGetDeviceProcAddr,std::uint32_t,std::uint32_t,VkFormat){return Status::Error(StatusCode::kUnavailable,"OpenXR disabled");}
bool XrFoveationMaps::BeginFrame(){return false;}
void XrFoveationMaps::EndFrame(){}
void XrFoveationMaps::Destroy(){}
bool XrFoveationMaps::ready() const{return false;}
bool XrFoveationMaps::acquired() const{return false;}
bool XrFoveationMaps::tracking() const{return false;}
VkImageView XrFoveationMaps::view(int) const{return VK_NULL_HANDLE;}
std::uint32_t XrFoveationMaps::width() const{return 0;}
std::uint32_t XrFoveationMaps::height() const{return 0;}
bool XrFoveationMaps::center(int,float*,float*) const{return false;}
}
