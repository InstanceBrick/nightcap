"""Boot-time, version-guarded bind mounts for the Frame video controls."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

ROOT = Path('/etc/nightcap-vr-video-ui')
TARGET = Path('/opt/steamvr/resources/webinterface/dashboard')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    assert os.geteuid() == 0
    manifest = json.loads((ROOT/'steamvr-sharpening-ui-patch.json').read_text())
    assets = manifest['assets']
    assert {a['target'] for a in assets} == {
        'chunk~8012d0c89.js', 'systemui.html', 'settings_desktop.html'}
    # Verify every file before modifying any mount. A SteamVR update is left
    # untouched until a matching patch has been regenerated and reviewed.
    pending = []
    for a in assets:
        source, target = ROOT/a['source'], TARGET/a['target']
        assert source.parent == ROOT and target.parent == TARGET
        assert digest(source) == a['modified_sha256'], 'Patch digest mismatch'
        current = digest(target)
        if current == a['modified_sha256']:
            continue
        if current != a['original_sha256']:
            raise SystemExit('SteamVR version changed; UI patch was not applied: '+a['target'])
        pending.append((source, target))
    for source, target in pending:
        subprocess.run(['mount', '--bind', str(source), str(target)], check=True)
        subprocess.run(['mount', '-o', 'remount,bind,ro', str(target)], check=True)
        print('Mounted video UI asset:', target.name)
    print('SteamVR video UI verified')

if __name__ == '__main__':
    main()
