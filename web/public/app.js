import { cone, coneInput, conePosition } from './cone.js';
const $=id=>document.getElementById(id);
const landscape=matchMedia('(orientation: landscape)');
const keys=new Set();
let lightsBusy=false;
let wanted=false,ready=false,suspended=false,touching=false;
let lease=null,sequence=0,generation=0,busy=false,state=null,retryAt=0,nextReadAt=0;
let pointer=null,lastPointer=null,pressPoint=null,sliding=false,reverse=false,x=0,y=0;
const active=()=>wanted&&landscape.matches&&!document.hidden&&!suspended;
const message=text=>{$('error').textContent=text;$('error').hidden=!text;};
async function json(path,options={}) {
 try{
  const res=await fetch(path,{cache:'no-store',signal:AbortSignal.timeout(path==='/api/control/lights'?10000:3000),...options});
  const data=await res.json();if(!res.ok)throw Error(data.error||data.reason||`HTTP ${res.status}`);return data;
 }catch(e){if(e.name==='TimeoutError'||e.name==='AbortError')throw Error('Mac control API timed out');throw e;}
}
const post=(path,data)=>json(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
function paint(){
 if(!lightsBusy){
  const light=state?.lights;
  if(typeof light?.on==='boolean')$('lights').checked=light.on;
  else if(typeof light?.requested==='boolean')$('lights').checked=light.requested;
 }
 $('lights').disabled=lightsBusy;
 $('speedValue').value=`${state?.output_speed||0}%`;$('angleValue').value=`${state?.angle||0}°`;
 $('joystick').classList.toggle('ready',ready&&active());
 const p=conePosition(x,Math.abs(y));$('thumb').setAttribute('cx',p.x);$('thumb').setAttribute('cy',p.y);
 $('reverse').disabled=touching||keys.size>0;
 $('driveState').textContent=!wanted?'Slide to take control':!active()?'Control paused':ready?'You have control':'Connecting control…';
}
function resetInput(){x=y=0;touching=false;keys.clear();pointer=null;lastPointer=null;pressPoint=null;sliding=false;paint();}
function immediate(neutral){
 if(!lease)return;
 const data=neutral?{lease,sequence:++sequence,steering:0,throttle:0,duration_ms:300}:{lease,sequence:++sequence};
 navigator.sendBeacon(neutral?'/api/control/input':'/api/control/release',new Blob([JSON.stringify(data)],{type:'application/json'}));
 if(!neutral)lease=null;
}
function pause(){generation++;resetInput();immediate(false);ready=false;paint();}
function fault(error){resetInput();ready=false;retryAt=Date.now()+1000;message(error.message);paint();}
async function cycle(){
 if(busy||document.hidden||Date.now()<retryAt)return;
 busy=true;const token=generation;
 try{
  if(active()&&!lease){
   const acquired=await post('/api/control/acquire',{});
   if(token!==generation){navigator.sendBeacon('/api/control/release',new Blob([JSON.stringify({lease:acquired.lease,sequence:1})],{type:'application/json'}));return;}
   lease=acquired.lease;sequence=0;state=acquired;
  }else if(!active() || !state?.ready || state.lease!==lease){
   if(Date.now()<nextReadAt)return;nextReadAt=Date.now()+250;
   // Active input responses already contain current backend state. Avoid a
   // second round trip, which can consume the entire input validity window.
   const observed=await json('/api/control/status');if(token!==generation)return;state=observed;
  }
  const camera=state.camera;
  $('status').textContent=camera?(state.ready?'Bus online · Auto':camera.mode==='manual'?'Bus online · Xbox':'Bus waiting'):'Connecting to bus…';
  if(!active()){ready=false;paint();return;}
  if(state.lease!==lease)throw Error('Control lease changed. Toggle control to take over.');
  ready=state.ready;
  if(!ready){message(state.error||'Waiting for bus control…');paint();return;}
  // Backend requires neutral after acquisition, expiry, error or manual takeover.
  const neutral=!state.armed;
  const data={lease,sequence:++sequence,steering:neutral?0:x,throttle:neutral?0:y,duration_ms:300};
  let result, responseSequence=data.sequence;
  try {result=await post('/api/control/input',data);}
  catch(error){
   if(token!==generation)return;
   // A previously armed response may be out of date after an input expiry.
   if(data.sequence!==sequence && ['neutral required','old-sequence'].includes(error.message))return;
   if(error.message!=='neutral required')throw error;
   responseSequence=++sequence;
   result=await post('/api/control/input',{lease,sequence:responseSequence,steering:0,throttle:0,duration_ms:300});
  }
  if(token!==generation)return;
  if(responseSequence===sequence)state=result;
  if(state.error)message(state.error);else message('');
  if(lastPointer&&touching)movePointer(lastPointer);
  paint();
 }catch(e){if(token===generation){if(e.message!=='old-sequence')fault(e);}}
 finally{busy=false;}
}
function movePointer(event) {
  if (event.pointerId !== pointer || !touching || !landscape.matches || document.hidden || suspended) return;
  if (!sliding) {
    if (!pressPoint || Math.hypot(event.clientX-pressPoint.x,event.clientY-pressPoint.y)<4) return;
    sliding=true;
  }
  if (!wanted) {
    const held={pointerId:event.pointerId,clientX:event.clientX,clientY:event.clientY};
    $('control').checked=true; $('control').onchange();
    pointer=held.pointerId; touching=true; sliding=true;
  }
  lastPointer={pointerId:event.pointerId,clientX:event.clientX,clientY:event.clientY};
  if (!ready) return;
  const rect=$('joystick').getBoundingClientRect();
  const scale=Math.min(rect.width/cone.width,rect.height/cone.height);
  const px=(event.clientX-rect.left-(rect.width-cone.width*scale)/2)/scale;
  const py=(event.clientY-rect.top-(rect.height-cone.height*scale)/2)/scale;
  const input=coneInput(px,py);
  // Send raw intent; backend owns steering orientation, curves and motor limits.
  x=input.steering; y=input.throttle*(reverse?-1:1); paint();
}
$('joystick').addEventListener('pointerdown',event=>{
  if (!landscape.matches || document.hidden || suspended || pointer !== null) return;
  event.preventDefault();
  pointer=event.pointerId; touching=true; sliding=false; keys.clear();
  pressPoint={x:event.clientX,y:event.clientY};
  $('joystick').setPointerCapture(pointer); paint();
});
$('joystick').addEventListener('pointermove',movePointer);
for(const name of ['pointerup','pointercancel','lostpointercapture']) {
  $('joystick').addEventListener(name,event=>{
    if(event.pointerId !== pointer) return;
    resetInput(); if(ready && active()) immediate(true);
  });
}
$('joystick').oncontextmenu=event=>event.preventDefault();
$('reverse').onchange=()=>{
  reverse=$('reverse').checked; resetInput();
  if(ready && active()) immediate(true);
};
$('lights').onchange=async()=>{
 const on=$('lights').checked;lightsBusy=true;paint();
 try{
  const result=await post('/api/control/lights',{on});
  if(state){state.lights={...result.lights,on:null};state.camera=result.camera;}
  else state=result;
  message('');
 }catch(error){message(error.message);$('lights').checked=!on;}
 finally{lightsBusy=false;paint();}
};
$('control').onchange=()=>{
 generation++;wanted=$('control').checked;
 if(!wanted){pause();state=null;message('');}
 else{ready=false;resetInput();retryAt=0;cycle();}
 paint();
};
const arrows = new Set(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight']);
function keyboard() {
  x = Number(keys.has('ArrowRight'))-Number(keys.has('ArrowLeft'));
  y = (keys.has('ArrowUp') || keys.has('ArrowDown') ? 1 : 0) * (reverse ? -1 : 1);
  paint();
  if (!keys.size && ready && active()) immediate(true);
}
window.addEventListener('keydown',event=>{
  if (!arrows.has(event.key) || !ready || !active() || touching) return;
  event.preventDefault();keys.add(event.key);keyboard();
});
window.addEventListener('keyup',event=>{if(arrows.has(event.key)){event.preventDefault();keys.delete(event.key);keyboard();}});
window.addEventListener('blur',()=>{suspended=true;pause();});
window.addEventListener('focus',()=>{suspended=false;cycle();});
window.addEventListener('pagehide',()=>{pause();$('camera').removeAttribute('src');});
window.addEventListener('pageshow',()=>{updateVideo();});
document.addEventListener('visibilitychange',()=>{updateVideo();if(document.hidden) pause();else{cycle();}});
landscape.addEventListener('change',()=>{pause();paint();updateVideo();cycle();});
window.addEventListener('resize',()=>{resetInput();if(ready) immediate(true);paint();});
$('camera').onload=()=>{$('videoHint').textContent='';};
$('camera').onerror=()=>{$('videoHint').textContent='Camera unavailable';};
function updateVideo(reconnect=false) {
  const image=$('camera');
  if(document.hidden || !landscape.matches) {
    image.removeAttribute('src'); $('videoHint').textContent='Camera paused';
  } else if(reconnect || !image.getAttribute('src')) {
    $('videoHint').textContent='Connecting to camera…';
    image.src=`/camera/stream?t=${Date.now()}`;
  }
}
$('reloadVideo').onclick=()=>updateVideo(true);
updateVideo();
paint(); setInterval(cycle,10); cycle();
