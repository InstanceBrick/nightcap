#ifndef MOCKTAIL_VR_ROBLOX_VR_GUEST_ABI_H_
#define MOCKTAIL_VR_ROBLOX_VR_GUEST_ABI_H_
#include <cstdint>
#include <cstring>
namespace mocktail::vr::guest_abi {
// AAPCS64 uses x0 for self and x8 for an indirect aggregate result. Declaring
// the actual aggregate return lets the compiler preserve the guest ABI.
struct StateResult { std::uint8_t bytes[0x138]; };
struct FrameResult { float elements[12]; };
static_assert(sizeof(StateResult) == 0x138);
static_assert(sizeof(FrameResult) == 0x30);
#if defined(__aarch64__)
using StateGetterFn = StateResult (*)(void* self);
using PointerFrameFn = FrameResult (*)(void* self);
inline void CallState(StateGetterFn fn, void* result, void* self) {
  const auto value = fn(self);
  std::memcpy(result, &value, sizeof(value));
}
inline void CallPointer(PointerFrameFn fn, void* result, void* self) {
  const auto value = fn(self);
  std::memcpy(result, &value, sizeof(value));
}
#else
using StateGetterFn = void* (*)(void* result, void* self);
using PointerFrameFn = void* (*)(void* result, void* self);
inline void CallState(StateGetterFn fn, void* result, void* self) {
  fn(result, self);
}
inline void CallPointer(PointerFrameFn fn, void* result, void* self) {
  fn(result, self);
}
#endif
} // namespace mocktail::vr::guest_abi
#endif
