#!/usr/bin/env bash
set -euo pipefail

# Run without host mounts, a GPU, SteamVR, account data or network access.
image=${1:-localhost/nightcap-vr-arm64:frame-foveated}
platform=$(podman image inspect --format '{{.Os}}/{{.Architecture}}' "$image")
if [[ "$platform" != linux/arm64 ]]; then
  printf 'Expected linux/arm64 image, got %s\n' "$platform" >&2
  exit 1
fi

podman run --rm -i --network none --read-only --cap-drop all \
  --security-opt no-new-privileges --tmpfs /tmp:rw,nosuid,nodev,size=16m \
  --entrypoint python3 "$image" - <<'PY'
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

prefix = Path('/opt/nightcap-vr-arm64')
runtime = prefix / 'lib64/mocktail'
programs = [
    prefix / 'bin/mocktail',
    prefix / 'bin/mocktail-vr-probe',
    prefix / 'bin/nightcap-vr-settings',
    prefix / 'bin/nightcap-vr-performance',
    runtime / 'mocktail',
    runtime / 'mocktail_updater',
    runtime / 'mocktail_failure_dialog',
    runtime / 'mocktail_webview_helper',
]
libraries = list(runtime.glob('*.so'))
if len(libraries) < 12:
    raise SystemExit('Installed Android adapter libraries are missing')
for path in programs + libraries:
    if not path.is_file() or not os.access(path, os.X_OK):
        raise SystemExit(f'Missing executable: {path}')
    with path.open('rb') as stream:
        header = stream.read(20)
    if (header[:6] != b'\x7fELF\x02\x01' or len(header) != 20
            or struct.unpack_from('<H', header, 18)[0] != 183):
        raise SystemExit(f'Expected a little-endian AArch64 ELF: {path}')
    result = subprocess.run(['ldd', str(path)], capture_output=True, text=True,
                            timeout=20, check=False)
    if result.returncode or 'not found' in result.stdout + result.stderr:
        raise SystemExit(f'Unresolved runtime dependencies: {path}\n'
                         + result.stdout + result.stderr)

metadata = prefix / 'share/mocktail/metadata'
for name in ('roblox_compatibility.json', 'roblox_bootstrap_sources.json',
             'roblox_host_abi_reference.json', 'roblox_signing_certificates.json'):
    value = json.loads((metadata / name).read_text())
    if not isinstance(value, (dict, list)) or not value:
        raise SystemExit(f'Invalid installed metadata: {name}')
license_files = [Path('/work/LICENSE'), Path('/work/THIRD_PARTY.md')]
for path in license_files:
    if not path.is_file() or not path.stat().st_size:
        raise SystemExit(f'Missing source license notices: {path}')

def verify_native_cmake_settings(cache_text):
    settings = {}
    for line in cache_text.splitlines():
        if not line or line.startswith(('#', '//')):
            continue
        declaration, separator, value = line.partition('=')
        name, type_separator, cache_type = declaration.partition(':')
        if separator and type_separator and cache_type:
            settings[name] = value
    # VR is a STRING option (AUTO/ON/OFF), while the other flags are BOOL.
    # The configured value determines the build policy, not the cache type.
    required = {'MOCKTAIL_ENABLE_VR': 'ON',
                'MOCKTAIL_ENABLE_FSR3': 'OFF',
                'MOCKTAIL_VR_DIAGNOSTICS': 'OFF'}
    for name, expected in required.items():
        if settings.get(name) != expected:
            raise SystemExit(f'Unexpected build setting: {name}={settings.get(name)!r}; expected {expected}')


cache = Path('/work/build-frame-arm64/CMakeCache.txt')
build_policy = 'source-build-cache'
if cache.exists():
    verify_native_cmake_settings(cache.read_text())
else:
    # Source-audited release images may omit their disposable compiler cache.
    provenance = json.loads((metadata / 'steam-frame-source.json').read_text())
    if provenance.get('fsr3_compiled') is not False or provenance.get('upscaling') != 'Native':
        raise SystemExit('Missing Native-only build provenance')
    build_policy = 'installed-source-provenance'

for path in ('/opt/steamvr', '/opt/host-lib', '/work/data', '/work/runtime',
             '/work/rbx_bin', '/root/.config/mocktail',
             '/root/.local/share/mocktail'):
    if Path(path).exists():
        raise SystemExit(f'Host runtime or user data must not be packaged: {path}')
patterns = ('libroblox.so', '*.apk', '*.xapk', '*.aab', '*.obb',
            'cookies*.txt', 'steamclient.so', 'vrclient.so')
# This upstream archive-parser regression fixture is not a Roblox client.
archive_fixture = Path('/work/third_party/mcpelauncher-linker/core/libziparchive/testdata/crash.apk')
for root in ('/work', '/opt', '/root', '/home'):
    for directory, subdirectories, filenames in os.walk(root, followlinks=False):
        if '.git' in subdirectories or '.git' in filenames:
            raise SystemExit('Git checkout credentials/history must not enter the image')
        for name in filenames:
            path = Path(directory) / name
            if path == archive_fixture:
                continue
            if any(fnmatch.fnmatch(name.lower(), pattern) for pattern in patterns):
                raise SystemExit(f'Proprietary payload or account data in image: {path}')

print(json.dumps({
    'platform': 'linux/arm64',
    'native_only_build_policy': build_policy,
    'aarch64_programs_checked': len(programs),
    'aarch64_adapter_libraries_checked': len(libraries),
    'dynamic_dependencies': 'resolved',
    'client_payload': 'absent',
    'host_steamvr_and_driver_mounts': 'absent',
    'license_sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                       for path in license_files},
    'scope': 'offline packaging checks; no headset or Roblox session started',
}, indent=2))
PY
