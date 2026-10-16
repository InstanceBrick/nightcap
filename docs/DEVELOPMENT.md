# Building and source layout

The [installation guide](INSTALL.md) covers a complete source build on Steam
Frame. The container recipe uses Fedora 44 ARM64 with CMake, Ninja, SDL3,
OpenXR, Vulkan, WebKitGTK and Nightcap's other native dependencies.
FSR 3.1 shaders are generated during the build. Fedora's `glslang` package
provides `glslangValidator`, and `spirv-tools` provides `spirv-val` for shader
validation; both are installed by the Frame container recipe. An existing
development image needs these packages before rebuilding this branch.

## Upstream source

This branch includes Nightcap's `vr` additions through
[`adccf4e4841275bce85d0330013367e2c124bd10`](https://github.com/CoderDayton/nightcap/commit/adccf4e4841275bce85d0330013367e2c124bd10),
including all changes from its `main` branch at that revision. The update adds
emoji fallback fonts for native text fields, decoded/compressed emulated-texture
byte counters, menu-only native Luau compilation, and queued game switching.
Menu compilation is enabled only after validating the native flag on ARM64;
explicit client settings remain in control.

Frame already uses the same reference SMAA revision as this update. Its existing
gaze AA path is retained without adding a second full-eye filter. Frame's native
2160 × 2160 rendering, 72 Hz profile, simple Video controls, controllers, audio
and foveation integration remain the platform defaults.

## Build scripts

`build-frame-nightcap.sh` configures a Release build at `/work/build-frame-arm64`
and installs to `/opt/nightcap-vr-arm64`. VR is enabled and runtime diagnostics
are disabled. It also builds the Steam application identity helper.

For incremental development in an existing Frame image, with Nightcap closed:

```bash
podman run --arch arm64 --name nightcap-frame-development \
  --cpus 3 --memory 8g --security-opt label=disable \
  -v "$PWD:/work" --entrypoint /bin/bash \
  localhost/nightcap-vr-arm64:frame-foveated -lc \
  'bash /work/build-frame-nightcap.sh && bash /work/test-arm64-bridge.sh'
```

The test script runs focused ARM64 bridge, pose, controller, lifecycle, JNI,
audio and host ABI tests, including the native assembly return-value fixture.
It also builds the GPU test tools. Build and test commands share one CMake tree;
run them sequentially.

## Main components

| Path | Purpose |
| --- | --- |
| `src/vr/` | OpenXR lifecycle, gaze, stereo eye transport, controllers, AA and foveation maps |
| `src/vr/roblox_vr_arm64_build_contracts.h` | Selects exact ARM64 client contracts |
| `src/vr/roblox_vr_arm64_contract*.h` | Client-specific ARM instruction and object layout contracts |
| `src/audio/` | Audio playback and microphone recording bridges |
| `src/compat/host_abi_profile.cc` | Client startup, allocator and native host ABI profiles |
| `src/graphics/vulkan_foveation_adapter.inc` | Scene rendering integration for density maps |
| `tools/steam_frame/` | Frame launcher, Steam shortcut and Video settings integration |
| `tools/frame_video_settings/` | OpenVR settings, refresh-rate and timing helpers |
| `config/roblox_compatibility.json` | Supported client identities and compatibility metadata |
| `config/steam-frame.example.yaml` | Initial Frame runtime settings |

## Client compatibility

The ARM64 bridge supports these client identities:

| Version | Build | ELF build ID |
| --- | --- | --- |
| 2.742.1059 | 3222 | `25aa7fd31008a744c3c99f717e7d16d81d371d12` |
| 2.736.1408 | 2998 | `ed2b035768b1513561d0e6ea7f9bbec35b0e60e5` |

New clients can change native methods, virtual tables, object fields and startup
ordering. Add matching ARM64 contracts and host/audio profiles before enabling a
new client. Keep the existing signature and build identity checks in place.

The timing helper in `tools/frame_video_settings/frame_timings.cc` reports the
scene process, dashboard visibility, headset activity and compositor throttling
alongside frame times. Compare the same view in the same experience while the
headset is displaying projection frames. Standby or desktop-only frames do not
measure active VR throughput.

Shader sources and their generated SPIR-V header are included. Regenerating the
AA shaders with `tools/compile_gaze_aa.py` additionally needs `glslangValidator`.
The SMAA sources retain their upstream license in `src/vr/smaa/`.

## FSR shaders

The FSR 1 reference headers and MIT license are in `src/vr/fsr`. The Vulkan
fragment shader uses FP32 EASU around the gaze with a smooth transition to
bilinear scaling in the periphery. SMAA runs at output resolution; RCAS then
sharpens only the gaze disk. sRGB inputs use a raw copy to an encoded UNORM
image so EASU avoids repeated gamma conversions. The original eye provenance,
copy command submission and completion fence are retained.

After changing the shader, regenerate the checked-in SPIR-V with:

```sh
python3 tools/compile_fsr.py .
```

This requires `glslangValidator`. Build `fsr_upscaler_gpu_test` with testing
enabled for offscreen checks of source replacement, repeated frames, SMAA
ordering, gaze boundaries and alpha. Set `NIGHTCAP_AA_VALIDATE=1` to also enable
the Vulkan validation layer and synchronization checks.

The optional temporal pass is `src/vr/temporal.frag`, with rotational
reprojection in `include/mocktail/vr/temporal_reprojection.h`. It runs after
SMAA and before RCAS, reuses the gaze scratch region and copies only that region
to a per-eye unsharpened history. It adds no queue submission or frame fence.
The shader regeneration command above also rebuilds `temporal_spv.h`.

FSR 3.1 uses the MIT-licensed AMD FidelityFX SDK in `third_party/fidelityfx`.
Its gaze-region path receives matching native scene depth, projection jitter
and motion vectors. AMD optical flow supplies image motion when native motion
is unavailable. Each eye keeps its own temporal history; the rest of the image
uses inexpensive spatial scaling. SDK shader generation and standalone GPU
checks are described in the [FidelityFX build notes](../third_party/fidelityfx/README.md).

## Foveation tile calibration

Density-map texels and GMEM render tiles have different sizes. The gaze guard
must protect every render tile touching the full-detail disk. The narrow entry
in `include/mocktail/vr/foveation_tile_bounds.h` applies only to this identity:

- Turnip driver ID 18, vendor `0x5143`, device `0x43051401`, version `109060195`.
- Driver name `turnip Mesa driver`, info `Mesa 26.3.0-devel (git-abc426bb62)`.
- Driver UUID `586b9fa4514cb88b3c11a968ef4d62e4`; measured ELF build ID
  `b7e5995d0b202f06e81cd5a0f809ec1b744a0cb8`.
- A 2160 Ã— 2160 framebuffer, one layer/subpass, single-sample
  `R8G8B8A8_UNORM` color and `D32_SFLOAT` depth, plus the density attachment.
  Offsets, resolves, extra attachments, multiview and feedback/input attachments
  retain the conservative guard.

On that binary, `vkCmdBeginRenderPass2` at RVA `0xb12180` calls the layout
selector at `0xad67c0`. The selector initializes its layout from the FDM flag
at render-pass byte 145; subsequent checks can only select `AVOID_CCU` again.
The divisor routine at `0xa9e300` returns 1 for FDM. CPU-only creation of 72
render-pass/imageless-framebuffer combinations, covering FDM on/off and every
standard color/depth load/store combination, produced identical budgets:
356352 pixels for `FULL`, 350208 for `AVOID_CCU`. Their base tiles are
480 Ã— 736 and 768 Ã— 448 respectively. FDM therefore selects 768 Ã— 448
independently of shaders and load/store operations for this tuple.

The GPU qualification compared a high-frequency full-density reference against
the production map helper with its 68 Ã— 68 map and 768 Ã— 448 guard. Sweep both
gaze axes through `{-1, -.75, -.4, 0, .4, .75, 1}`, radii
`{.08, .15, .22, .35}`, and all three active profiles. All 588 cases preserved
every pixel inside the gaze disk exactly, with 588 valid fragment-invocation
queries and no validation errors. Mean fragment invocations were 51.46% of the
reference in this fixture; this measures shading work, not game FPS. Requalify
both the driver invariant and this image/counter sweep before adding identities
or pass tuples. Unknown identities retain the conservative policy.

The CPU identity and coordinate contracts can be rerun with:

```sh
cmake --build build-frame-arm64 --target foveation_tile_bounds_test framebuffer_density_map_test
ctest --test-dir build-frame-arm64 -R 'FoveationTileBounds|FramebufferDensityMap' --output-on-failure
```

## Frame-time stability

The Android `mallinfo` bridge shares a real host allocator snapshot for up to
500 ms. glibc scans allocator free lists under arena locks; several consecutive
queries therefore reuse a recent coherent sample. A scan runs outside the cache
mutex, and concurrent readers can keep using the previous completed sample.
Allocation, deallocation and the process-memory limit checks retain their normal
behavior.

Gaze density generation reuses per-eye pixel and half-float buffers across
frames. The density values and gaze protection are unchanged. At a 1:1 render
and output size with matching channel formats, FSR copies the encoded source
pixels directly instead of reconstructing pixels that already exist. Gaze AA,
optional temporal stabilization and RCAS keep their existing order.


The Vulkan eye copy is submitted on the OpenXR binding queue before releasing
its swapchain images, so SteamVR can enqueue composition immediately. When the
guest device requested exactly one unprotected queue, completion is deferred
until the next use of its AA/FSR resources. GPU queue ordering keeps subsequent guest
commands behind the copy. The upstream submission ring tracks command buffers, fences and source/destination
images. Presentation dependencies are consumed on the GPU and replaced with a
per-swapchain-image semaphore, so the CPU does not wait for the current render.
AA descriptors and temporal history wait only for previous eye-copy users;
current-frame present dependencies are excluded from that resource wait.
Captured source-image handles are drained before
image destruction; queue exposure, swapchain replacement and teardown also
drain pending work before releasing resources. `xrWaitFrame` remains on the
original render thread. Additional queues, readbacks and the virtual keyboard
retain completed-copy synchronization. `MOCKTAIL_VR_EARLY_SUBMIT=0` restores
the original submission order, while `MOCKTAIL_VR_DEFER_COPY_WAIT=0` keeps the
early submission but completes its fence before waiting for the next frame.

Density-map generation skips the distance/ramp calculation inside the protected
full-detail disk and beyond the peripheral plateau. Transition cells retain the
same distance calculation and spatial pattern. This reduces CPU work without
changing gaze protection or density values.

The Frame launcher enables a render-thread CPU preference after projection
submission starts. It reads Linux CPU capacity values only for the thread's
existing allowed CPUs and selects capacity peers within 80% of their maximum.
Incomplete capacity information or fewer than two preferred CPUs retains the
original allocation. This keeps render work on the application's performance
cores while respecting SteamVR's CPU reservation. Other engine threads retain
their normal allocation.

## Upstream integration

The imported Nightcap VR revision is recorded in [the credits](../THIRD_PARTY.md).
The Frame launcher disables the generic full-eye FXAA pass so Steam Video's
Anti Aliasing setting remains the single AA control. The upstream JNI haptic
protocol runs natively on ARM64. Its optional x86 SetMotor instruction scanner
is gated to x86; ARM64 build IDs, instruction checks and native return-value
bridges retain their own contracts.

Steam Video settings are loaded once at startup and then refreshed on a worker.
The rendering callbacks copy a completed snapshot; they never read the settings
file or parse JSON. Unchanged content reuses its parsed values. A failed refresh
keeps the last completed snapshot, and shutdown wakes the polling worker.
The launcher gives a configured per-app headset refresh rate precedence over
the desktop rate observed before the app gains focus, and derives Roblox's
startup FPS cap from that selected profile.
