"""Generate the version-pinned Frame video controls from local SteamVR assets."""
from pathlib import Path
import hashlib
import json
import argparse

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("originals",type=Path,help="Directory holding the three saved original assets")
parser.add_argument("output",type=Path,help="Directory for generated local assets")
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)

work = args.output.resolve()
source = args.originals.resolve()
original = source / 'steamvr-dashboard.original.js'
expected = {
  'steamvr-dashboard.original.js':'937ec62c67a481f030a416099ceb98b0fe2a702aea5a0705601c6c40d4969f6d',
  'steamvr-systemui.original.html':'f9edb70f9fdf5c7122ee2dfa9420614018f4f96c5a022b3576abf698acffc4d9',
  'steamvr-settings_desktop.original.html':'b8df83b228354052964ff913d706eb65163037ae12faea40ae1ac36a684e756c',
}
for name, digest in expected.items():
    if hashlib.sha256((source/name).read_bytes()).hexdigest()!=digest:
        raise SystemExit('SteamVR version differs; no patch generated: '+name)
text = original.read_text(encoding='utf-8')
helper_anchor = 'function y(e,t){var r;const n=_.p.defaultPerAppRefreshRateValue;'
assert text.count(helper_anchor)==1
helper=(Path(__file__).resolve().parent/'steamvr-simple-video-controls.js').read_text(encoding='utf-8')
text=text.replace(helper_anchor,helper+'\n'+helper_anchor,1)

# The device-wide GPU recommendation can differ from Nightcap's actual native
# default. Scope its pixel detent and Auto dimensions to this app's identity;
# explicit slider values and every other app keep the packaged behavior.
identity_anchor='get computedResolution(){var e;let t=1,r=1;'
assert text.count(identity_anchor)==1
identity_getter='get usesNightcapResolution(){return NightcapResolutionApp(this.currentApp?this.currentApp.key:void 0,this.currentApp?g.HR.GetAppName(this.currentApp.key):void 0)}'
text=text.replace(identity_anchor,identity_getter+identity_anchor,1)
auto_anchor='Math.pow(this.m_nResolutionOverride/g.HR.systemInfo.render_target_size.width,2):_.p.supersampleGpuScaleValue:'
assert text.count(auto_anchor)==1
text=text.replace(auto_anchor,'Math.pow(this.m_nResolutionOverride/g.HR.systemInfo.render_target_size.width,2):(this.usesNightcapResolution?Math.pow(2160/g.HR.systemInfo.render_target_size.width,2):_.p.supersampleGpuScaleValue):',1)
detent_anchor='y=[_.p.ComputeResolution(e,e)[0]]}const f='
assert text.count(detent_anchor)==1
text=text.replace(detent_anchor,'y=[this.usesNightcapResolution?2160:_.p.ComputeResolution(e,e)[0]]}const f=',1)
dimensions_anchor='s.createElement("div",{className:"Dimensions"},this.computedResolution),!m&&this.m_nResolutionMultiplier'
assert text.count(dimensions_anchor)==1
text=text.replace(dimensions_anchor,'s.createElement("div",{className:"Dimensions"},this.computedResolution),m&&this.usesNightcapResolution&&s.createElement("small",{className:"Percentage",style:{pointerEvents:"none"}},"Recommended 2160 × 2160"),!m&&this.m_nResolutionMultiplier',1)
reset_anchor='resetAppToDefaults(){return(0,n.sH)(this,void 0,void 0,(function*(){yield g.HR.ResetAppSettings(this.currentApp.key),this.fetchAppState(this.currentApp.key)}))}'
assert text.count(reset_anchor)==1
reset_default='resetAppToDefaults(){if(!this.usesNightcapResolution)return(0,n.sH)(this,void 0,void 0,(function*(){yield g.HR.ResetAppSettings(this.currentApp.key),this.fetchAppState(this.currentApp.key)}));const appKey=this.currentApp.key;return(0,n.sH)(this,void 0,void 0,(function*(){yield g.HR.ResetAppSettings(appKey);yield g.HR.SetAppSettings(appKey,{resolution_override:2160});this.currentApp&&this.currentApp.key===appKey&&this.fetchAppState(appKey)}))}'
text=text.replace(reset_anchor,reset_default,1)

global_anchor = 'C.c.m_bSupportsEyeTracking&&s.createElement(v.Al,{name:"/settings/steamvr/sharpening",label:(0,p.we)("#Settings_Advanced_FoveatedSharpening_Header"),title:(0,p.we)("#Settings_Advanced_FoveatedSharpening_Description")})'
assert text.count(global_anchor) == 1
text = text.replace(global_anchor, global_anchor + ',C.c.m_bSupportsEyeTracking&&s.createElement(NightcapVideoControls,{global:!1})', 1)
per_app_anchor = 'onChange:this.onResolutionSliderChange}),s.createElement(v.YO,{className:"SettingsItem"},s.createElement(L,null))))'
assert text.count(per_app_anchor) == 1
text = text.replace(per_app_anchor, per_app_anchor + ',w&&s.createElement(NightcapVideoControls,{global:!0})', 1)
output = work / 'steamvr-dashboard.sharpening.js'
output.write_text(text,encoding='utf-8',newline='')
manifest = {'original_sha256':hashlib.sha256(original.read_bytes()).hexdigest(),
            'modified_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
            'global_sliders':1,'per_application_sliders':1,'custom_selectors':2}
manifest['assets'] = [{
    'target': 'chunk~8012d0c89.js', 'source': output.name,
    'original_sha256': manifest['original_sha256'],
    'modified_sha256': manifest['modified_sha256'],
}]
# The packaged HTML pins a content hash in its script URL. Change that URL too,
# otherwise a reload can still use a cached copy of the original JavaScript.
for name in ['systemui', 'settings_desktop']:
    original_html = args.originals / ('steamvr-' + name + '.original.html')
    html = original_html.read_text(encoding='utf-8')
    anchor = 'chunk~8012d0c89.js?contenthash=0f8bbe756c2d47f74f5a'
    assert html.count(anchor) == 1
    html = html.replace(anchor, 'chunk~8012d0c89.js?contenthash=' + manifest['modified_sha256'][:20])
    patched = work / ('steamvr-' + name + '.sharpening.html')
    patched.write_text(html, encoding='utf-8', newline='')
    manifest['assets'].append({
        'target': name + '.html', 'source': patched.name,
        'original_sha256': hashlib.sha256(original_html.read_bytes()).hexdigest(),
        'modified_sha256': hashlib.sha256(patched.read_bytes()).hexdigest(),
    })
(work/'steamvr-sharpening-ui-patch.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest))
