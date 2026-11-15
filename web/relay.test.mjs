import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import {once,EventEmitter} from 'node:events';
import {PassThrough} from 'node:stream';
import {createBridge} from './bridge.mjs';
import {createCameraRelay,rtspArguments} from './camera-relay.mjs';
const waitFor=async predicate=>{const start=Date.now();while(!predicate()){if(Date.now()-start>2500)throw Error('condition timed out');await new Promise(r=>setTimeout(r,10));}};
function fakeReceiver(){
 let total=0,active=0;
 const children=[];
 function spawn(){
  total++;active++;
  const child=new EventEmitter();child.stdout=new PassThrough();child.stderr=new PassThrough();
  let frame=0,closed=false;
  const timer=setInterval(()=>{
   const jpeg=Buffer.from(`jpeg-${++frame}`);
   const part=Buffer.concat([Buffer.from(`\r\n--ffmpeg\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\n\r\n`),jpeg]);
   child.stdout.write(part.subarray(0,13));child.stdout.write(part.subarray(13));
  },20);
  child.kill=()=>{if(!closed){closed=true;clearInterval(timer);active--;setImmediate(()=>child.emit('close',0));}return true;};
  children.push(child);return child;
 }
 return {spawn,total:()=>total,active:()=>active,children,close:()=>children.forEach(c=>c.kill())};
}

test('RTSP receiver uses UDP, low latency and stream copy without transcoding',()=>{
 const args=rtspArguments({host:'192.168.8.209'});
 assert.equal(args[args.indexOf('-rtsp_transport')+1],'udp');
 assert.equal(args[args.indexOf('-c:v')+1],'copy');
 assert.equal(args[args.indexOf('-flush_packets')+1],'1');
 assert.ok(args.includes('rtsp://192.168.8.209:8554/mjpeg/1'));
});

test('idle launches nothing; viewers share one receiver; last close stops it; new viewer reconnects',async t=>{
 const camera=fakeReceiver();
 const bridge=createBridge({host:'127.0.0.1',spawnProcess:camera.spawn});bridge.listen(0,'127.0.0.1');await once(bridge,'listening');
 const readers=[];
 t.after(()=>{for(const r of readers){r.response.destroy();r.request.destroy();}bridge.closeAllConnections();bridge.close();camera.close();});
 const health=async()=>await(await fetch(`http://127.0.0.1:${bridge.address().port}/health`)).json();
 assert.equal((await health()).video.consumers,0);assert.equal(camera.total(),0);
 async function viewer(){
  const request=http.get(`http://127.0.0.1:${bridge.address().port}/stream`);
  const [response]=await once(request,'response');let data='';
  response.on('data',chunk=>{data+=chunk;});response.on('error',()=>{});
  const reader={request,response,data:()=>data};readers.push(reader);return reader;
 }
 const a=await viewer();await waitFor(()=>a.data().includes('jpeg-'));
 const b=await viewer();await waitFor(()=>b.data().includes('jpeg-'));
 assert.equal(camera.total(),1);assert.equal(camera.active(),1);assert.equal((await health()).video.consumers,2);
 assert.equal((await health()).video.transport,'rtsp/udp');
 a.response.destroy();a.request.destroy();await waitFor(()=>!a.response.readable);
 assert.equal(camera.active(),1);
 b.response.destroy();b.request.destroy();await waitFor(()=>camera.active()===0);
 assert.equal((await health()).video.consumers,0);assert.equal((await health()).video.connected,false);
 const c=await viewer();await waitFor(()=>c.data().includes('jpeg-'));assert.equal(camera.total(),2);
});

test('slow viewer skips intermediate frames and drains only its latest',async t=>{
 const camera=fakeReceiver(),relay=createCameraRelay({host:'127.0.0.1',spawnProcess:camera.spawn});
 const res=new EventEmitter();res.destroyed=false;const sent=[];
 res.writeHead=()=>res;res.flushHeaders=()=>{};res.write=frame=>{sent.push(frame.toString());return false;};
 res.destroy=()=>{res.destroyed=true;res.emit('close');};
 t.after(()=>{relay.close();camera.close();});
 relay.handle({method:'GET'},res);await waitFor(()=>relay.stats().frames>=6);
 assert.equal(sent.length,1);
 const latest=relay.stats().frames;
 res.emit('drain');assert.equal(sent.length,2);assert.match(sent[1],new RegExp(`jpeg-${latest}`));
 res.destroy();await waitFor(()=>camera.active()===0);
});

test('receiver failure closes viewers and exposes an actionable error',async t=>{
 const camera=fakeReceiver(),relay=createCameraRelay({host:'127.0.0.1',spawnProcess:camera.spawn});
 const res=new EventEmitter();res.destroyed=false;
 res.writeHead=()=>res;res.flushHeaders=()=>{};res.write=()=>true;
 res.destroy=()=>{res.destroyed=true;res.emit('close');};
 t.after(()=>{relay.close();camera.close();});
 relay.handle({method:'GET'},res);await waitFor(()=>relay.stats().connected);
 camera.children[0].emit('error',Error('decoder unavailable'));
 assert.ok(res.destroyed);assert.equal(camera.active(),0);
 assert.match(relay.stats().last_error,/decoder unavailable/);
});
