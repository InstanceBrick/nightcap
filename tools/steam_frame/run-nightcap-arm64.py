"""Run the ARM64 Nightcap build with the Frame's native drivers."""
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
import tempfile

def steam_profile_display_hz(active_hz, settings, app_key):
    # SteamVR can switch refresh when this scene app gains focus. Its stored
    # per-app rate takes precedence over the desktop rate observed at startup.
    if isinstance(settings, dict):
        for section in [settings.get(app_key), settings.get('steamvr')]:
            value = section.get('preferredRefreshRate') if isinstance(section, dict) else None
            if type(value) in (int, float) and math.isfinite(value) and 30 <= value <= 240:
                return value
    return active_hz

def steam_profile_frame_cap(hz, settings, app_key):
    if not isinstance(settings, dict):
        return str(hz)
    def throttle(section):
        value = section.get('framesToThrottle') if isinstance(section, dict) else None
        return value if type(value) is int and 0 <= value <= 5 else None
    frames = throttle(settings.get('steamvr'))
    frames = frames if frames is not None else 0
    app = settings.get(app_key)
    selected = throttle(app)
    if selected is not None:
        frames = selected
    if isinstance(app, dict) and app.get('motionSmoothingOverride') == 3:
        frames = max(frames, 1)
    # Round fractional targets up so the scheduler never caps below XR pacing.
    return str(max(1, math.ceil(hz / (1 + frames))))

def steam_profile_scene_samples(settings):
    # The native renderer always uses a single sample, including old profiles.
    return 1


def read_steam_profile(path):
    try:
        with path.open('rb') as profile:
            saved = profile.read(262145)
        if len(saved) <= 262144:
            value = json.loads(saved)
            if isinstance(value, dict):
                return value
    except (OSError, ValueError, TypeError):
        pass
    return {}


home = Path.home()
work = home / '.local/share/nightcap-vr-arm64'
data = work / 'data'
drivers = work / 'host-drivers'
data.mkdir(parents=True, exist_ok=True)
drivers.mkdir(exist_ok=True)
registry = data / '.config/openvr/openvrpaths.vrpath'
registry.parent.mkdir(parents=True, exist_ok=True)
(data / 'logs').mkdir(exist_ok=True)
(data / '.config/steamvr').mkdir(parents=True, exist_ok=True)
registry.write_text(json.dumps({
    'jsonid': 'vrpathreg', 'version': 1, 'runtime': ['/opt/steamvr'],
    'config': ['/data/.config/steamvr'], 'log': ['/data/logs'],
    'external_drivers': None,
}))
for pattern in ['libGLX_mesa.so*', 'libEGL_mesa.so*', 'libgallium*.so',
                'libgbm.so*', 'libdisplay-info.so*', 'libxml2.so.2*',
                'libicu*.so.74*']:
    for library in Path('/usr/lib').glob(pattern):
        target = drivers / library.name
        if not target.is_symlink() and not target.exists():
            target.symlink_to('/opt/host-lib/' + library.name)

icd_source = next(Path('/usr/share/vulkan/icd.d').glob('*freedreno*.json'))
icd = json.loads(icd_source.read_text())
icd['ICD']['library_path'] = '/opt/host-lib/libvulkan_freedreno.so'
(work / 'freedreno.json').write_text(json.dumps(icd))
(work / 'egl-mesa.json').write_text(json.dumps({
    'file_format_version': '1.0.0',
    'ICD': {'library_path': '/opt/host-drivers/libEGL_mesa.so.0'},
}))

steam_app_id=0
try:
    identity=json.loads((work/'steam-shortcut.json').read_text())
    candidate=identity.get('appid')
    if isinstance(candidate,int) and 0<candidate<=0xffffffff:steam_app_id=candidate
except (OSError,ValueError,TypeError):
    pass
vr_app_key='steam.app.'+str(steam_app_id) if steam_app_id else 'io.github.CoderDayton.nightcap.vr'

