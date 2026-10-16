// Native ABI fixture: assembly deliberately consumes x0=self and x8=result.
// This catches accidental use of the x86 two-argument sret calling convention.
#include "mocktail/vr/roblox_vr_guest_abi.h"
#include <array>
#include <cstdio>
#include <cstring>

using namespace mocktail::vr;
extern "C" guest_abi::StateResult fixture_state(void*);
extern "C" guest_abi::FrameResult fixture_frame(void*);
asm(R"(
.text
.p2align 4
.global fixture_state
.type fixture_state,%function
fixture_state:
  mov x9, #0x138
  b fixture_copy
.global fixture_frame
.type fixture_frame,%function
fixture_frame:
  mov x9, #0x30
fixture_copy:
  ldrb w10, [x0], #1
  strb w10, [x8], #1
  subs x9, x9, #1
  b.ne fixture_copy
  ret
)");

template <std::size_t N> struct Guarded {
  std::array<unsigned char, 16> before;
  std::array<unsigned char, N> result;
  std::array<unsigned char, 16> after;
  Guarded() { before.fill(0xa5); result.fill(0); after.fill(0x5a); }
  bool valid() const {
    for (auto x : before) if (x != 0xa5) return false;
    for (auto x : after) if (x != 0x5a) return false;
    return true;
  }
};

int main() {
  std::array<unsigned char, 0x138> source;
  for (std::size_t i=0; i<source.size(); ++i) source[i] = (i * 37 + 11) & 255;
  Guarded<0x138> state;
  Guarded<0x30> frame;
  guest_abi::CallState(&fixture_state, state.result.data(), source.data());
  guest_abi::CallPointer(&fixture_frame, frame.result.data(), source.data());
  if (!state.valid() || !frame.valid() ||
      std::memcmp(state.result.data(), source.data(), state.result.size()) ||
      std::memcmp(frame.result.data(), source.data(), frame.result.size())) {
    std::fputs("ARM64 ABI fixture failed\n", stderr);
    return 1;
  }
  std::puts("ARM64 x0/x8 state and frame ABI fixtures passed; guards intact");
}
