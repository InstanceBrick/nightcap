#pragma once
#include "vr/roblox_vr_arm64_contract.h"
#include "vr/roblox_vr_arm64_contract_3222.h"
#include <string_view>
#include <initializer_list>
namespace mocktail::vr::internal::arm64 {
struct BuildContract {
  std::uintptr_t kConstructorRva;
  const std::array<std::uint8_t,656>& kConstructor;
  std::uintptr_t kStateGetterRva;
  const std::array<std::uint8_t,16>& kStateGetter;
  std::uintptr_t kEyeGetterRva;
  const std::array<std::uint8_t,12>& kEyeGetter;
  std::uintptr_t kInitializerRva;
  const std::array<std::uint8_t,588>& kInitializer;
  std::uintptr_t kPointerGetterRva;
  const std::array<std::uint8_t,264>& kPointerGetter;
  std::uintptr_t kWorkspaceGetterRva;
  const std::array<std::uint8_t,32>& kWorkspaceGetter;
  std::uintptr_t kFramebufferHelperRva;
  const std::array<std::uint8_t,312>& kFramebufferHelper;
  std::uintptr_t kHapticSinkRva;
  const std::array<std::uint8_t,4>& kHapticSink;
  std::uintptr_t kIsVrAppGetterRva;
  const std::array<std::uint8_t,8>& kIsVrAppGetter;
  std::uintptr_t kIsVrAppRegistrationRva;
  const std::array<std::uint8_t,36>& kIsVrAppRegistration;
  std::uintptr_t kIsVrAppDescriptorRva;
  std::uintptr_t kIsVrAppNameRva;
  std::uintptr_t vtable, state_memcpy, camera_slot, hand_selector, head_scale, descriptor_slot, framebuffer_slot;
};
inline const BuildContract& Build2998() {
  namespace N = mocktail::vr::internal::arm64;
  static const BuildContract c = {N::kConstructorRva, N::kConstructor, N::kStateGetterRva, N::kStateGetter, N::kEyeGetterRva, N::kEyeGetter, N::kInitializerRva, N::kInitializer, N::kPointerGetterRva, N::kPointerGetter, N::kWorkspaceGetterRva, N::kWorkspaceGetter, N::kFramebufferHelperRva, N::kFramebufferHelper, N::kHapticSinkRva, N::kHapticSink, N::kIsVrAppGetterRva, N::kIsVrAppGetter, N::kIsVrAppRegistrationRva, N::kIsVrAppRegistration, N::kIsVrAppDescriptorRva, N::kIsVrAppNameRva, 0x635ab08, 0x6251e70, 0x3a8, 0x118, 0x140, 0x80, 0x178};
  return c;
}
inline const BuildContract& Build3222() {
  namespace N = mocktail::vr::internal::arm64_v3222;
  static const BuildContract c = {N::kConstructorRva, N::kConstructor, N::kStateGetterRva, N::kStateGetter, N::kEyeGetterRva, N::kEyeGetter, N::kInitializerRva, N::kInitializer, N::kPointerGetterRva, N::kPointerGetter, N::kWorkspaceGetterRva, N::kWorkspaceGetter, N::kFramebufferHelperRva, N::kFramebufferHelper, N::kHapticSinkRva, N::kHapticSink, N::kIsVrAppGetterRva, N::kIsVrAppGetter, N::kIsVrAppRegistrationRva, N::kIsVrAppRegistration, N::kIsVrAppDescriptorRva, N::kIsVrAppNameRva, 0x67b6688, 0x64da5e0, 0x3a0, 0x108, 0x130, 0x88, 0x188};
  return c;
}
inline const BuildContract* ForBuildId(std::string_view id) {
  if(id=="ed2b035768b1513561d0e6ea7f9bbec35b0e60e5")return &Build2998();
  if(id=="25aa7fd31008a744c3c99f717e7d16d81d371d12")return &Build3222();
  return nullptr;
}
inline const BuildContract* ForConstructor(std::uintptr_t rva) {
  for(const auto* c : {&Build2998(),&Build3222()})if(c->kConstructorRva==rva)return c;
  return nullptr;
}
inline const BuildContract* ForInitializer(std::uintptr_t rva) {
  for(const auto* c : {&Build2998(),&Build3222()})if(c->kInitializerRva==rva)return c;
  return nullptr;
}
}