manifest = work / 'nightcap.vrmanifest'
manifest.write_text(json.dumps({'source':'builtin','applications':[{
    'app_key':vr_app_key,'launch_type':'binary',
    'binary_path_linux':str(home / '.local/bin/nightcap-vr-arm64'),
    'binary_path_linux_arm':str(home / '.local/bin/nightcap-vr-arm64'),
    'working_directory':str(work),'image_path':str(work / 'nightcap.png'),
    'strings':{'en_us':{'name':'Nightcap Roblox VR'}}
}]}))

uid = os.getuid()
args = ['podman', 'run', '--rm', '--arch', 'arm64', '--name', 'nightcap-arm64-test',
        '--userns=keep-id', '--group-add', 'keep-groups', '--network=host',
        '--ipc=host', '--pid=host', '--security-opt', 'label=disable']
for path, target, mode in [
    (data, '/data', 'rw'), (drivers, '/opt/host-drivers', 'ro'),
    (manifest, str(manifest), 'ro'),
    (Path('/usr/lib'), '/opt/host-lib', 'ro'),
    (Path('/opt/steamvr'), '/opt/steamvr', 'ro'),
    (home / '.config/openvr/config', '/opt/frame-steamvr-config', 'ro'),
    (Path('/tmp'), '/tmp', 'rw'),
    (Path(f'/run/user/{uid}'), f'/run/user/{uid}', 'rw'),
    (work / 'freedreno.json', '/opt/frame-freedreno.json', 'ro'),
    (work / 'egl-mesa.json', '/opt/frame-egl-mesa.json', 'ro'),
]:
    args += ['-v', f'{path}:{target}:{mode}']
for device in ['/dev/dri', '/dev/input', '/dev/snd']:
    if Path(device).exists():
        args += ['--device', device]
steam_settings = read_steam_profile(home / '.config/openvr/config/steamvr.vrsettings')
scene_msaa_samples = steam_profile_scene_samples(steam_settings)
msaa_flags = json.dumps({
    'FIntDebugForceMSAASamples': str(scene_msaa_samples),
    'FFlagForceOffMSAAForQuestVR': 'True',
})
print('Nightcap rendering: Native; single-sample scene with gaze anti-aliasing', flush=True)

# Roblox has a separate scheduler cap. Use the active HMD rate, rather than
# an SDL desktop refresh or a fixed 60/240 FPS value. Apply SteamVR's selected
# frame limit separately from the physical display rate. The helper is a background
# OpenVR client and never acquires scene focus.
refresh_limit='unlimited'
display_rate=''
try:
    probe_env=dict(os.environ)
    probe_env.pop('LD_PRELOAD',None)
    probe_env.pop('LD_LIBRARY_PATH',None)
    rate_probe=subprocess.run([str(data/'tools/refresh_rate')],capture_output=True,text=True,timeout=6,env=probe_env)
    rate_text=rate_probe.stdout.strip()
    if rate_probe.returncode==0 and rate_text.isdigit() and 30<=int(rate_text)<=240:
        profile=steam_settings
        selected_hz=steam_profile_display_hz(int(rate_text),profile,vr_app_key)
        display_rate=format(selected_hz,'g')
        refresh_limit=steam_profile_frame_cap(selected_hz,profile,vr_app_key)
        print('Roblox scheduler cap: '+refresh_limit+' FPS (selected headset '+display_rate+' Hz; startup '+rate_text+' Hz)',flush=True)
    else:
        print('Headset refresh unavailable; retaining Roblox maximum 240 FPS cap',flush=True)
except (OSError,subprocess.TimeoutExpired):
    print('Headset refresh query unavailable; retaining Roblox maximum 240 FPS cap',flush=True)

