const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'../tools/steam_frame/steamvr-simple-video-controls.js'),'utf8');
function harness(saved={}){
  let state,refIndex=0,first=true,now=100000;
  const refs=[],effects=[],writes=[],reads=[],timers=[];
  const context={document:{getElementById:()=>({})},Date:{now:()=>now},setInterval:callback=>{timers.push(callback);return 1;},clearInterval:()=>{},
    s:{Fragment:'fragment',createElement:(type,props,...children)=>({type,props,children}),
      useEffect:effect=>{if(first)effects.push(effect);},
      useState:initial=>{if(!state)state={...initial};return[state,update=>{state=typeof update==='function'?update(state):update;}];},
      useRef:initial=>{const index=refIndex++;return refs[index]||(refs[index]={current:initial});}},
    v:{xS:'select',tE:'slider',Al:'toggle'},g:{HR:{
      GetSettingsValue:setting=>{reads.push(setting);return Promise.resolve(saved[setting.split('/').at(-1)]);},
      SetSettingsStringValueWithoutSchema:(setting,value)=>writes.push([setting,value])}}};
  vm.createContext(context);vm.runInContext(source,context);
  return{writes,reads,get state(){return state;},render(){refIndex=0;const rows=context.NightcapVideoControls({}).children.filter(Boolean);first=false;return rows;},
    async load(){for(const effect of effects)effect();await Promise.resolve();await Promise.resolve();},
    async poll(){for(const timer of timers)timer();await Promise.resolve();await Promise.resolve();},advance:ms=>{now+=ms;}};
}
const row=(rows,label)=>{const result=rows.find(value=>value.props.label===label);assert(result,`Missing ${label}`);return result;};
(async()=>{
  const defaults=harness();let rows=defaults.render();
  assert.deepEqual(rows.map(value=>value.props.label),['Foveated Rendering','Gaze sharp radius','Performance Overlay','Anti Aliasing']);
  assert.equal(row(rows,'Foveated Rendering').props.value,3);assert.equal(row(rows,'Anti Aliasing').props.value,3);
  assert.deepEqual(Array.from(row(rows,'Anti Aliasing').props.items,item=>[item.value,item.sLabel]),[[0,'Off'],[1,'FXAA (gaze)'],[2,'SMAA Low (gaze)'],[3,'SMAA High (gaze)']]);
  assert(rows.every(value=>!('title'in value.props)&&!('description'in value.props)&&!('help'in value.props)));
  assert(!/FSR|MSAA|upscal|relaunch|sharpen/i.test(source));
  for(const aaMode of [0,1,2,3]){
    const saved=harness({aaMode,gazeRadiusPercent:8,performanceOverlay:1,foveationLevel:3,upscaleMode:3,fsrQuality:5,msaaSamples:4});
    saved.render();await saved.load();rows=saved.render();
    assert.equal(row(rows,'Anti Aliasing').props.value,aaMode);assert.equal(saved.state.gazeRadiusPercent,8);assert.equal(row(rows,'Performance Overlay').props.value,true);
    assert.deepEqual(Object.keys(saved.state).sort(),['aaMode','foveationLevel','gazeRadiusPercent','performanceOverlay']);
    assert.equal(saved.writes.length,0,'Loading saved choices must not write defaults');
    assert(saved.reads.every(setting=>!/(FSR|MSAA|upscale)/i.test(setting)));
    row(rows,'Anti Aliasing').props.onChange(3);assert.deepEqual(saved.writes.at(-1),['/settings/nightcap/aaMode',3]);
  }
  for(const aaMode of [-1,4,5,6,7,8,NaN]){
    const invalid=harness({aaMode});invalid.render();await invalid.load();rows=invalid.render();
    assert.equal(row(rows,'Anti Aliasing').props.value,3);row(rows,'Anti Aliasing').props.onChange(aaMode);assert.equal(invalid.writes.length,0);
  }
  const gaze=harness({gazeRadiusPercent:8});gaze.render();await gaze.load();rows=gaze.render();
  const slider=row(rows,'Gaze sharp radius');slider.props.onInteractionStart();slider.props.onChange(20);
  gaze.advance(2000);await gaze.poll();assert.equal(gaze.state.gazeRadiusPercent,20,'Polling must not move an actively dragged slider');
  slider.props.onInteractionEnd();await gaze.poll();assert.equal(gaze.state.gazeRadiusPercent,8);
  const count=gaze.writes.length;for(const value of [7,36,NaN])slider.props.onChange(value);assert.equal(gaze.writes.length,count);
  row(rows,'Performance Overlay').props.onChange(true);assert.deepEqual(gaze.writes.at(-1),['/settings/nightcap/performanceOverlay',1]);
  console.log('Native video controls: SMAA High/Performance defaults, four AA choices, saved gaze8, removed legacy controls and safe slider polling passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
