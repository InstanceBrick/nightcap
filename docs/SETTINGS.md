# Settings

## Video

Open the game's SteamVR **Video** settings after installing the settings
integration.

The standard profile is **native 2160 × 2160 per eye**, **72 Hz**, **SMAA High**
and **Performance** foveated rendering. The application targets 72 FPS at 72 Hz;
actual frame rate depends on the experience and Roblox's graphics settings.
The gaze radius remains adjustable, and existing saved choices are preserved.

| Setting | Effect |
| --- | --- |
| Foveated Rendering | Off uses full-density shading throughout. Quality, Balanced and Performance progressively reduce peripheral shading work. Each mode keeps the same full-detail gaze region. |
| Gaze sharp radius | Sets the minimum full-detail region around your gaze, from 8% to 35%. Larger values preserve more of the view at full density and cost more GPU time. Hardware tile boundaries can enlarge this region. |
| Anti Aliasing | Off, FXAA, SMAA Low or SMAA High. Filtering runs around your gaze on the composed eye image, including interface edges. SMAA High is the default. |
| Performance Overlay | Shows actual game FPS, headset Hz, average frame time, SteamVR GPU time and per-eye resolution at the bottom center of the headset view. It hides while the Steam menu is open and does not capture controller input. |
| Resolution Per Eye | Sets the headset eye-buffer size. Nightcap's recommended default is 2160 × 2160 per eye; Auto and Reset to Defaults use this baseline. Explicit custom values stay selected. Changes apply after the slider settles, with a brief pause while eye buffers are rebuilt at a safe frame boundary. |
| Display Refresh Rate | The standard profile uses 72 Hz. The launcher follows the selected per-game headset rate and any explicit Steam frame limit. Restart Nightcap after changing the rate or frame limit so Roblox's internal cap matches it. |

Enable **Performance Overlay**, then close the Steam menu. **APP FPS** counts
new Roblox projections over a rolling second. **HMD Hz** is the headset refresh
rate. **FRAME** is average time per submitted game frame, **TARGET** is the
configured application frame rate, and **BUDGET** is the time available per
application frame: 13.89 ms for 72 FPS at 72 Hz. Repeated or reprojected
compositor frames do not count as new game frames.

**GPU** averages SteamVR timings for new application frames, excluding
compositor-only reprojections. It can include scheduling gaps. The overlay
reports standby or missing frames instead of retaining a stale FPS. Its setting
persists between launches and can be turned off at any time.

Foveated rendering reduces fragment-shader work in the 3D scene before the
image is composed. The final composition stays at native resolution, keeping
screen-space text and interface elements from being coarsened a second time.
Your gaze region retains the detail available at the selected eye resolution;
the periphery uses progressively lower shading density.

Foveation, gaze radius and anti-aliasing controls update while the application
is running. Higher eye resolutions improve fine detail and increase GPU and
memory cost. For more headroom, keep Performance foveation, reduce the gaze
radius if needed, or lower Roblox's own graphics quality.

The launcher uses the selected Steam Video resolution without an extra
multiplier. To add supersampling, set `NIGHTCAP_VR_RENDER_SCALE` when launching
Nightcap; for example, `NIGHTCAP_VR_RENDER_SCALE=1.25`. The default is `1.0`.
Larger values increase GPU work.

The launcher reads the selected headset refresh rate and SteamVR's frame limit
on every launch. At 72 Hz with no frame limit, Roblox's internal scheduler is
capped at 72 FPS. Half-rate rendering is not forced automatically.

On Frame, the render thread prefers higher-capacity cores within the CPU
allocation supplied by SteamVR. To use the default thread placement, launch with
`NIGHTCAP_VR_PREFER_PERFORMANCE_CORES=0`.

## Controllers and typing

The native VR bridge supplies controller tracking, buttons, axes and haptics.
Roblox experiences choose how to use those inputs. The in-headset keyboard opens
when a text field receives focus; point at a key and pull the trigger to type.
Steam Controller desktop navigation can also be configured through the Steam
shortcut's controller layout.

## Microphone

Set the Frame's headset microphone as the default input in the desktop audio
settings and check that it is not muted. Roblox voice chat uses its normal
account eligibility, microphone permission and in-experience controls.

Nightcap's native recording bridge captures the selected input when Roblox
requests recording. To choose a different input, close Nightcap and edit
`~/.local/share/nightcap-vr-arm64/data/.config/mocktail/config.yaml`:

```yaml
audio:
  input_device: default
```

An exact SDL device name can replace `default`. Input device names appear in the
launch log.

Roblox's hidden desktop viewport stays at 1920 × 1080 for stable in-game menu
size. It is independent of the headset eye resolution. Desktop window geometry
is not restored or saved while the VR window is hidden.

## Files and troubleshooting

| Item | Location |
| --- | --- |
| Launcher | `~/.local/bin/nightcap-vr-arm64` |
| Launch log | `~/.local/share/nightcap-vr-arm64/last-launch.log` |
| Configuration | `~/.local/share/nightcap-vr-arm64/data/.config/mocktail/config.yaml` |
| Local client store | `~/.local/share/nightcap-vr-arm64/data/.local/share/mocktail` |
| Steam shortcut identity | `~/.local/share/nightcap-vr-arm64/steam-shortcut.json` |

If launching fails, check the launch log, make sure SteamVR is running and confirm
that the `localhost/nightcap-vr-arm64:frame-foveated` image exists. If the game
reports an unsupported client, update Nightcap before replacing the client.
Keep account data and client downloads outside source control.
