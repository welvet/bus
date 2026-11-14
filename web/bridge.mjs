import http from 'node:http';
import dgram from 'node:dgram';
import {createCameraRelay} from './camera-relay.mjs';
import {createControlService} from './control-service.mjs';

export function cameraJson(host,path,method='GET') {
 return new Promise((resolve,reject)=>{
  const req=http.request({hostname:host,port:80,path,method,agent:false,headers:method==='POST'?{'Content-Length':0}:{}},res=>{
   let body='';res.on('data',c=>body+=c);res.on('end',()=>{try{if(res.statusCode!==200)throw Error(`camera HTTP ${res.statusCode}`);resolve(JSON.parse(body));}catch(e){reject(e);}});res.on('error',reject);
  });req.setTimeout(6500,()=>req.destroy(Error('Camera HTTP request timed out')));req.on('error',reject);req.end();
 });
}
export function sendUdp(host,port,command) {
 return new Promise((resolve,reject)=>{
  const socket=dgram.createSocket('udp4');let finished=false;
  const finish=(error,value)=>{if(finished)return;finished=true;clearTimeout(timer);socket.close();error?reject(error):resolve(value);};
  const timer=setTimeout(()=>finish(Error('Camera UDP acknowledgment timed out')),450);
  socket.once('error',e=>finish(e));socket.on('message',(data,peer)=>{
   if(peer.address!==host||peer.port!==port)return;
   try{const ack=JSON.parse(data);if(ack.counter===command.counter&&typeof ack.accepted==='boolean')finish(null,ack);}catch{}
  });socket.send(JSON.stringify(command),port,host);
 });
}
export function createBridge({host='192.168.8.209',port=4210,streamPort=81,transport,riseMs=1000,autoStart=true}={}) {
 const relay=createCameraRelay({host,streamPort});
 const control=createControlService({...transport || {getStatus:()=>cameraJson(host,'/status'),newSession:()=>cameraJson(host,'/session','POST'),send:c=>sendUdp(host,port,c)},riseMs,autoStart});
 const server=http.createServer(async(req,res)=>{
  const reply=(code,body)=>{if(!res.destroyed)res.writeHead(code,{'Content-Type':'application/json','Cache-Control':'no-store'}).end(JSON.stringify(body));};
  const path=req.url?.split('?')[0];
  if(path==='/health'&&req.method==='GET')return reply(200,{ok:true,camera:host,video:relay.stats()});
  if(path==='/stream')return relay.handle(req,res);
  if(path==='/control/status'&&req.method==='GET'){control.refresh();return reply(200,control.status());}
  if(path==='/command'||path==='/session')return reply(410,{error:'Use /api/control/acquire, /api/control/input, /api/control/release; raw motor commands are disabled'});
  if(!['/control/acquire','/control/input','/control/release','/control/lights'].includes(path))return reply(404,{error:'not found'});
  if(req.method!=='POST')return reply(405,{error:'POST required'});
  try{
   if(req.headers.origin&&new URL(req.headers.origin).host!==req.headers.host)return reply(403,{error:'cross-origin request'});
   if(!req.headers['content-type']?.startsWith('application/json'))return reply(415,{error:'application/json required'});
   let body='';for await(const chunk of req){body+=chunk;if(Buffer.byteLength(body)>1024)return reply(413,{error:'maximum 1024 bytes'});}
   const data=JSON.parse(body);
   if(!data||Array.isArray(data)||typeof data!=='object')return reply(400,{error:'JSON object required'});
   if(path==='/control/acquire')return reply(200,control.acquire());
   if(path==='/control/lights')return reply(200,await control.setLights(data.on));
   reply(200,path==='/control/input'?control.input(data):control.release(data));
  }catch(e){reply(e.status|| (e instanceof SyntaxError?400:502),{error:e.message});}
 });
 server.on('close',()=>{control.close();relay.close();});
 return server;
}
if(process.argv[1]&&import.meta.url===new URL(process.argv[1],'file:').href){
 createBridge({host:process.env.BUS_CAMERA_IP||'192.168.8.209',riseMs:Number(process.env.BUS_ACCELERATION_MS||1000)})
 .listen(Number(process.env.PORT||8792),'127.0.0.1',()=>console.log('Bus control and camera relay on 127.0.0.1:8792'));
}
