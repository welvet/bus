import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import {once,EventEmitter} from 'node:events';
import {createBridge} from './bridge.mjs';
import {createCameraRelay} from './camera-relay.mjs';
const waitFor=async predicate=>{const start=Date.now();while(!predicate()){if(Date.now()-start>2500)throw Error('condition timed out');await new Promise(r=>setTimeout(r,10));}};
async function fakeCamera(){
 let total=0,active=0;
 const server=http.createServer((req,res)=>{
  total++;active++;
  res.writeHead(200,{'Content-Type':'multipart/x-mixed-replace;boundary=source'});
  let frame=0;
  const timer=setInterval(()=>{
   const jpeg=Buffer.from(`jpeg-${++frame}`);
   const part=Buffer.concat([Buffer.from(`\r\n--source\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\nX-Capture-Us: ${frame}\r\n\r\n`),jpeg]);
   res.write(part.subarray(0,13));res.write(part.subarray(13));
  },20);
  res.on('close',()=>{clearInterval(timer);active--;});
 });server.listen(0,'127.0.0.1');await once(server,'listening');
 return {server,port:server.address().port,total:()=>total,active:()=>active};
}
test('zero consumers: idle; two viewers: one upstream; last close disconnects; new viewer reconnects',async t=>{
 const camera=await fakeCamera();
 const bridge=createBridge({host:'127.0.0.1',streamPort:camera.port});bridge.listen(0,'127.0.0.1');await once(bridge,'listening');
 const readers=[];
 t.after(()=>{for(const r of readers){r.response.destroy();r.request.destroy();}bridge.closeAllConnections();bridge.close();camera.server.closeAllConnections();camera.server.close();});
 const health=async()=>await (await fetch(`http://127.0.0.1:${bridge.address().port}/health`)).json();
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
 assert.match(b.data(),/X-Capture-Us:/);
 a.response.destroy();a.request.destroy();await waitFor(()=>readers.length && !a.response.readable);
 await waitFor(()=>camera.active()===1);assert.equal((await health()).video.consumers,1);
 b.response.destroy();b.request.destroy();await waitFor(()=>camera.active()===0);
 assert.equal((await health()).video.consumers,0);assert.equal((await health()).video.connected,false);
 const c=await viewer();await waitFor(()=>c.data().includes('jpeg-'));assert.equal(camera.total(),2);
 c.response.destroy();c.request.destroy();await waitFor(()=>camera.active()===0);
});
test('slow consumer skips intermediate frames and drains only the latest',async t=>{
 const camera=await fakeCamera();const relay=createCameraRelay({host:'127.0.0.1',streamPort:camera.port});
 const res=new EventEmitter();res.destroyed=false;const sent=[];
 res.writeHead=()=>res;res.flushHeaders=()=>{};res.write=frame=>{sent.push(frame.toString());return false;};
 res.destroy=()=>{res.destroyed=true;res.emit('close');};
 t.after(()=>{relay.close();camera.server.closeAllConnections();camera.server.close();});
 relay.handle({method:'GET'},res);
 await waitFor(()=>relay.stats().frames>=6);
 assert.equal(sent.length,1);
 const latest=relay.stats().frames;
 res.emit('drain');assert.equal(sent.length,2);assert.match(sent[1],new RegExp(`jpeg-${latest}`));
 res.destroy();await waitFor(()=>camera.active()===0);assert.equal(relay.stats().consumers,0);
});
