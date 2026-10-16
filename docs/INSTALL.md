# Install on Steam Frame

## Requirements

- Steam Frame running its ARM64 Linux desktop, with Steam and SteamVR installed.
- Native SteamVR at `/opt/steamvr`, including `steamxr_linuxarm64.json`.
- The Frame's Turnip/Freedreno Vulkan driver under `/usr/lib` and its Vulkan ICD
  under `/usr/share/vulkan/icd.d`.
- Rootless Podman, Git, Python 3 and a C++17 compiler on the Frame.
- A Linux filesystem for the checkout, internet access and space for the source,
  Fedora build image and a separate Roblox client download.

The build dependencies are installed inside a Fedora 44 ARM64 container,
including `glslangValidator` and `spirv-val` for the FSR shaders. Keep the
Frame's own graphics drivers: the launcher mounts them into the container.
Use a case-sensitive Linux checkout because some Android headers differ only
by letter case. The repository is private, so GitHub access is required to clone it.

## 1. Get the source

```bash
git clone --branch steam-frame --recurse-submodules \
  https://github.com/InstanceBrick/nightcap-steam-frame.git \
  "$HOME/.local/share/nightcap-steam-frame-src"
cd "$HOME/.local/share/nightcap-steam-frame-src"
```

## 2. Build Nightcap

```bash
podman build --arch arm64 --jobs 1 \
  -f packaging/steam-frame/Containerfile \
  -t localhost/nightcap-vr-arm64:frame-foveated .
```

This builds Nightcap and its helpers from source. It does not download or bundle
Roblox. The image tag is also the one used by the Frame launcher.

## 3. Install the launcher and refresh-rate helper

```bash
python3 tools/steam_frame/install_launcher.py
mkdir -p "$HOME/.local/share/nightcap-vr-arm64/data/tools"
c++ -std=c++17 -O2 tools/frame_video_settings/refresh_rate.cc -ldl \
  -o "$HOME/.local/share/nightcap-vr-arm64/data/tools/refresh_rate"
```

The application menu entry is **Nightcap Roblox VR**.

## 4. Download the compatible Roblox client

Close Nightcap first. Prepare its data directory and copy the initial settings
only if no configuration exists yet:

```bash
frame_data="$HOME/.local/share/nightcap-vr-arm64/data"
mkdir -p "$frame_data/.config/mocktail"
if [ ! -f "$frame_data/.config/mocktail/config.yaml" ]; then
  cp config/steam-frame.example.yaml "$frame_data/.config/mocktail/config.yaml"
fi
podman run --rm --arch arm64 --userns=keep-id \
  --security-opt label=disable \
  -v "$frame_data:/data:rw" \
  -e HOME=/data -e XDG_CONFIG_HOME=/data/.config \
  -e XDG_DATA_HOME=/data/.local/share -e XDG_CACHE_HOME=/data/.cache \
  --entrypoint /opt/nightcap-vr-arm64/lib64/mocktail/mocktail_updater \
  localhost/nightcap-vr-arm64:frame-foveated \
  update --no-latest-check --skip-canary
```

The updater downloads the newest ARM64 client supported by the included catalog.
The command retains APK signature, architecture, build identity and payload
integrity checks. `--skip-canary` skips the separate desktop graphics test; VR
starts through the Frame launcher in the next step. Client files stay in your
local data directory.

## 5. Launch in VR and add to Steam

Start SteamVR and connect the headset, then open **Nightcap Roblox VR** from the
application menu. Sign in through Roblox's normal interface.

To add the Steam shortcut automatically, enable Steam's local browser debugging
endpoint at `127.0.0.1:8080`, keep Steam running and use:

```bash
python3 tools/steam_frame/register_steam_shortcut.py
```

The script creates or reuses the launcher shortcut, sets its Nightcap icon and
marks it as a VR application. It saves the generated identity locally. If the
endpoint is unavailable, use Steam's **Add a Non-Steam Game** dialog and select
`~/.local/bin/nightcap-vr-arm64`, then mark the shortcut as a VR application.

## 6. Add the SteamVR Video controls

The settings integration supports a specific SteamVR dashboard revision. It
checks the installed dashboard before applying changes and leaves other
revisions untouched. Valve dashboard files are read from your local installation.

With SteamVR closed and the original dashboard files visible:

```bash
mkdir -p frame-ui-local/originals frame-ui-local/generated
cp /opt/steamvr/resources/webinterface/dashboard/chunk~8012d0c89.js \
  frame-ui-local/originals/steamvr-dashboard.original.js
cp /opt/steamvr/resources/webinterface/dashboard/systemui.html \
  frame-ui-local/originals/steamvr-systemui.original.html
cp /opt/steamvr/resources/webinterface/dashboard/settings_desktop.html \
  frame-ui-local/originals/steamvr-settings_desktop.original.html
python3 tools/steam_frame/prepare_video_ui.py \
  frame-ui-local/originals frame-ui-local/generated
sudo python3 tools/steam_frame/install_video_ui.py frame-ui-local/generated
```

Start SteamVR again. The settings are described in [Video settings](SETTINGS.md).
The service mounts three generated files read-only; the original SteamVR files
are preserved. If upgrading an existing installation, reuse your saved original
files instead of copying the already patched dashboard.

## Update Nightcap

Close Nightcap, pull this branch and rebuild under a temporary image tag:

```bash
git pull --ff-only
git submodule update --init --recursive
podman build --arch arm64 --jobs 1 \
  -f packaging/steam-frame/Containerfile \
  -t localhost/nightcap-vr-arm64:next .
podman tag localhost/nightcap-vr-arm64:frame-foveated \
  localhost/nightcap-vr-arm64:previous
podman tag localhost/nightcap-vr-arm64:next \
  localhost/nightcap-vr-arm64:frame-foveated
python3 tools/steam_frame/install_launcher.py
```

Existing Roblox downloads, sign-in state and settings remain in the data
directory. To install a newly supported Roblox client, repeat step 4 after
updating Nightcap. Automatic client updates are off in the supplied Frame
configuration, since the ARM64 VR bridge needs matching client interfaces.

To restore the previous Nightcap image with the app closed:

```bash
podman tag localhost/nightcap-vr-arm64:previous \
  localhost/nightcap-vr-arm64:frame-foveated
```
