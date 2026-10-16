#pragma once

#include <gtest/gtest.h>
#include <elf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "mocktail/vr/openxr_backend.h"
#include "mocktail/vr/projection_jitter.h"
#include "mocktail/vr/native_projection.h"
#include "vr/roblox_vr_device_bridge_internal.h"
#include "vr/roblox_projection_arm64_contract.h"
#include "vr/roblox_motion_arm64_contract.h"

namespace mocktail::vr {
namespace {

std::array<double, 2> ProjectTangentRay(const float (&extents)[4],
                                     double x, double y, double depth,
                                     double width, double height) {
  return {(x / depth + extents[2]) / (extents[2] + extents[3]) * width,
          (extents[0] - y / depth) / (extents[0] + extents[1]) * height};
}

TEST(ProjectionJitter, EveryRayMovesRequestedRasterPixelsForBothEyes) {
  for (const auto& eye : {std::array<float, 4>{1.f, 1.f, 1.f, 1.f},
                          std::array<float, 4>{1.2f, .7f, .8f, 1.1f}}) {
    for (const auto& requested : {std::array<float, 2>{.375f, -.25f},
                                  std::array<float, 2>{-.125f, .4f}}) {
      float original[4], shifted[4], jitter[2];
      std::copy(eye.begin(), eye.end(), original);
      std::copy(eye.begin(), eye.end(), shifted);
      std::copy(requested.begin(), requested.end(), jitter);
      ASSERT_TRUE(ApplyRasterProjectionJitter(shifted, true, jitter, 1440, 1280));
      EXPECT_NEAR(shifted[0] + shifted[1], original[0] + original[1], 1e-6);
      EXPECT_NEAR(shifted[2] + shifted[3], original[2] + original[3], 1e-6);
      for (double z : {1., 7., 100.}) {
        for (double x : {-.4, 0., .6}) {
          for (double y : {-.3, 0., .5}) {
            const auto before = ProjectTangentRay(original, x, y, z, 1440, 1280);
            const auto after = ProjectTangentRay(shifted, x, y, z, 1440, 1280);
            EXPECT_NEAR(after[0] - before[0], jitter[0], .0002);
            EXPECT_NEAR(after[1] - before[1], jitter[1], .0002);
          }
        }
      }
    }
  }
}

TEST(ProjectionJitter, OffAndInvalidRequestsLeaveExtentsUntouched) {
  const float original[4] = {1.2f, .7f, .8f, 1.1f};
  for (int invalid = 0; invalid < 8; ++invalid) {
    float tangents[4], jitter[2] = {.25f, -.25f};
    std::copy_n(original, 4, tangents);
    bool enabled = true;
    std::uint32_t width = 1440, height = 1280;
    if (invalid == 0) enabled = false;
    if (invalid == 1) width = 0;
    if (invalid == 2) height = 0;
    if (invalid == 3) jitter[0] = std::numeric_limits<float>::quiet_NaN();
    if (invalid == 4) jitter[1] = std::numeric_limits<float>::infinity();
    if (invalid == 5) jitter[0] = 1.01f;
    if (invalid == 6) tangents[0] = 0.f;
    if (invalid == 7) tangents[3] = std::numeric_limits<float>::infinity();
    float before[4];
    std::copy_n(tangents, 4, before);
    EXPECT_FALSE(ApplyRasterProjectionJitter(tangents, enabled, jitter, width, height));
    EXPECT_EQ(std::memcmp(tangents, before, sizeof(tangents)), 0);
  }
  float degenerate[4] = {1.f, 1.f, 1.f, 1.f};
  const float jitter[2] = {1.f, 0.f};
  EXPECT_FALSE(ApplyRasterProjectionJitter(degenerate, true, jitter, 1, 1));
  EXPECT_FLOAT_EQ(degenerate[2], 1.f);
  EXPECT_FLOAT_EQ(degenerate[3], 1.f);
}

ScriptedPoseSample ProjectionJitterPose() {
  ScriptedPoseSample pose;
  pose.valid = true;
  pose.position[0] = .4f;
  pose.position[1] = 1.6f;
  pose.orientation[1] = std::sin(.2f);
  pose.orientation[3] = std::cos(.2f);
  for (int eye = 0; eye < 2; ++eye) {
    pose.eye_offset[eye][0] = eye ? .032f : -.032f;
    const float fov[] = {-.7f, .8f, .9f, -.6f};
    std::copy_n(fov, 4, pose.eye_fov[eye]);
  }
  return pose;
}

TEST(ProjectionJitterBridge, StateAndPhysicalTrackingStayUnjittered) {
  const auto physical = ProjectionJitterPose();
  auto jittered = physical;
  jittered.projection_jitter_pixels[0] = .375f;
  jittered.projection_jitter_pixels[1] = -.25f;
  jittered.projection_render_width = 1440;
  jittered.projection_render_height = 1280;
  jittered.projection_jitter_valid = true;
  const auto snapshot = jittered;
  std::array<std::uint8_t, internal::kStateCopySize> baseline{}, state{};
  ASSERT_TRUE(internal::ApplyXrPose(baseline.data(), physical));
  ASSERT_TRUE(internal::ApplyXrPose(state.data(), jittered));
  EXPECT_EQ(std::memcmp(snapshot.eye_fov, jittered.eye_fov,
                        sizeof(jittered.eye_fov)), 0);
  EXPECT_EQ(std::memcmp(snapshot.position, jittered.position,
                        sizeof(jittered.position)), 0);
  EXPECT_EQ(std::memcmp(snapshot.orientation, jittered.orientation,
                        sizeof(jittered.orientation)), 0);
  // The engine mirrors X after reading these tangents. Jittering this state
  // would displace the two eyes in opposite directions; only returned native
  // projection matrices may receive the jitter.
  EXPECT_EQ(state, baseline);
}

TEST(ProjectionJitterBridge, DisabledAndUninitializedJitterPreserveTracking) {
  const auto physical = ProjectionJitterPose();
  std::array<std::uint8_t, internal::kStateCopySize> baseline{}, state{};
  ASSERT_TRUE(internal::ApplyXrPose(baseline.data(), physical));
  for (int invalid = 0; invalid < 4; ++invalid) {
    auto pose = physical;
    pose.projection_jitter_pixels[0] = .25f;
    pose.projection_jitter_pixels[1] = -.25f;
    pose.projection_jitter_valid = invalid != 0;
    if (invalid != 1) pose.projection_render_width = 1440;
    if (invalid != 2) pose.projection_render_height = 1280;
    if (invalid == 3)
      pose.projection_jitter_pixels[0] = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(internal::ApplyXrPose(state.data(), pose));
    EXPECT_EQ(state, baseline);
  }
}

std::array<double, 2> ProjectNativeMatrix(const float (&matrix)[16],
                                        const std::array<double, 4>& point,
                                        double width, double height) {
  double clip[4] = {};
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      clip[row] += matrix[row * 4 + col] * point[col];
  return {(clip[0] / clip[3] + 1.) * width * .5,
          (1. - clip[1] / clip[3]) * height * .5};
}

TEST(ProjectionJitterMatrix, BothMirroredEyesMoveTheSameRasterPixels) {
  for (int eye = 0; eye < 2; ++eye) {
    float original[16] = {1.2f, 0.f, .15f, 0.f,
                          0.f, 1.4f, -.1f, 0.f,
                          0.f, 0.f, .00002f, .1f,
                          0.f, 0.f, -1.f, 0.f};
    if (eye) original[2] = -original[2];
    float shifted[16];
    std::copy_n(original, 16, shifted);
    const float jitter[2] = {.375f, -.25f};
    ASSERT_TRUE(ApplyRasterMatrixProjectionJitter(shifted, true, jitter, 1440, 1280));
    for (double z : {-1., -7., -100.}) {
      const std::array<double, 4> point = {.2, -.1, z, 1.};
      const auto before = ProjectNativeMatrix(original, point, 1440, 1280);
      const auto after = ProjectNativeMatrix(shifted, point, 1440, 1280);
      EXPECT_NEAR(after[0] - before[0], jitter[0], .0001);
      EXPECT_NEAR(after[1] - before[1], jitter[1], .0001);
    }
    shifted[2] = original[2];
    shifted[6] = original[6];
    EXPECT_EQ(std::memcmp(shifted, original, sizeof(shifted)), 0);
  }
}

TEST(ProjectionJitterMatrix, CombinedWorldViewProjectionKeepsRasterDisplacement) {
  // A world-space camera with rotation and translation: row3 is no longer
  // {0,0,-1,0}, so changing only M02/M12 would produce incorrect motion.
  const float original[16] = {1.04f, 0.f, .6f, -2.f,
                             -.1f, 1.3f, .17f, .7f,
                             -.00001f, 0.f, -.000017f, .1f,
                             -.5f, 0.f, -.8660254f, 3.f};
  float shifted[16];
  std::copy_n(original, 16, shifted);
  const float jitter[2] = {-.375f, .25f};
  ASSERT_TRUE(ApplyRasterViewProjectionJitter(shifted, true, jitter, 1440, 1280));
  for (const auto& point : {std::array<double, 4>{.2, -.1, -4., 1.},
                            std::array<double, 4>{3., 2., -40., 1.}}) {
    const auto before = ProjectNativeMatrix(original, point, 1440, 1280);
    const auto after = ProjectNativeMatrix(shifted, point, 1440, 1280);
    EXPECT_NEAR(after[0] - before[0], jitter[0], .0002);
    EXPECT_NEAR(after[1] - before[1], jitter[1], .0002);
  }
  EXPECT_EQ(std::memcmp(shifted + 8, original + 8, 8 * sizeof(float)), 0);
}

TEST(ProjectionDepth, DerivesBothNativeNearPlanesAndFarDistance) {
  for (float near : {.1f, .5f}) {
    constexpr float far = 5000.f;
    float projection[16] = {1.f, 0.f, 0.f, 0.f,
                            0.f, 1.f, 0.f, 0.f,
                            0.f, 0.f, near / (far - near), near * far / (far - near),
                            0.f, 0.f, -1.f, 0.f};
    float actual_near = 0.f, actual_far = 0.f;
    bool infinite = true;
    ASSERT_TRUE(DecodeReverseProjectionDepth(projection, &actual_near,
                                             &actual_far, &infinite));
    EXPECT_NEAR(actual_near, near, 1e-6f);
    EXPECT_NEAR(actual_far, far, .001f);
    EXPECT_FALSE(infinite);
    // Native projection maps the actual near plane to1 and far plane to0.
    EXPECT_NEAR(-projection[10] + projection[11] / near, 1.f, 1e-6f);
    EXPECT_NEAR(-projection[10] + projection[11] / far, 0.f, 1e-6f);
  }
}

TEST(ProjectionDepth, RejectsInvalidDepthAndRecognizesInfiniteFar) {
  float matrix[16] = {1.f, 0.f, 0.f, 0.f,
                     0.f, 1.f, 0.f, 0.f,
                     0.f, 0.f, 0.f, .1f,
                     0.f, 0.f, -1.f, 0.f};
  float near = 0.f, far = 0.f;
  bool infinite = false;
  ASSERT_TRUE(DecodeReverseProjectionDepth(matrix, &near, &far, &infinite));
  EXPECT_FLOAT_EQ(near, .1f);
  EXPECT_TRUE(std::isinf(far));
  EXPECT_TRUE(infinite);
  matrix[10] = -1.f;
  EXPECT_FALSE(DecodeReverseProjectionDepth(matrix, &near, &far, &infinite));
  matrix[10] = 0.f;
  matrix[11] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(DecodeReverseProjectionDepth(matrix, &near, &far, &infinite));
}

TEST(NativeProjectionContract, RejectsEveryChangedByteAndUnsupportedBuild) {
  const std::string_view build = "25aa7fd31008a744c3c99f717e7d16d81d371d12";
  auto exercise = [&](const auto& contract, const auto& check) {
    ASSERT_TRUE(check(contract.data(), contract.size(), build));
    EXPECT_FALSE(check(nullptr, contract.size(), build));
    EXPECT_FALSE(check(contract.data(), contract.size() - 1, build));
    EXPECT_FALSE(check(contract.data(), contract.size(), "unknown"));
    for (std::size_t i = 0; i < contract.size(); ++i) {
      auto changed = contract;
      changed[i] ^= 1;
      EXPECT_FALSE(check(changed.data(), changed.size(), build)) << i;
    }
  };
  exercise(internal::kNativeProjectionGetter3222,
           internal::HasExpectedNativeProjectionContract);
  exercise(internal::kNativeViewProjectionGetter3222,
           internal::HasExpectedNativeViewProjectionContract);
}

TEST(NativeMotionConstructorContract, RejectsAnyChangedInstructionOrWrongBuild) {
  const std::string_view build="25aa7fd31008a744c3c99f717e7d16d81d371d12";
  const auto& code=internal::kNativeMotionConstructor3222;
  const auto check=internal::HasExpectedNativeMotionConstructorContract;
  ASSERT_TRUE(check(code.data(),code.size(),build));
  EXPECT_FALSE(check(nullptr,code.size(),build));
  EXPECT_FALSE(check(code.data(),code.size()-1,build));
  EXPECT_FALSE(check(code.data(),code.size(),"unsupported"));
  for(std::size_t i=0;i<code.size();++i) {
    auto changed=code;changed[i]^=1;
    EXPECT_FALSE(check(changed.data(),changed.size(),build))<<i;
  }
  std::uint32_t original=0;
  std::memcpy(&original,code.data()+internal::kNativeMotionModeInstructionOffset,
              sizeof(original));
  EXPECT_EQ(original,internal::kNativeMotionWorldModeInstruction);
  // The replacement changes only the immediate from one to zero; the
  // destination register and the matching store remain part of the contract.
  EXPECT_EQ(original^internal::kNativeMotionClipModeInstruction,0x20u);
  EXPECT_EQ(internal::kNativeMotionModeInstructionOffset%alignof(std::uint32_t),0u);
}

TEST(NativeMotionSetupContract, RejectsAnyChangedInstructionOrWrongBuild) {
  const std::string_view build = "25aa7fd31008a744c3c99f717e7d16d81d371d12";
  const auto& code = internal::kNativeMotionSetup3222;
  const auto check = internal::HasExpectedNativeMotionSetupContract;
  ASSERT_TRUE(check(code.data(), code.size(), build));
  EXPECT_FALSE(check(nullptr, code.size(), build));
  EXPECT_FALSE(check(code.data(), code.size() - 1, build));
  EXPECT_FALSE(check(code.data(), code.size(), "unsupported"));
  for (std::size_t i = 0; i < code.size(); ++i) {
    auto changed = code;
    changed[i] ^= 1;
    EXPECT_FALSE(check(changed.data(), changed.size(), build)) << i;
  }
  std::uint32_t original = 0;
  std::memcpy(&original, code.data() + internal::kNativeMotionSetupInstructionOffset,
              sizeof(original));
  EXPECT_EQ(original, internal::kNativeMotionWorldSetupInstruction);
  // MOV W1,1 becomes MOV W1,0 at this sole manager setup callsite. The generic
  // setter target, manager load and neighboring callsites remain unchanged.
  EXPECT_EQ(original ^ internal::kNativeMotionClipSetupInstruction, 0x20u);
  EXPECT_EQ(internal::kNativeMotionSetupInstructionOffset % alignof(std::uint32_t), 0u);
}

class NativeMotionIntProperty : public testing::Test {
 protected:
  void SetUp() override {
    auto* header = reinterpret_cast<Elf64_Ehdr*>(image.data());
    std::memcpy(header->e_ident, ELFMAG, SELFMAG);
    header->e_ident[EI_CLASS] = ELFCLASS64;
    header->e_ident[EI_DATA] = ELFDATA2LSB;
#if defined(__aarch64__)
    header->e_machine = EM_AARCH64;
#else
    header->e_machine = EM_X86_64;
#endif
    header->e_phoff = sizeof(Elf64_Ehdr);
    header->e_phnum = 2;
    auto* segments = reinterpret_cast<Elf64_Phdr*>(
        image.data() + sizeof(Elf64_Ehdr));
    segments[0] = Elf64_Phdr{PT_LOAD, PF_R, 0, 0, 0, 0x1000, 0x1000, 0x1000};
    segments[1] = Elf64_Phdr{PT_LOAD, PF_R | PF_W, 0, 0x1000, 0,
                              0x1000, 0x1000, 0x1000};
    std::memcpy(image.data() + 0x800, "MotionBuffers3", 15);
    name = base() + 0x800;
    std::memcpy(image.data() + 0x1008, &name, sizeof(name));
  }
  std::uintptr_t base() const {
    return reinterpret_cast<std::uintptr_t>(image.data());
  }
  alignas(64) std::array<std::uint8_t, 0x2000> image{};
  std::uintptr_t name = 0;
};

TEST_F(NativeMotionIntProperty, WritesAllFourBytesWithoutTouchingNameOrPadding) {
  std::fill_n(image.data() + 0x1000, 8, 0xa5);
  const auto original = image;
  ASSERT_TRUE(internal::SetNamedFastInt32(base(), 0x1000, "MotionBuffers3", 0x800, 100));
  std::int32_t value = 0;
  std::memcpy(&value, image.data() + 0x1000, sizeof(value));
  EXPECT_EQ(value, 100);
  EXPECT_EQ(std::memcmp(image.data() + 0x1004, original.data() + 0x1004,
                        image.size() - 0x1004), 0);
}

TEST_F(NativeMotionIntProperty, RejectsWrongNameAddressAndReadonlyProperty) {
  const auto original = image;
  EXPECT_FALSE(internal::SetNamedFastInt32(base(), 0x1000, "MotionBuffers3", 0x810, 100));
  EXPECT_FALSE(internal::SetNamedFastInt32(base(), 0x1000, "MotionBuffers", 0x800, 100));
  EXPECT_FALSE(internal::SetNamedFastInt32(base(), 0x800, "MotionBuffers3", 0x800, 100));
  EXPECT_FALSE(internal::SetNamedFastInt32(base(), 0x1ff8, "MotionBuffers3", 0x800, 100));
  EXPECT_EQ(image, original);
}

}  // namespace
}  // namespace mocktail::vr
