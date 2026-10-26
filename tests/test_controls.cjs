const vm=require('node:vm'),fs=require('node:fs'),assert=require('node:assert/strict');
class Element {
 constructor(dataset={}) {this.dataset=dataset;this.events={};this.value='100';}
 addEventListener(name, callback) {(this.events[name]??=[]).push(callback);}
 setPointerCapture() {}
 emit(name, event={}) {for(const callback of this.events[name]??[])callback({preventDefault(){},...event});}
}
(async()=>{
 const forward=new Element({m1:'1',m2:'0'}),reverse=new Element({m1:'0',m2:'-1'}),slider=new Element(),stop=new Element(),error=new Element();
 const document=new Element(),window=new Element(),commands=[],timers=new Set();
 document.querySelectorAll=()=>[forward,reverse];document.querySelector=id=>({'#speed':slider,'#stop':stop,'#error':error}[id]);
 let fail=false;
 const context={document,window,AbortSignal:{timeout:()=>({})},setInterval:fn=>{timers.add(fn);return fn},clearInterval:fn=>timers.delete(fn),fetch:async(url,options)=>{commands.push(JSON.parse(options.body));if(fail)throw Error('offline');return {ok:true}}};
 vm.createContext(context);vm.runInContext(fs.readFileSync('controls.js','utf8'),context);
 const flush=()=>new Promise(resolve=>setImmediate(resolve));const last=()=>commands.at(-1);
 forward.emit('pointerdown',{button:0,pointerId:1});await flush();assert.deepEqual(last(),{m1:255,m2:0});
 for(let i=0;i<20;i++){for(const fn of timers)fn();await flush();assert.equal(last().m1,255);}
 slider.value='50';for(const fn of timers)fn();await flush();assert.equal(last().m1,128);
 forward.emit('pointerup',{pointerId:1});await flush();assert.deepEqual(last(),{m1:0,m2:0});assert.equal(timers.size,0);
 for(const reason of ['pointercancel','lostpointercapture']){reverse.emit('pointerdown',{button:0,pointerId:2});await flush();assert.equal(last().m2,-128);reverse.emit(reason,{pointerId:2});await flush();assert.equal(last().m2,0);}
 forward.emit('keydown',{key:'Enter',repeat:false});await flush();assert.equal(last().m1,128);forward.emit('keyup',{key:'Enter'});await flush();assert.equal(last().m1,0);
 for(const reason of ['blur','pagehide']){forward.emit('pointerdown',{button:0,pointerId:3});await flush();window.emit(reason);await flush();assert.equal(last().m1,0);}
 forward.emit('pointerdown',{button:0,pointerId:4});await flush();document.hidden=true;document.emit('visibilitychange');await flush();assert.equal(last().m1,0);
 forward.emit('pointerdown',{button:0,pointerId:5});await flush();stop.emit('click');await flush();assert.equal(last().m1,0);
 fail=true;forward.emit('pointerdown',{button:0,pointerId:6});await flush();assert.equal(timers.size,0);assert.equal(last().m1,0);
 console.log('Hold, renewal, slider, release, cancel, keyboard, blur, hidden page, stop and failure checks passed');
})().catch(error=>{console.error(error);process.exitCode=1});