environment = {
    'HOME': '/data', 'XDG_CONFIG_HOME': '/data/.config',
    'XDG_DATA_HOME': '/data/.local/share', 'XDG_CACHE_HOME': '/data/.cache',
    'XDG_RUNTIME_DIR': f'/run/user/{uid}',
    'DISPLAY': os.environ.get('DISPLAY', ':1'), 'SDL_VIDEODRIVER': 'x11',
    'DBUS_SESSION_BUS_ADDRESS': f'unix:path=/run/user/{uid}/bus',
    'XR_RUNTIME_JSON': '/opt/steamvr/steamxr_linuxarm64.json',
    'MOCKTAIL_VR_RUNTIME': 'steamvr',
    'MOCKTAIL_VR_PREFER_PERFORMANCE_CORES': os.environ.get('NIGHTCAP_VR_PREFER_PERFORMANCE_CORES', '1'),
    'MOCKTAIL_VR_FRAME_RATE_LIMIT': refresh_limit,
    'MOCKTAIL_VR_DISPLAY_HZ': display_rate,
    'MOCKTAIL_PASS_CURRENT_DISPLAY_REFRESH_RATE': '1',
    # XR paces presentation; avoid a second cap from the hidden desktop window.
    'MOCKTAIL_VSYNC': 'off',
    # Yield between main-thread polls so the VR render and compositor can run.
    'MOCKTAIL_ENGINE_PUMP_REST': 'sleep',
    'MOCKTAIL_VR_ANTI_ALIASING': 'off',
    'MOCKTAIL_VR_FOVEATION': '1',
    'MOCKTAIL_VR_APPLICATION_NAME': 'Nightcap Roblox VR',
    'NIGHTCAP_STEAM_APP_ID': str(steam_app_id),
    'NIGHTCAP_VR_MANIFEST': str(manifest),
    'MOCKTAIL_WIN_TITLE': 'Nightcap Roblox VR',
    # Keep Roblox panel layout stable independently of headset render size.
    'MOCKTAIL_WIN_WIDTH': '1920', 'MOCKTAIL_WIN_HEIGHT': '1080',
    'MOCKTAIL_VR_DESKTOP_MIRROR': '0',
    'MOCKTAIL_VR_HIDE_DESKTOP_WINDOW': '1',
    'MOCKTAIL_SHOW_WINDOW_ON_FIRST_SWAP': '0',
    'MOCKTAIL_CLIENT_SETTINGS_OVERRIDES_JSON': msaa_flags,
    'MOCKTAIL_FAST_FLAGS_JSON': msaa_flags,
    'MOCKTAIL_GAME_LEAVE_COMPLETES_WITHOUT_LUA_CALLBACK': '1',
    'MOCKTAIL_VR_RENDER_SCALE': os.environ.get('NIGHTCAP_VR_RENDER_SCALE', '1.0'),
    'LD_LIBRARY_PATH': '/opt/host-drivers:/opt/nightcap-vr-arm64/lib64:/opt/nightcap-vr-arm64/lib',
    'VK_DRIVER_FILES': '/opt/frame-freedreno.json',
    'LIBGL_DRIVERS_PATH': '/opt/host-lib/dri',
    '__EGL_VENDOR_LIBRARY_FILENAMES': '/opt/frame-egl-mesa.json',
    'MOCKTAIL_COMPATIBILITY_MANIFEST': '/opt/nightcap-vr-arm64/share/mocktail/metadata/roblox_compatibility.json',
    'MOCKTAIL_UPDATE_COMPATIBILITY_PATH': '/opt/nightcap-vr-arm64/share/mocktail/metadata/roblox_compatibility.json',
    'MOCKTAIL_UPDATE_SIGNING_TRUST_PATH': '/opt/nightcap-vr-arm64/share/mocktail/metadata/roblox_signing_certificates.json',
    'MOCKTAIL_UPDATE_HOST_ABI_REFERENCE': '/opt/nightcap-vr-arm64/share/mocktail/metadata/roblox_host_abi_reference.json',
    'MOCKTAIL_BOOTSTRAP_SOURCES_PATH': '/opt/nightcap-vr-arm64/share/mocktail/metadata/roblox_bootstrap_sources.json',
    'MOCKTAIL_RUNTIME_LIBRARY_DIR': '/opt/nightcap-vr-arm64/lib64/mocktail',
}
telemetry_path = None
if not (len(sys.argv) > 1 and sys.argv[1] == '--probe'):
    runtime = Path(f'/run/user/{uid}')
    try:
        telemetry_fd, telemetry_path = tempfile.mkstemp(prefix='nightcap-performance-', dir=runtime)
        os.ftruncate(telemetry_fd, 4096)
        os.close(telemetry_fd)
        environment['NIGHTCAP_PERFORMANCE_TELEMETRY'] = telemetry_path
    except OSError as error:
        print('[performance-overlay] telemetry unavailable: ' + str(error), flush=True)

