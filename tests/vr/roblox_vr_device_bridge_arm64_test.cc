#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include "mocktail/vr/roblox_vr_device_bridge.h"
#include "vr/roblox_vr_device_bridge_internal.h"
#include "vr/roblox_vr_arm64_contract.h"
#include "vr/roblox_vr_arm64_build_contracts.h"
#include "projection_jitter_tests.h"

namespace mocktail::vr {
namespace {
namespace arm = internal::arm64;
const internal::VrBuildContract& Contract() {
  return *internal::FindVrBuildContract("ed2b035768b1513561d0e6ea7f9bbec35b0e60e5");
}
compat::BuildProfile Profile() {
  compat::BuildProfile p{};
  p.elf_build_id = std::string(Contract().build_id);
  p.allow_host_abi_bridges = true;
  p.vr_debug_device_bridge = Contract().device;
  return p;
}

TEST(ArmVrContracts, RejectsEveryChangedInstructionByteAndTruncatedFunction) {
  const auto& c = Contract();
  auto exercise = [](const auto& bytes, const auto& check) {
    ASSERT_TRUE(check(bytes.data(), bytes.size()));
    EXPECT_FALSE(check(static_cast<const std::uint8_t*>(nullptr), bytes.size()));
    EXPECT_FALSE(check(bytes.data(), bytes.size()-1));
    for (std::size_t i=0; i<bytes.size(); ++i) {
      auto changed = bytes;
      changed[i] ^= 1;
      ASSERT_FALSE(check(changed.data(), changed.size())) << "byte " << i;
    }
  };
  exercise(arm::kStateGetter, [&](auto* p, auto n) { return internal::HasExpectedVrStateGetterContract(p,n,c); });
  exercise(arm::kEyeGetter, internal::HasExpectedVrEyeGetterContract);
  exercise(arm::kConstructor, [&](auto* p, auto n) { return internal::HasExpectedVrConstructorContract(p,n,c.device); });
  exercise(arm::kInitializer, [&](auto* p, auto n) { return internal::HasExpectedVrEyeInitializerContract(p,n,c.device); });
  exercise(arm::kPointerGetter, [&](auto* p, auto n) { return internal::HasExpectedVrPointerContract(p,n,c); });
}

TEST(ArmVrContracts, RejectsX86BuildAndWrongFrameBufferSlot) {
  EXPECT_EQ(internal::FindVrBuildContract("ade08266c67aee88ec9c1d00902150e1684dad3a"), nullptr);
  auto c = Contract();
  c.device.device_create_framebuffer_vtable_offset = 0x180;
  EXPECT_FALSE(internal::HasExpectedVrEyeInitializerContract(arm::kInitializer.data(),arm::kInitializer.size(),c.device));
  c = Contract(); c.camera_head_scale_offset += 16;
  EXPECT_FALSE(internal::HasExpectedVrPointerContract(arm::kPointerGetter.data(),arm::kPointerGetter.size(),c));
  c = Contract(); c.state_memcpy_rva += 4;
  EXPECT_FALSE(internal::HasExpectedVrStateGetterContract(arm::kStateGetter.data(),arm::kStateGetter.size(),c));
}

TEST(ArmVrContracts, LatestBuildRejectsMutatedCodeAndOldObjectFields) {
  const auto* c = internal::FindVrBuildContract("25aa7fd31008a744c3c99f717e7d16d81d371d12");
  ASSERT_NE(c, nullptr);
  const auto& code = arm::Build3222();
  auto exercise = [](const auto& bytes, const auto& check) {
    ASSERT_TRUE(check(bytes.data(), bytes.size()));
    EXPECT_FALSE(check(static_cast<const std::uint8_t*>(nullptr), bytes.size()));
    EXPECT_FALSE(check(bytes.data(), bytes.size()-1));
    for (std::size_t i=0; i<bytes.size(); ++i) {
      auto changed = bytes;
      changed[i] ^= 1;
      ASSERT_FALSE(check(changed.data(), changed.size())) << "byte " << i;
    }
  };
  exercise(code.kConstructor, [&](auto* p, auto n) { return internal::HasExpectedVrConstructorContract(p,n,c->device); });
  exercise(code.kInitializer, [&](auto* p, auto n) { return internal::HasExpectedVrEyeInitializerContract(p,n,c->device); });
  exercise(code.kStateGetter, [&](auto* p, auto n) { return internal::HasExpectedVrStateGetterContract(p,n,*c); });
  exercise(code.kPointerGetter, [&](auto* p, auto n) { return internal::HasExpectedVrPointerContract(p,n,*c); });
  for (auto field : {&internal::VrBuildContract::camera_vtable_offset,
                    &internal::VrBuildContract::pointer_hand_selector_offset,
                    &internal::VrBuildContract::camera_head_scale_offset}) {
    auto wrong = *c;
    wrong.*field = Contract().*field;
    EXPECT_FALSE(internal::HasExpectedVrPointerContract(code.kPointerGetter.data(),code.kPointerGetter.size(),wrong));
  }
  EXPECT_FALSE(internal::HasExpectedVrStateGetterContract(arm::kStateGetter.data(),arm::kStateGetter.size(),*c));
  auto wrong_device=c->device;
  wrong_device.device_create_framebuffer_vtable_offset=0x178;
  EXPECT_FALSE(internal::HasExpectedVrEyeInitializerContract(code.kInitializer.data(),code.kInitializer.size(),wrong_device));
  // Independent machine operand in the verified helper: ldr x9,[x8,#slot].
  std::uint32_t dispatch_instruction=0;
  std::memcpy(&dispatch_instruction,code.kFramebufferHelper.data()+0xcc,4);
  EXPECT_EQ(((dispatch_instruction>>10)&0xfffu)*8,c->device.device_create_framebuffer_vtable_offset);
  EXPECT_EQ(code.descriptor_slot,0x88u);
  EXPECT_EQ(arm::Build2998().descriptor_slot,0x80u);
  EXPECT_EQ(arm::ForBuildId("unknown"),nullptr);
}

TEST(ArmVrLifecycle, RejectsMissingMetadataPermissionAndUnknownBuild) {
  RobloxVrDeviceBridge bridge;
  auto p = Profile(); p.vr_debug_device_bridge.reset();
  EXPECT_FALSE(bridge.Install(p).ok());
  p = Profile(); p.allow_host_abi_bridges = false;
  EXPECT_FALSE(bridge.Install(p).ok());
  p = Profile(); p.elf_build_id = "unknown";
  EXPECT_FALSE(bridge.Install(p).ok());
  p = Profile(); ++p.vr_debug_device_bridge->state_getter_rva;
  EXPECT_FALSE(bridge.Install(p).ok());
}

TEST(ArmVrLifecycle, SingleOwnerAndFailClosedActivation) {
  RobloxVrDeviceBridge first, second;
  EXPECT_TRUE(first.Install(Profile()).ok());
  EXPECT_FALSE(second.Install(Profile()).ok());
  EXPECT_FALSE(first.Activate(0).ok());
  EXPECT_FALSE(first.active());
  first.Shutdown();
  EXPECT_TRUE(second.Install(Profile()).ok());
  second.Shutdown();
}

TEST(ArmVrResources, RequiresBothDistinctEyesAndReadiness) {
  std::array<unsigned char,0x228> object{};
  auto put = [&](std::size_t off, auto value) { std::memcpy(object.data()+off,&value,sizeof(value)); };
  put(internal::kWidthOffset,std::uint32_t{1024});
  put(internal::kHeightOffset,std::uint32_t{1024});
  EXPECT_EQ(internal::InspectEyeResources(object.data()),internal::EyeResourceState::kEmpty);
  object[internal::kReadyByteOffset] = 1;
  EXPECT_EQ(internal::InspectEyeResources(object.data()),internal::EyeResourceState::kInvalid);
  put(internal::kFramebufferSlotOffset,std::uintptr_t{0x100});
  put(internal::kFramebufferSlotOffset+16,std::uintptr_t{0x200});
  put(internal::kTextureSlotOffset,std::uintptr_t{0x300});
  put(internal::kTextureSlotOffset+16,std::uintptr_t{0x400});
  EXPECT_EQ(internal::InspectEyeResources(object.data()),internal::EyeResourceState::kReady);
  put(internal::kFramebufferSlotOffset+16,std::uintptr_t{0x100});
  EXPECT_EQ(internal::InspectEyeResources(object.data()),internal::EyeResourceState::kInvalid);
}
} // namespace
} // namespace mocktail::vr
