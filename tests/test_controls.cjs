const vm=require('node:vm'),fs=require('node:fs'),assert=require('node:assert/strict');
class Element {
 constructor(dataset={}) {this.dataset=dataset;this.events={};this.value='100';}
 addEventListener(name, callback) {(this.events[name]??=[]).push(callback);}
 emit(name) {for(const callback of this.events[name]??[])callback({});}
}
(async()=>{
 const forward=new Element({m1:'1',m2:'0'}),reverse=new Element({m1:'0',m2:'-1'}),slider=new Element(),stop=new Element(),error=new Element();
 const bothForward=new Element({m1:'1',m2:'1'}),bothReverse=new Element({m1:'-1',m2:'-1'});
 const document=new Element(),window=new Element(),commands=[],timers=new Set();
 document.querySelectorAll=()=>[forward,reverse,bothForward,bothReverse];document.querySelector=id=>({'#speed':slider,'#stop':stop,'#error':error}[id]);
 let fail=false;
 const context={document,window,AbortSignal:{timeout:()=>({})},setInterval:fn=>{timers.add(fn);return fn},clearInterval:fn=>timers.delete(fn),fetch:async(url,options)=>{commands.push(JSON.parse(options.body));if(fail)throw Error('offline');return {ok:true}}};
 vm.createContext(context);vm.runInContext(fs.readFileSync('controls.js','utf8'),context);
 const flush=()=>new Promise(resolve=>setImmediate(resolve));const last=()=>commands.at(-1);
 forward.emit('click');await flush();assert.deepEqual(last(),{m1:255,m2:0});
 forward.emit('pointerup');forward.emit('blur');window.emit('blur');await flush();assert.equal(timers.size,1);
 for(let i=0;i<20;i++){for(const fn of timers)fn();await flush();assert.equal(last().m1,255);}
 slider.value='50';for(const fn of timers)fn();await flush();assert.equal(last().m1,128);
 reverse.emit('click');await flush();assert.deepEqual(last(),{m1:0,m2:-128});assert.equal(timers.size,1);
 bothForward.emit('click');await flush();assert.deepEqual(last(),{m1:128,m2:128});
 for(let i=0;i<10;i++){for(const fn of timers)fn();await flush();assert.deepEqual(last(),{m1:128,m2:128});}
 slider.value='100';for(const fn of timers)fn();await flush();assert.deepEqual(last(),{m1:255,m2:255});
 bothReverse.emit('click');await flush();assert.deepEqual(last(),{m1:-255,m2:-255});
 stop.emit('click');await flush();assert.deepEqual(last(),{m1:0,m2:0});assert.equal(timers.size,0);
 for(const reason of ['pagehide','offline']){forward.emit('click');await flush();window.emit(reason);await flush();assert.equal(last().m1,0);assert.equal(timers.size,0);}
 forward.emit('click');await flush();document.hidden=true;document.emit('visibilitychange');await flush();assert.equal(last().m1,0);
 forward.emit('click');await flush();context.connectionLost();await flush();assert.equal(last().m1,0);assert.equal(timers.size,0);
 fail=true;forward.emit('click');await flush();assert.equal(timers.size,0);assert.equal(last().m1,0);
 fail=false;await flush();assert.equal(timers.size,0);assert.equal(last().m1,0);
 console.log('Click-to-run, renewal, slider, direction, stop, page/connection loss and no automatic restart checks passed');
})().catch(error=>{console.error(error);process.exitCode=1});
