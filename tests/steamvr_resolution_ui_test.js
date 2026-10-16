const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
assert(process.argv[2],'Pass the generated version-pinned dashboard JavaScript');
const patched=fs.readFileSync(process.argv[2],'utf8');
const helper=fs.readFileSync(path.join(__dirname,'../tools/steam_frame/steamvr-simple-video-controls.js'),'utf8');
const classStart=patched.indexOf('let G=w=class extends s.Component');assert(classStart>=0);
const classSource=patched.slice(classStart);
function method(signature){
  const start=classSource.indexOf(signature);assert(start>=0,signature);
  const open=classSource.indexOf('{',start);let depth=0,quote='',escape=false;
  for(let i=open;i<classSource.length;++i){const c=classSource[i];
    if(quote){if(escape)escape=false;else if(c==='\\')escape=true;else if(c===quote)quote='';continue;}
    if(c==='"'||c==="'"){quote=c;continue;}
    if(c==='{')++depth;else if(c==='}'&&--depth===0)return classSource.slice(start,i+1);
  }throw Error('Unclosed method '+signature);
}
const calls=[],names=new Map([['steam.app.123','Nightcap Roblox VR'],['steam.app.456','Another VR Game']]);
const context={
  s:{createElement:(type,props,...children)=>({type,props,children})},
  g:{HR:{systemInfo:{resolution_per_app:true,render_target_size:{width:2200}},GetAppName:key=>names.get(key),
    ResetAppSettings:async key=>{calls.push(['reset',key]);},SetAppSettings:async(key,value)=>{calls.push(['set',key,JSON.parse(JSON.stringify(value))]);}}},
  _:{p:{supersampleGpuScaleValue:.64,minSupersampleScaleValue:.01,maxSupersampleScaleValue:16,
    supersampleManualOverride:false,supersampleScaleValue:1,ComputeResolution:(x,y)=>[Math.round(2200*Math.sqrt(x)),Math.round(2200*Math.sqrt(y))]}},
  b:{OQ:(value,min,max)=>Math.min(max,Math.max(min,value))},N:{Default:0,Letterbox:1,Advanced:2},
  p:{we:(key,...values)=>values.length?values.join(' × '):key},
  n:{sH:async(self,a,b,generator)=>{const iterator=generator.call(self);let value;for(;;){const next=iterator.next(value);if(next.done)return next.value;value=await next.value;}}}
};
vm.createContext(context);vm.runInContext(helper,context);
const api=vm.runInContext('({'+['get usesNightcapResolution()','get computedResolution()','onResolutionSliderChange(e)','setAppState(e,t)','resetAppToDefaults()'].map(method).join(',')+'})',context);
const model=(key='steam.app.123',resolution=0)=>Object.assign(Object.create(api),{
  currentApp:{key},m_nResolutionOverride:resolution,m_nResolutionMultiplier:1,m_eFovScaleFormat:0,
  m_appSettings:{response:{}},fetchAppState:key=>calls.push(['fetch',key])});
const begin=patched.indexOf('let b=_.p.minSupersampleScaleValue,S=_.p.maxSupersampleScaleValue,y=[1];');
assert(begin>=0);const end=patched.indexOf(',R=this.m_eFovScaleFormat',begin);assert(end>begin);
const slider=vm.runInContext('(function(m){var e;'+patched.slice(begin,end)+';return{min:b,max:S,detents:y,value:f};})',context);
const noteAnchor='m&&this.usesNightcapResolution&&s.createElement("small",';
const noteStart=patched.indexOf(noteAnchor);assert(noteStart>=0);
const noteEnd=patched.indexOf('),!m&&this.m_nResolutionMultiplier',noteStart);assert(noteEnd>noteStart);
const note=vm.runInContext('(function(m){return '+patched.slice(noteStart,noteEnd+1)+';})',context);
(async()=>{
  for(const key of ['steam.app.123','io.github.coderdayton.nightcap.vr','io.github.CoderDayton.nightcap.vr']){
    for(const resolution of [0,null,undefined]){
      const app=model(key,resolution);assert.equal(app.usesNightcapResolution,true);assert.equal(app.computedResolution,'2160 × 2160');
      assert.deepEqual(Array.from(slider.call(app,true).detents),[2160]);
      assert.equal(note.call(app,true).children[0],'Recommended 2160 × 2160');
    }
    for(const resolution of [1728,1944,2160,3072]){
      const app=model(key,resolution);assert.equal(app.computedResolution,`${resolution} × ${resolution}`);
      assert.equal(slider.call(app,true).value,resolution);assert.deepEqual(Array.from(slider.call(app,true).detents),[2160]);
    }
    calls.length=0;const app=model(key,3072);app.onResolutionSliderChange(1944);
    assert.equal(app.m_nResolutionOverride,1944);assert.deepEqual(calls,[['set',key,{resolution_override:1944}]]);
    calls.length=0;await app.resetAppToDefaults();
    assert.deepEqual(calls,[['reset',key],['set',key,{resolution_override:2160}],['fetch',key]]);
  }
  for(const key of ['steam.app.456','some.other.app']){
    const app=model(key);assert.equal(app.usesNightcapResolution,false);assert.equal(app.computedResolution,'1760 × 1760');
    assert.deepEqual(Array.from(slider.call(app,true).detents),[1760]);assert(!note.call(app,true));
    calls.length=0;await app.resetAppToDefaults();assert.deepEqual(calls,[['reset',key],['fetch',key]]);
    app.m_nResolutionOverride=3072;assert.equal(app.computedResolution,'3072 × 3072');assert.equal(slider.call(app,true).value,3072);
  }
  names.set('some.other.app','Nightcap Roblox VR');assert.equal(model('some.other.app').usesNightcapResolution,false);
  const switched=model();context.g.HR.ResetAppSettings=async key=>{calls.push(['reset',key]);switched.currentApp={key:'steam.app.456'};};
  calls.length=0;await switched.resetAppToDefaults();assert.deepEqual(calls,[['reset','steam.app.123'],['set','steam.app.123',{resolution_override:2160}]]);
  context.g.HR.systemInfo.resolution_per_app=false;
  const legacy=model();assert.equal(legacy.computedResolution,'1760 × 1760');assert.deepEqual(Array.from(slider.call(legacy,false).detents),[1]);assert(!note.call(legacy,false));
  console.log('Patched SteamVR fixture: Nightcap Auto/recommended/reset2160, explicit slider values, identity scope and other apps passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
