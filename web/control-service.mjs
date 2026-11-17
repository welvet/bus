import {randomUUID} from 'node:crypto';
import {MotionPolicy} from './motion-policy.mjs';
export const SEND_INTERVAL_MS=50;

export function createControlService({getStatus,newSession,send,now=()=>performance.now(),riseMs=0,autoStart=true}) {
 const motion=new MotionPolicy({riseMs});
 let lease=null,sequence=-1,session=0,counter=0,epoch=0,camera=null,statusAt=-Infinity;
 let deadline=0,armed=false,error=null,settingUp=false,polling=false,lastPoll=-Infinity,lastAuto=-Infinity;
 let lightsRequested=null,lightsBusy=false,lightsPending=false,lightsError=null,lightsRevision=0;
 let pending=0,lastAckCounter=0,lastAckAt=-Infinity,stopped=true,closed=false,version=0,leaseAt=0;
 const valid=()=>!!session&&camera?.session===session&&camera.mode==='auto'&&camera.renesas_age_ms>=0&&camera.renesas_age_ms<1000&&now()-statusAt<4000;
 const wire=(op,extra={})=>({op,session,counter:(counter=(counter+1)>>>0),...extra});
 function stop(reason=null) {
   deadline=0;armed=false;motion.reset(now());if(reason)error=reason;
   if(session&&!stopped){stopped=true;const packet=wire('stop');send(packet).catch(()=>{});}
 }
 function disconnect(reason) {
   console.warn(`[bus] ${new Date().toISOString()} control reset: ${reason}`);
   stop(reason);version++;lease=null;session=0;counter=0;epoch=0;
   camera=null;statusAt=-Infinity;lastAckCounter=0;lastAckAt=-Infinity;
   settingUp=false;polling=false;lightsRequested=null;lightsRevision++;lightsPending=false;lightsError=null;
 }
 function output(packet) {
   const token=version;pending++;
   send(packet).then(ack=>{
     if(token!==version)return;
     if(ack.accepted){
       const delta=(packet.counter-lastAckCounter)>>>0;
       if(delta&&delta<0x80000000){lastAckCounter=packet.counter;lastAckAt=now();}
     }else if(!(ack.reason==='old-counter'&&packet.counter!==counter))disconnect(ack.reason||'camera rejected command');
   }).catch(e=>{
     if(token!==version)return;
     const newerAck=(lastAckCounter-packet.counter)>>>0;
     // UDP can lose an older datagram while later commands succeed.
     if(newerAck&&newerAck<0x80000000&&now()-lastAckAt<450)return;
     stop(e.message);
   }).finally(()=>pending--);
 }
 async function setup(token) {
   settingUp=true;
   try {
     const observed=await getStatus();if(token!==version||closed)return;
     if(observed.renesas_age_ms<0||observed.renesas_age_ms>=1000)throw Error('No recent Renesas UART feedback');
     const created=await newSession();if(token!==version||closed)return;
     session=created.session;counter=0;camera={...observed,session};statusAt=now();epoch=observed.epoch;
     const ack=await send(wire('stop'));if(!ack.accepted)throw Error(ack.reason);stopped=true;
     const activate=await send(wire('auto'));if(!activate.accepted)throw Error(activate.reason);
     const confirmed=await getStatus();if(token!==version||closed)return;
     if(confirmed.session!==session||confirmed.mode!=='auto'||confirmed.renesas_age_ms<0||confirmed.renesas_age_ms>=1000)throw Error('Bus not ready after setup');
     camera=confirmed;epoch=confirmed.epoch;statusAt=now();lastPoll=now();error=null;
   }catch(e){if(token===version)disconnect(e.message);}
   finally{if(token===version)settingUp=false;}
 }
 async function refresh() {
   if(polling||settingUp||now()-lastPoll<800)return;
   polling=true;lastPoll=now();const token=version;
   try{
     const observed=await getStatus();if(token!==version||closed)return;
     const wasValid=valid();camera=observed;statusAt=now();
     if(!lease)return;
     if(observed.session!==session||observed.epoch!==epoch) {disconnect('Bus connection changed; slide to take control again');return;}
     if(!valid())disconnect(observed.mode==='manual'?'Xbox has control':'No recent Renesas UART feedback');
     else if(!wasValid){stop();error=null;}
   }catch(e){
     if(token===version){
       // One slow HTTP poll does not invalidate an otherwise healthy UDP link.
       if(lease&&now()-statusAt>=4000)disconnect(e.message);
       else if(!lease){camera=null;statusAt=-Infinity;}
     }
   }
   finally{if(token===version)polling=false;}
 }
 function tick() {
   if(closed||!lease)return;
   // Expiry applies during setup too; an offline client cannot retain ownership.
   if(now()-leaseAt>10000){disconnect('Control lease expired');return;}
   if(settingUp)return;
   if(!session){if(now()-lastPoll>=1000){lastPoll=now();setup(version);}return;}
   refresh();
   if(!valid()){
     if(now()-statusAt>=4000){disconnect('Bus disconnected; slide to take control again');return;}
     stop(error||'Waiting for camera Auto mode');
     if(camera?.session===session&&camera.mode==='manual'&&now()-statusAt<4000&&now()-lastAuto>=1000&&pending<4){lastAuto=now();output(wire('auto'));}
     return;
   }
   if(!armed||now()>=deadline){stop();return;}
   if(pending>=12)return; // Bound sockets, not a movement queue; 450 ms ACKs at 20 Hz fit.
   const value=motion.step(now());stopped=false;
   output(wire('drive',{epoch:camera.epoch,...value,duration_ms:200}));
 }
 function status(){return {connection:camera&&now()-statusAt<4000?'online':'offline',lights:{pending:lightsPending,error:lightsError,supported:camera?.lights_api===1,requested:lightsRequested,on:camera?.lights_age_ms>=0&&camera.lights_age_ms<1000&&now()-statusAt<4000?camera.lights_on:null},lease,ready:!settingUp&&valid(),armed,active:armed&&now()<deadline,error,camera,sequence,...motion.status(),output_speed:Math.trunc(motion.real)||0,send_interval_ms:SEND_INTERVAL_MS,remaining_ms:Math.max(0,Math.round(deadline-now()))};}
 function acquire(){
   stop();version++;polling=false;settingUp=false;lease=randomUUID();sequence=-1;session=0;counter=0;lastAckCounter=0;lastAckAt=-Infinity;camera=null;deadline=0;error=null;leaseAt=now();lastPoll=-Infinity;
   setup(version);return {lease,...status()};
 }
 function input(body) {
   if(!lease||body.lease!==lease)throw Object.assign(Error(`control lease changed${error?': '+error:''}`),{status:409});
   if(!Number.isSafeInteger(body.sequence)||body.sequence<=sequence)throw Object.assign(Error('old-sequence'),{status:409});
   const finite=v=>typeof v==='number'&&Number.isFinite(v)&&v>=-1&&v<=1;
   if(!finite(body.steering)||!finite(body.throttle)||!Number.isInteger(body.duration_ms)||body.duration_ms<1||body.duration_ms>1000)throw Object.assign(Error('steering/throttle must be -1..1; duration_ms 1..1000'),{status:400});
   sequence=body.sequence;leaseAt=now();
   if(settingUp||!valid()){stop(error||'control not ready');return status();}
   if(armed&&now()>=deadline)stop();
   if(!armed&&Math.abs(body.throttle)>.02)throw Object.assign(Error('neutral required'),{status:409});
   const instant=now();
   // If expiry elapsed before the timer ran, discard old acceleration credit.
   if(instant>=deadline)motion.reset(instant);
   motion.set(body.steering,body.throttle,instant);deadline=instant+body.duration_ms;armed=true;error=null;
   // Only update latest intent here. The independent 50 ms pump emits Drive.
   return status();
 }
 async function applyLights(on){
   if(lightsBusy)throw Object.assign(Error('Lights request in progress'),{status:409});
   lightsBusy=true;
   try{
   if(typeof on!=='boolean')throw Object.assign(Error('on must be boolean'),{status:400});
   if(settingUp)throw Object.assign(Error('Control setup is in progress; try lights again'),{status:409});
   const token=version, observed=camera&&now()-statusAt<800?camera:await getStatus();
   if(token!==version)throw Object.assign(Error('Control changed; try lights again'),{status:409});
   if(observed.lights_api!==1)throw Object.assign(Error('Camera and Renesas firmware update required for web lights'),{status:501});
   if(observed.renesas_age_ms<0||observed.renesas_age_ms>=1000)throw Object.assign(Error('No recent Renesas UART feedback'),{status:409});
   if(lease&&observed.session!==session)throw Object.assign(Error('Camera session changed'),{status:409});
   if(!lease){
     const remote=observed.have_counter?observed.counter:0;
     const delta=(remote-counter)>>>0;
     if(session!==observed.session){session=observed.session;counter=remote;}
     else if(delta&&delta<0x80000000)counter=remote;
   }
   camera=observed;statusAt=now();
   const ack=await send(wire('lights',{on}));
   if(!ack.accepted)throw Object.assign(Error(ack.reason||'Lights command rejected'),{status:409});
   if(token!==version)throw Object.assign(Error('Control changed; try lights again'),{status:409});
   lastPoll=-Infinity;
   return status();
   }finally{lightsBusy=false;}
 }
 function setLights(on){
   if(typeof on!=='boolean')throw Object.assign(Error('on must be boolean'),{status:400});
   lightsRequested=on;lightsRevision++;lightsPending=true;lightsError=null;
   deliverLights();return status();
 }
 async function deliverLights(){
   if(lightsBusy||closed||typeof lightsRequested!=='boolean')return;
   const revision=lightsRevision,on=lightsRequested;
   try{await applyLights(on);}
   catch(e){if(revision===lightsRevision)lightsError=e.message;}
   finally{
     if(revision===lightsRevision)lightsPending=false;
     else if(!closed)deliverLights();
   }
 }
 function release(body){
   if(body.lease!==lease)throw Object.assign(Error(`control lease changed${error?': '+error:''}`),{status:409});
   if(!Number.isSafeInteger(body.sequence)||body.sequence<=sequence)throw Object.assign(Error('old-sequence'),{status:409});
   sequence=body.sequence;stop();lease=null;version++;return status();
 }
 const timer=autoStart?setInterval(tick,SEND_INTERVAL_MS):null;timer?.unref();
 return {acquire,input,release,setLights,status,tick,refresh,close(){closed=true;stop();version++;clearInterval(timer);}};
}
