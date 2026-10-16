# FidelityFX Super Resolution 3.1.4

This directory contains the MIT-licensed upscaler, Optical Flow component and Vulkan backend from
AMD FidelityFX SDK 1.1.4, commit
`c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`.

The standalone CMake target `nightcap_fsr3` builds on Linux ARM64. It compiles
AMD's GLSL compute passes with `glslangValidator` and derives descriptor names,
types and bindings from SPIR-V. `spirv-val` validates generated code when
available. Shader binaries remain build artifacts.

Supported variants use FP32 or opt-in FP16, render-resolution motion vectors,
normal or inverted depth, LDR or HDR color, optional sharpening and the SDK's
Lanczos choices. Jittered and unjittered motion-vector variants are generated.
Camera jitter and matching scene depth and motion vectors
are required by the caller. Separate upscaler contexts retain each eye's
temporal history. OpenXR retains presentation ownership.

When engine motion vectors are unavailable, the wrapper can run AMD Optical
Flow independently for each eye. Its signed 8 × 8 block vectors are reconstructed
as a render-resolution RG16F motion field. The conversion removes camera jitter
and supplies a GPU scene-cut reactive mask. This image-motion fallback adds
compute work and has less information about occlusions and object surfaces than
native engine vectors.

The AMD license is retained. Linux integration includes these changes:

- Opaque context storage accommodates Linux's 32-bit `wchar_t` binding tables.
- Checked string helpers provide the bounds checks expected by the host code.
- Vulkan memory selection accepts unified-memory device-local heaps, while
  retaining a preference for invisible VRAM on discrete GPUs.
- The promoted Vulkan 1.1 memory-requirements function is used when its KHR
  alias is absent.
- The GLSL luma-history storage declaration matches the runtime's RGBA16F image.
- Physical-device queries use a private host Vulkan loader. Production code
  keeps that loader local so it does not collide with Android Vulkan symbols.

`compile_fsr3_shaders.py`, `compile_opticalflow_shaders.py`,
`fsr3_vulkan_support.cpp` and this CMake target provide Linux build integration.
OpenXR owns presentation; this target does not replace its swapchain.

The gaze transport crops a fixed-size focus region with a guard band, runs the
genuine temporal upscaler on that region, and blends it with a cheap spatial
reconstruction of the periphery. Native motion is converted to pixel units with
the changing crop origin included. The compositor maps global pixel coordinates
to the crop directly, retaining fractional output placement when the input crop
moves. Large gaze jumps reset the region's temporal history.

FP16 requires `shaderFloat16` and `shaderInt16` to have been enabled on the
logical Vulkan device. The generated shaders do not require 16-bit storage-buffer
or push-constant features. The SDK also requires storage-image extended formats
and storage-image writes without a declared format.

## GPU checks

Configure this directory with `FSR3_BUILD_VULKAN_SMOKE=ON`,
`FSR3_BUILD_TRANSPORT_SMOKE=ON` and `FSR3_BUILD_GAZE_SMOKE=ON` to build the
native Vulkan checks:

- `fsr3_vulkan_smoke` dispatches the genuine upscaler with independent stereo
  histories and checks finite FP16 output.
- `fsr3_vulkan_smoke opticalflow` verifies known image translation. A texture
  moving right by two input pixels produces backward vectors of `(-2, 0)`.
- `fsr3_transport_smoke` checks dark, mid and bright encoded colors through
  native RGBA/BGRA UNORM → sRGB sample → linear FSR → sRGB blit → raw UNORM copy.
  It checks the opaque alpha used by the VR eye images.
- `fsr3_transport_smoke 1440 2160` measures GPU timestamps around one eye's
  transport and upscaling. Add `opticalflow` to include motion estimation. These
  synthetic timings describe the upscaler's cost rather than application FPS.
- `fsr3_gaze_transport_smoke` checks color transport, native motion conversion,
  changing crop origins and history resets for both eyes and channel orders.
  Its optional resolution arguments measure the gaze-region GPU cost.

Run these checks with the Khronos validation layer and synchronization
validation enabled when changing resources, shaders or barriers.
