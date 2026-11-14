import test from 'node:test';
import assert from 'node:assert/strict';
import {createControlService} from './control-service.mjs';
const flush=()=>new Promise(r=>setImmediate(r));
async function setup(){
 let time=1000;const packets=[],camera={session:7,epoch:1,mode:'auto',renesas_age_ms:10};
 const service=createControlService({now:()=>time,autoStart:false,getStatus:async()=>({...camera}),newSession:async()=>({session:7}),send:async packet=>{packets.push(packet);return {accepted:true,counter:packet.counter};}});
 const acquired=service.acquire();await flush();let sequence=0;
 const input=(throttle,steering=0)=>service.input({lease:acquired.lease,sequence:++sequence,throttle,steering,duration_ms:300});
 return {service,camera,packets,acquired,input,async tick(ms=50){time+=ms;service.tick();await flush();},now:()=>time};
}
test('backend lease requires neutral and changes desired while ramping, expiry stops and resets',async t=>{
 const h=await setup();t.after(()=>h.service.close());
 assert.ok(h.packets.every(p=>p.op!=='drive'),'acquire must not attach servo');
 assert.throws(()=>h.input(1),/neutral required/);
 h.input(0);h.input(1);
 for(let i=0;i<10;i++){h.input(1);await h.tick();}
 assert.equal(h.service.status().output_speed,50);
 h.input(.03);assert.equal(h.service.status().output_speed,30);
 h.input(1);await h.tick();assert.equal(h.service.status().output_speed,35);
 await h.tick(301);assert.equal(h.service.status().armed,false);assert.equal(h.service.status().allowed_speed,0);assert.equal(h.packets.at(-1).op,'stop');
 assert.throws(()=>h.input(1),/neutral required/);h.input(0);h.input(1);await h.tick();assert.equal(h.service.status().output_speed,5);
});
test('release, manual takeover and stale status discard motion rather than replaying it',async t=>{
 const h=await setup();t.after(()=>h.service.close());h.input(0);h.input(1);await h.tick();
 h.camera.mode='manual';await h.tick(900);await h.service.refresh();await flush();
 assert.equal(h.service.status().armed,false);assert.equal(h.service.status().real_speed,0);
 h.camera.mode='auto';await h.tick(900);await h.service.refresh();await flush();
 assert.equal(h.service.status().real_speed,0);
 h.service.release({lease:h.acquired.lease,sequence:100});assert.equal(h.service.status().lease,null);
 assert.throws(()=>h.input(0),/lease changed/);
});
test('invalid normalized intent cannot issue actuator commands',async t=>{
 const h=await setup();t.after(()=>h.service.close());const count=h.packets.length;
 assert.throws(()=>h.service.input({lease:h.acquired.lease,sequence:1,throttle:100,steering:0,duration_ms:300}),/must be/);
 assert.equal(h.packets.length,count);
});

test('expired input cannot rearm with movement even if the timer has not run',async t=>{
 let time=1000;const s=createControlService({now:()=>time,autoStart:false,getStatus:async()=>({session:9,epoch:1,mode:'auto',renesas_age_ms:1}),newSession:async()=>({session:9}),send:async p=>({accepted:true,counter:p.counter})});
 t.after(()=>s.close());const {lease}=s.acquire();await flush();
 s.input({lease,sequence:1,steering:0,throttle:0,duration_ms:100});
 time+=101;
 assert.throws(()=>s.input({lease,sequence:2,steering:0,throttle:1,duration_ms:100}),/neutral required/);
 assert.equal(s.status().allowed_speed,0);
});

test('fast input only replaces intent; each independent pump tick sends once without waiting for ACK',async t=>{
 let time=1000;const packets=[],acks=[];
 const service=createControlService({now:()=>time,autoStart:false,getStatus:async()=>({session:9,epoch:1,mode:'auto',renesas_age_ms:1}),newSession:async()=>({session:9}),send:p=>{
  packets.push(p);if(p.op!=='drive')return Promise.resolve({accepted:true,counter:p.counter});
  return new Promise(resolve=>acks.push(()=>resolve({accepted:true,counter:p.counter})));
 }});t.after(()=>{for(const ack of acks)ack();service.close();});
 const {lease}=service.acquire();await flush();let sequence=0;
 service.input({lease,sequence:++sequence,steering:0,throttle:0,duration_ms:300});
 const before=packets.length;
 for(let i=0;i<10;i++)service.input({lease,sequence:++sequence,steering:i/10,throttle:.5,duration_ms:300});
 assert.equal(packets.length,before,'input requests must not emit Drive');
 for(let i=0;i<5;i++){time+=50;service.tick();}
 assert.equal(packets.length,before+5,'50 ms pump must not wait for four pending acknowledgments');
 assert.equal(packets.at(-1).angle,-36,'only latest steering is transmitted');
 time+=51;service.tick();assert.equal(packets.at(-1).op,'stop','expired latest intent must stop instead of replay');
});

test('lost older UDP acknowledgment cannot reset acceleration after a newer command succeeds',async t=>{
 let time=1000;const pending=[];
 const service=createControlService({now:()=>time,autoStart:false,getStatus:async()=>({session:9,epoch:1,mode:'auto',renesas_age_ms:1}),newSession:async()=>({session:9}),send:p=>p.op==='drive'?new Promise((resolve,reject)=>pending.push({p,resolve,reject})):Promise.resolve({accepted:true,counter:p.counter})});
 t.after(()=>{for(const p of pending)p.resolve({accepted:true,counter:p.p.counter});service.close();});
 const {lease}=service.acquire();await flush();
 service.input({lease,sequence:1,steering:0,throttle:0,duration_ms:300});service.input({lease,sequence:2,steering:0,throttle:1,duration_ms:300});
 time+=50;service.tick();time+=50;service.tick();
 pending[1].resolve({accepted:true,counter:pending[1].p.counter});await flush();
 pending[0].reject(Error('lost older ack'));await flush();
 assert.equal(service.status().armed,true);assert.equal(service.status().output_speed,10);assert.equal(service.status().error,null);
});

test('headlights work without acquiring motion, and old firmware is rejected',async t=>{
 const packets=[];let supported=false;
 const service=createControlService({autoStart:false,getStatus:async()=>({session:7,counter:10,have_counter:true,epoch:1,mode:'manual',renesas_age_ms:1,lights_api:supported?1:undefined,lights_on:false,lights_age_ms:10}),newSession:async()=>{throw Error('lights must not rotate session');},send:async p=>{packets.push(p);return {accepted:true,counter:p.counter};}});
 t.after(()=>service.close());
 await assert.rejects(service.setLights(true),/firmware update required/);assert.equal(packets.length,0);
 supported=true;const result=await service.setLights(true);
 assert.equal(result.lease,null);assert.equal(result.lights.requested,true);
 assert.deepEqual(packets,[{op:'lights',session:7,counter:11,on:true}]);
 assert.equal(service.status().real_speed,0);assert.equal(service.status().armed,false);
});
