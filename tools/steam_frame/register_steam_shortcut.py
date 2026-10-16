#!/usr/bin/env python3
"""Register one VR shortcut through Steam's local SharedJSContext."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

home = Path.home()
work = home / '.local/share/nightcap-vr-arm64'
launcher = home / '.local/bin/nightcap-vr-arm64'
if not launcher.is_file() or not (work / 'nightcap.png').is_file():
    raise SystemExit('Run install_launcher.py first.')
expression = '''(async()=>{
 const exe=LAUNCHER;
 const existing=await SteamClient.Apps.GetShortcutDataForPath(exe);
 let id=existing&&typeof existing==='object'?(existing.unAppID||existing.appid||existing.appId):0;
 if(!id)id=await SteamClient.Apps.AddShortcut('Nightcap Roblox VR',exe,'',exe);
 if(!Number.isInteger(id)||id<=0||id>0xffffffff)throw new Error('Invalid shortcut ID');
 await SteamClient.Apps.SetShortcutIcon(id,ICON);
 await SteamClient.Apps.SetShortcutIsVR(id,true);
 await SteamClient.Apps.SetShortcutStartDir(id,WORK);
 return JSON.stringify({appid:id,isVR:true});
})()'''
for marker, value in [('LAUNCHER', str(launcher)), ('ICON', str(work / 'nightcap.png')), ('WORK', str(work))]:
    expression = expression.replace(marker, json.dumps(value))
with tempfile.TemporaryDirectory(prefix='nightcap-shortcut-') as temporary:
    script = Path(temporary) / 'shortcut.js'
    script.write_text(expression, encoding='utf-8')
    result = subprocess.run([sys.executable, str(Path(__file__).with_name('steam_native_action.py')), str(script)],
                            check=True, capture_output=True, text=True)
    reply = json.loads(result.stdout)
    if 'exceptionDetails' in reply.get('result', {}):
        raise SystemExit('Steam rejected shortcut registration: ' + json.dumps(reply['result']['exceptionDetails']))
    identity = json.loads(reply['result']['result']['value'])
    if not isinstance(identity.get('appid'), int) or not 0 < identity['appid'] <= 0xffffffff:
        raise SystemExit('Steam returned an invalid shortcut identity.')
    (work / 'steam-shortcut.json').write_text(json.dumps(identity, indent=2) + '\n')
    print('Nightcap Roblox VR shortcut registered:', identity['appid'])
