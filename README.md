<div align="center">
  <img src="packaging/io.github.CoderDayton.nightcap.svg" alt="Nightcap logo" width="128">
  <h1>Nightcap Roblox VR for Steam Frame</h1>
</div>

Native ARM64 Roblox VR for Steam Frame, with eye-tracked foveated rendering,
controller input and SteamVR integration.

The standard video profile is **native 2160 × 2160 per eye**, **72 Hz**,
**SMAA High** and **Performance** foveation. Frame rate depends on the Roblox
experience and its graphics settings; 72 FPS is the target.

## Features

- Native ARM64 execution through Nightcap's Android compatibility layer.
- Stereo OpenXR rendering, head tracking, VR controllers, haptics and an
  in-headset keyboard.
- Direct eye tracking with a full-detail gaze region and gradual reductions in
  peripheral shading quality.
- Adjustable gaze radius and Quality, Balanced and Performance foveation modes.
- Gaze-only FXAA, SMAA Low and SMAA High anti-aliasing.
- Native-resolution rendering with a 2160 × 2160 per-eye recommended default.
- Roblox's internal frame-rate cap follows the selected headset refresh rate.
- Renderer and session handling across menu, game and game-to-game transitions.
- Native microphone capture bridge for Roblox voice chat.
- One **Nightcap Roblox VR** Steam entry with the original Nightcap icon and a
  hidden desktop mirror.
- Simple **Foveated Rendering**, **Gaze sharp radius** and **Anti Aliasing**
  controls in SteamVR's Video settings.
- Optional in-headset performance overlay with actual game FPS, headset Hz,
  frame time, GPU time and current per-eye resolution.

## Install

Build and install on the Frame using the [installation guide](docs/INSTALL.md).
The repository contains source code, build instructions and integration tools.
The Roblox client is downloaded separately on the device; no Roblox binaries,
account data or prebuilt runtime images are distributed here.

Current ARM64 client support: **2.742.1059 (3222)** and **2.736.1408 (2998)**.

- [Installation and updates](docs/INSTALL.md)
- [Video, input and microphone settings](docs/SETTINGS.md)
- [Building and source layout](docs/DEVELOPMENT.md)

## About this fork

This Steam Frame edition is based on the `vr` branch of
[CoderDayton/nightcap](https://github.com/CoderDayton/nightcap), itself a fork of
[komaruworld/mocktail](https://github.com/komaruworld/mocktail). It adds the native
ARM64 VR bridge and Frame rendering, input, audio and Steam integration changes.
The original Nightcap artwork is retained. Internal executable and configuration
names keep `mocktail` for compatibility with the upstream source.

## License

[Apache License 2.0](LICENSE). Original copyright notices and third-party
licenses remain in their source directories. See [third-party credits](THIRD_PARTY.md).

Nightcap is an independent community project and is not affiliated with Roblox
Corporation or Valve.
