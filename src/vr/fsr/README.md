# FidelityFX Super Resolution 1

`ffx_a.h` and `ffx_fsr1.h` are the unmodified AMD reference headers from
[GPUOpen-Effects/FidelityFX-FSR](https://github.com/GPUOpen-Effects/FidelityFX-FSR),
revision `a21ffb8f6c13233ba336352bdff293894c706575`.

The Vulkan integration uses FP32 EASU for spatial upscaling and gaze-limited
RCAS for sharpening after anti-aliasing. The upstream MIT license is preserved
in `LICENSE.txt` and in both headers.
