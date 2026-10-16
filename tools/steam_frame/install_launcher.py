#!/usr/bin/env python3
"""Install the Frame launcher and original Nightcap icons for the current user."""
from pathlib import Path
import shutil
import subprocess

source = Path(__file__).resolve().parent
checkout = source.parents[1]
home = Path.home()
work = home / '.local/share/nightcap-vr-arm64'
identity = 'io.github.CoderDayton.nightcap'
work.mkdir(parents=True, exist_ok=True)
(home / '.local/bin').mkdir(parents=True, exist_ok=True)
shutil.copyfile(source / 'run-nightcap-arm64.py', work / 'run-nightcap-arm64.py')
launcher = home / '.local/bin/nightcap-vr-arm64'
shutil.copyfile(source / 'nightcap-vr-arm64', launcher)
launcher.chmod(0o755)
packaging = checkout / 'packaging'
icons = packaging / 'icons/hicolor'
shutil.copyfile(icons / ('128x128/apps/' + identity + '.png'), work / 'nightcap.png')
for base in [home / '.local/share', work / 'data/.local/share']:
    theme = base / 'icons/hicolor'
    for icon in icons.glob('*/apps/' + identity + '.png'):
        target = theme / icon.relative_to(icons)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(icon, target)
    svg = theme / ('scalable/apps/' + identity + '.svg')
    svg.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(packaging / (identity + '.svg'), svg)
    if shutil.which('gtk-update-icon-cache'):
        subprocess.run(['gtk-update-icon-cache', '--force', '--ignore-theme-index', str(theme)], check=True)
applications = home / '.local/share/applications'
applications.mkdir(parents=True, exist_ok=True)
entry = applications / (identity + '.desktop')
entry.write_text(f'''[Desktop Entry]
Type=Application
Name=Nightcap Roblox VR
Comment=Native ARM64 Roblox VR for Steam Frame
Exec=/usr/bin/python3 "{launcher}"
Icon={identity}
Terminal=false
Categories=Game;
StartupNotify=false
StartupWMClass={identity}
''', encoding='utf-8')
entry.chmod(0o755)
legacy = applications / 'nightcap-vr-arm64.desktop'
if legacy.exists():
    text = '\n'.join(line for line in legacy.read_text().splitlines()
                     if not line.startswith(('NoDisplay=', 'StartupWMClass=')))
    legacy.write_text(text + '\nNoDisplay=true\n')
if shutil.which('update-desktop-database'):
    subprocess.run(['update-desktop-database', str(applications)], check=True)
print('Installed Nightcap Roblox VR launcher:', entry)
