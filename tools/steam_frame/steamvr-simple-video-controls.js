// This module's React alias is s. Custom settings have no Valve schema, so
// use the existing schema-free native settings methods and controlled inputs.
function NightcapResolutionApp(appKey,appName){
  return appKey==='io.github.coderdayton.nightcap.vr'||appKey==='io.github.CoderDayton.nightcap.vr'||
    (typeof appKey==='string'&&/^steam\.app\.\d+$/.test(appKey)&&appName==='Nightcap Roblox VR');
}
function NightcapVideoControls(props){
  s.useEffect(()=>{
    if(document.getElementById('nightcap-video-layout'))return;
    const style=document.createElement('style');style.id='nightcap-video-layout';
    style.textContent='.SettingsMain .Modal.AppVideoModal>.Subsection.Body{min-height:0}.SettingsMain .Modal.AppVideoModal>.Subsection.Body>.SettingsItem{flex-shrink:0}.SettingsMain .Modal.AppVideoModal>.Subsection.Top,.SettingsMain .Modal.AppVideoModal>.Subsection.Bottom{flex-shrink:0}';
    document.head.appendChild(style);
  },[]);
  const initial={foveationLevel:3,aaMode:3,gazeRadiusPercent:15,performanceOverlay:0};
  const[values,setValues]=s.useState(initial);
  const dragging=s.useRef(false);
  const pending=s.useRef({});
  const choices={foveationLevel:[0,1,2,3],aaMode:[0,1,2,3],performanceOverlay:[0,1]};
  const valid=(key,n)=>key==='gazeRadiusPercent'?Number.isFinite(n)&&n>=8&&n<=35:choices[key].includes(n);
  s.useEffect(()=>{
    let live=true;
    const load=()=>{for(const key of Object.keys(initial))g.HR.GetSettingsValue('/settings/nightcap/'+key).then(value=>{
      if(value===null||value===undefined||value==='')return;
      const n=Number(value);if(live&&valid(key,n)&&!(dragging.current&&key==='gazeRadiusPercent')&&!(Date.now()<(pending.current[key]||0)))setValues(old=>old[key]===n?old:({...old,[key]:n}));
    }).catch(()=>{});};
    load();const timer=setInterval(load,1000);
    return()=>{live=false;clearInterval(timer);};
  },[]);
  const change=(key,value)=>{
    const n=Number(value);if(!valid(key,n))return;
    pending.current[key]=Date.now()+1500;
    g.HR.SetSettingsStringValueWithoutSchema('/settings/nightcap/'+key,n);
    setValues(old=>old[key]===n?old:({...old,[key]:n}));
  };
  // Keep native rows as direct children: the page prevents those rows from
  // shrinking. An extra div can collapse while its controls cover later rows.
  return s.createElement(s.Fragment,null,
    s.createElement(v.xS,{label:'Foveated Rendering',value:values.foveationLevel,onChange:value=>change('foveationLevel',value),items:[{value:0,sLabel:'Off'},{value:1,sLabel:'Quality'},{value:2,sLabel:'Balanced'},{value:3,sLabel:'Performance'}]}),
    s.createElement(v.tE,{label:'Gaze sharp radius',min:8,max:35,step:1,value:values.gazeRadiusPercent,renderValue:value=>Math.round(value)+'%',onChange:value=>change('gazeRadiusPercent',Math.round(value)),onInteractionStart:()=>{dragging.current=true;},onInteractionEnd:()=>{dragging.current=false;}}),
    s.createElement(v.Al,{label:'Performance Overlay',value:values.performanceOverlay===1,onChange:value=>change('performanceOverlay',value?1:0)}),
    s.createElement(v.xS,{label:'Anti Aliasing',value:values.aaMode,onChange:value=>change('aaMode',value),items:[{value:0,sLabel:'Off'},{value:1,sLabel:'FXAA (gaze)'},{value:2,sLabel:'SMAA Low (gaze)'},{value:3,sLabel:'SMAA High (gaze)'}]}));
}
