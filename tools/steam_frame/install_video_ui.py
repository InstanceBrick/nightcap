"""Install root-owned assets and a reversible, narrowly scoped boot service."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import argparse

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("work",type=Path,help="Directory containing generated UI assets and manifest")
args=parser.parse_args()

work = args.work.resolve()
root = Path('/etc/nightcap-vr-video-ui')
target = Path('/opt/steamvr/resources/webinterface/dashboard')
assert os.geteuid() == 0
manifest = json.loads((work/'steamvr-sharpening-ui-patch.json').read_text())
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
previous = json.loads((root/'steamvr-sharpening-ui-patch.json').read_text()) if (root/'steamvr-sharpening-ui-patch.json').exists() else {'assets': []}
previous_hashes = {a['target']: a['modified_sha256'] for a in previous['assets']}
for asset in manifest['assets']:
    assert Path(asset['source']).name == asset['source']
    assert Path(asset['target']).name == asset['target']
    assert digest(work/asset['source']) == asset['modified_sha256']
    assert digest(target/asset['target']) in [asset['original_sha256'], asset['modified_sha256'], previous_hashes.get(asset['target'])]
# Replace only our own known mounts, after verifying the previous manifest.
# Unmount before updating files so the service binds the fresh asset inode.
for asset in previous['assets']:
    mounted, source = target/asset['target'], root/asset['source']
    if source.exists() and mounted.stat().st_ino == source.stat().st_ino and mounted.stat().st_dev == source.stat().st_dev:
        assert digest(mounted) == asset['modified_sha256']
        subprocess.run(['umount', str(mounted)], check=True)
root.mkdir(mode=0o755, exist_ok=True)
assert not root.is_symlink()
os.chown(root, 0, 0)
os.chmod(root, 0o755)
for name in ['steamvr-sharpening-ui-patch.json', 'apply_steamvr_video_ui.py'] + [a['source'] for a in manifest['assets']]:
    destination = root/name
    assert not destination.is_symlink()
    source = Path(__file__).resolve().parent/name if name == 'apply_steamvr_video_ui.py' else work/name
    shutil.copyfile(source, destination)
    os.chown(destination, 0, 0)
    os.chmod(destination, 0o644)
# Migrate the earlier temporary mount from the user-owned asset. Only unmount
# this exact verified file, leaving every other mount and OS protection alone.
old = work/'steamvr-dashboard.sharpening.js'
mounted = target/'chunk~8012d0c89.js'
if mounted.stat().st_ino == old.stat().st_ino and mounted.stat().st_dev == old.stat().st_dev:
    subprocess.run(['umount', str(mounted)], check=True)
unit = Path('/etc/systemd/system/nightcap-vr-video-ui.service')
unit.write_text('''[Unit]
Description=Steam Frame gaze foveation and anti-aliasing controls
After=local-fs.target
Before=display-manager.service
RequiresMountsFor=/opt/steamvr/resources/webinterface/dashboard /etc/nightcap-vr-video-ui

[Service]
Type=oneshot
ExecStart=/usr/bin/python3 /etc/nightcap-vr-video-ui/apply_steamvr_video_ui.py
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
''')
os.chown(unit, 0, 0)
os.chmod(unit, 0o644)
subprocess.run(['systemctl', 'daemon-reload'], check=True)
subprocess.run(['systemctl', 'enable', 'nightcap-vr-video-ui.service'], check=True)
subprocess.run(['systemctl', 'restart', 'nightcap-vr-video-ui.service'], check=True)
print('Installed root-owned, version-guarded video UI assets and boot service')
