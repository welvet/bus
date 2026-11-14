import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
import {cone,coneInput,conePosition} from './public/cone.js';
function harness(){
 const elements=new Map(),events={},handlers={},timers=[],requests=[],beacons=[],reads=[];
 const element=id=>elements.get(id)||elements.set(id,{checked:false,hidden:false,classList:{toggle(){}},setAttribute(){},getAttribute(){return this.src;},removeAttribute(){delete this.src;},setPointerCapture(){},addEventListener(n,f){handlers[n]=f;},getBoundingClientRect:()=>({left:0,top:0,width:220,height:320})}).get(id);
 const orientation={matches:true,addEventListener(n,f){events.orientation=f;}};
 const state={lights:{on:false,requested:false,supported:true},lease:null,ready:false,armed:false,camera:{mode:'auto'},angle:0,output_speed:0,error:null};let time=10000,delay=false,resolveInput,neutralRequired=false;
 const document={hidden:false,getElementById:element,addEventListener(n,f){events[n]=f;}};
 const context=vm.createContext({cone,coneInput,conePosition,document,matchMedia:()=>orientation,window:{addEventListener(n,f){events[n]=f;}},navigator:{sendBeacon(path,data){beacons.push({path,data});return true;}},Blob,AbortSignal,Date:{now:()=>time},setInterval(fn){timers.push(fn);},fetch:async(path,options={})=>{
  if(!options.body)reads.push(path);
  const body=options.body?JSON.parse(options.body):null;if(body)requests.push({path,...body});
  if(path==='/api/control/lights'){state.lights={on:body.on,requested:body.on,supported:true};}
  if(path==='/api/control/acquire'){state.lease='lease-test';state.ready=true;state.armed=false;}
  if(path==='/api/control/input'){
   if(neutralRequired && body.throttle!==0){neutralRequired=false;state.armed=false;return {ok:false,json:async()=>({error:'neutral required'})};}
   if(delay){delay=false;return await new Promise(resolve=>{resolveInput=()=>resolve({ok:false,json:async()=>({error:'old-sequence'})});});}
   state.armed=true;state.output_speed=body.throttle?50:0;state.angle=body.steering?20:0;
  }
  return {ok:true,json:async()=>({...state})};
 }});
 vm.runInContext(readFileSync(new URL('./public/app.js',import.meta.url),'utf8').replace(/^import .*;\n/gm,''),context);
 const flush=()=>new Promise(r=>setImmediate(r));
 const cycle=()=>timers[0]();
 const settle=async()=>{for(let i=0;i<6;i++){await flush();await cycle();}};
 const down=(id=1)=>handlers.pointerdown({pointerId:id,clientX:110,clientY:260,preventDefault(){}});
 const move=(steering,throttle,id=1)=>{const p=conePosition(steering,throttle);handlers.pointermove({pointerId:id,clientX:p.x,clientY:p.y});};
 return {element,orientation,events,handlers,requests,beacons,reads,state,flush,cycle,settle,down,move,advance(){time+=1100;},delay(){delay=true;},finish(){resolveInput();},requireNeutral(){neutralRequired=true;}};
}
test('slide acquires control, sends neutral first, then raw intent; release and switch use clean API',async()=>{
 const h=harness();await h.flush();h.down();assert.equal(h.element('control').checked,false);
 h.move(.56,.5);assert.equal(h.element('control').checked,true);await h.settle();
 const inputs=h.requests.filter(r=>r.path==='/api/control/input');assert.equal(inputs[0].throttle,0);
 const latest=inputs.at(-1);assert.ok(Math.abs(latest.throttle-.5)<1e-9);assert.ok(Math.abs(latest.steering-.56)<1e-9);
 assert.equal(latest.speed,undefined);assert.equal(latest.angle,undefined);assert.equal(latest.session,undefined);
 assert.equal(h.element('speedValue').value,'50%','display uses backend output');
 h.handlers.pointerup({pointerId:1});const neutral=JSON.parse(await h.beacons.at(-1).data.text());assert.equal(neutral.throttle,0);assert.equal(neutral.steering,0);
 h.element('control').checked=false;h.element('control').onchange();assert.equal(h.beacons.at(-1).path,'/api/control/release');
});
test('portrait and hide release control, and no old gesture is replayed',async()=>{
 const h=harness();await h.flush();h.down();h.move(1,1);await h.settle();
 h.orientation.matches=false;h.events.orientation();await h.flush();assert.equal(h.beacons.at(-1).path,'/api/control/release');
 const count=h.requests.filter(r=>r.path==='/api/control/input').length;h.move(1,1);await h.cycle();
 assert.equal(h.requests.filter(r=>r.path==='/api/control/input').length,count);
 h.orientation.matches=true;h.events.orientation();await h.settle();assert.equal(h.requests.at(-1).throttle,0);
});
test('release while acquisition pending stays neutral; reverse changes only raw throttle sign',async()=>{
 const h=harness();await h.flush();h.down();h.move(1,1);h.handlers.pointerup({pointerId:1});await h.settle();
 assert.ok(h.requests.filter(r=>r.path==='/api/control/input').every(r=>r.throttle===0));
 h.element('reverse').checked=true;h.element('reverse').onchange();h.down();h.move(0,1);await h.cycle();assert.equal(h.requests.at(-1).throttle,-1);
});
test('newer neutral supersedes old in-flight input without losing control',async()=>{
 const h=harness();await h.flush();h.down();h.move(0,1);await h.settle();
 h.delay();const pending=h.cycle();await h.flush();h.handlers.pointerup({pointerId:1});h.finish();await pending;
 assert.equal(h.element('control').checked,true);assert.equal(h.element('error').hidden,true);
});

test('steady driving sends one input round trip per cycle, without redundant status reads',async()=>{
 const h=harness();await h.flush();h.down();h.move(0,1);await h.settle();
 const beforeReads=h.reads.length,beforeInputs=h.requests.length;
 for(let i=0;i<10;i++)await h.cycle();
 assert.equal(h.reads.length,beforeReads);
 assert.equal(h.requests.length,beforeInputs+10);
 assert.ok(h.requests.slice(beforeInputs).every(r=>r.path==='/api/control/input'&&r.duration_ms===300));
});

test('backend neutral interlock is satisfied automatically before a held gesture resumes',async()=>{
 const h=harness();await h.flush();h.down();h.move(0,1);await h.settle();
 h.requireNeutral();const before=h.requests.length;await h.cycle();
 const recovery=h.requests.slice(before);
 assert.equal(recovery[0].throttle,1);assert.equal(recovery[1].throttle,0);assert.equal(recovery[1].steering,0);
 assert.ok(recovery[1].sequence>recovery[0].sequence);assert.equal(h.element('error').hidden,true);
 assert.equal(h.element('control').checked,true);
 await h.cycle();assert.equal(h.requests.at(-1).throttle,1);
});

test('headlight switch sends only an absolute light setting, without taking motion control',async()=>{
 const h=harness();await h.flush();h.element('lights').checked=true;await h.element('lights').onchange();
 assert.equal(h.requests.at(-1).path,'/api/control/lights');assert.equal(h.requests.at(-1).on,true);
 assert.ok(!h.requests.some(r=>r.path==='/api/control/acquire'||r.path==='/api/control/input'));
});