for key, value in environment.items():
    args += ['-e', key + '=' + value]
for key in ['SteamAppId', 'SteamGameId', 'STEAM_COMPAT_APP_ID', 'VK_LOADER_DEBUG']:
    if key in os.environ:
        args += ['-e', key]
probe = len(sys.argv) > 1 and sys.argv[1] == '--probe'
program = 'mocktail-vr-probe' if probe else 'mocktail'
program_args = sys.argv[2:] if probe else ['--vr'] + sys.argv[1:]
program_dir = '/opt/nightcap-vr-arm64/bin/' if probe else '/opt/nightcap-vr-arm64/lib64/mocktail/'
args += ['--entrypoint', program_dir + program,
         'localhost/nightcap-vr-arm64:frame-foveated'] + program_args
os.environ.pop('LD_PRELOAD', None)
os.environ.pop('LD_LIBRARY_PATH', None)
if probe:
    raise SystemExit(subprocess.call(args))
child = subprocess.Popen(args)
overlay = None
scene_pid = None
for attempt in range(40):
    if child.poll() is not None:
        break
    check = subprocess.run(['podman', 'inspect', '--format', '{{.State.Pid}}', 'nightcap-arm64-test'], capture_output=True, text=True)
    if check.returncode == 0 and check.stdout.strip().isdigit() and int(check.stdout.strip()) > 0:
        scene_pid = check.stdout.strip()
        identity = subprocess.run(['podman','exec','nightcap-arm64-test','/opt/nightcap-vr-arm64/bin/nightcap-vr-settings','--identify',check.stdout.strip()], capture_output=True, text=True)
        for line in identity.stdout.splitlines():
            if line.startswith(('[vr-identity]', '[vr-pacing]')):
                print(line, flush=True)
        if identity.returncode:
            print('[vr-identity] registration unavailable; VR continues', flush=True)
        break
    time.sleep(.1)
try:
    retry_overlay_at = 0.
    while child.poll() is None:
        if telemetry_path and scene_pid and (overlay is None or overlay.poll() is not None):
            if time.monotonic() >= retry_overlay_at:
                # A separate headless overlay container cannot leave an active
                # exec session blocking cleanup of the main application.
                overlay_args = args[:-len(program_args)]
                overlay_args = list(overlay_args)
                overlay_args[overlay_args.index('--name')+1] = 'nightcap-performance-overlay'
                overlay_args[overlay_args.index('--entrypoint')+1] = '/opt/nightcap-vr-arm64/bin/nightcap-vr-performance'
                overlay = subprocess.Popen(overlay_args + [scene_pid, telemetry_path])
                retry_overlay_at = time.monotonic() + 10.
        time.sleep(.5)
    result = child.returncode
finally:
    if overlay is not None:
        try:
            overlay.wait(timeout=3)
        except subprocess.TimeoutExpired:
            subprocess.run(['podman','stop','--time','2','nightcap-performance-overlay'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            overlay.wait(timeout=5)
    if telemetry_path:
        Path(telemetry_path).unlink(missing_ok=True)
raise SystemExit(result)
