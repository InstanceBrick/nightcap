# Credits and third-party components

This edition includes modifications for Steam Frame ARM64 and is based on:

- [Nightcap](https://github.com/CoderDayton/nightcap), including vii's changes to
  Mocktail and the upstream `vr` branch.
- [Mocktail](https://github.com/komaruworld/mocktail), by the Mocktail project
  authors.

The Steam Frame edition incorporates the upstream VR tree through
[`753a3f81`](https://github.com/CoderDayton/nightcap/commit/753a3f8106e4d7fdafea457601a5ff238e77c2d0)
(15 October 2026), including GPU-ordered presentation, asynchronous eye and
mirror copies, cached JNI method dispatch, headset detection and the JNI haptic
protocol. ARM64 client contracts and Frame gaze AA remain platform-specific.

The project is distributed under the [Apache License 2.0](LICENSE). Existing
copyright and modification notices are retained in the source.

Third-party components retain their own licenses and notices, including the
Android/Bionic linker sources under `third_party/mcpelauncher-linker`, the pinned
OpenXR SDK, Vulkan headers and libjnivm submodules, SMAA under `src/vr/smaa`, AMD FidelityFX FSR 1 under `src/vr/fsr`
([MIT license](src/vr/fsr/LICENSE.txt)),
AMD FidelityFX SDK 1.1.4's FSR 3.1.4 upscaler, Optical Flow component and Vulkan
backend under `third_party/fidelityfx`
([MIT license](third_party/fidelityfx/LICENSE.txt)), and
the OpenVR interface header under `tools/frame_video_settings`. The original
Nightcap icon and artwork are retained from the upstream project.

Roblox client binaries and Valve SteamVR dashboard assets are not included.
The client is downloaded separately and the Video settings tools read dashboard
files from the user's installed SteamVR copy.
