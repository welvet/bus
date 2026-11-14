// Ten-minute camera stability test. Never sends Drive or servo-angle commands.
import http from 'node:http';
import {mkdirSync,appendFileSync,writeFileSync} from 'node:fs';
const seconds=Number(process.argv[2] || 600);
if(!Number.isFinite(seconds)||seconds<1)throw Error('duration must be positive seconds');
mkdirSync('.build',{recursive:true});
const stamp=new Date().toISOString().replaceAll(':','-');
const logPath=`.build/camera-monitor-${stamp}.jsonl`;
const summaryPath=`.build/camera-monitor-${stamp}.summary.json`;
const started=Date.now(),deadline=started+seconds*1000;
const summary={started:new Date(started).toISOString(),duration_seconds:seconds,frames:0,stream_connections:0,stream_errors:0,status_ok:0,status_errors:0,session_changes:0,frame_counter_drops:0,max_frame_gap_ms:0,mode:'not-requested',motion_commands:0};
let session=0,counter=0,stream=null,streamResponse=null,stopping=false,lastFrame=0,previous=null,loopTimer;
function log(event,data={}){const entry={time:new Date().toISOString(),elapsed_s:Math.round((Date.now()-started)/1000),event,...data};appendFileSync(logPath,JSON.stringify(entry)+'\n');if(event!=='status')console.log(JSON.stringify(entry));}
function request(path,method='GET',body){return new Promise((resolve,reject)=>{
 const content=body===undefined?null:JSON.stringify(body);
 const req=http.request({host:'127.0.0.1',port:80,path,method,headers:{Host:'bus.local',...(content?{'Content-Type':'application/json','Content-Length':Buffer.byteLength(content)}:{})}},res=>{
  let text='';res.on('data',c=>text+=c);res.on('end',()=>{try{const data=JSON.parse(text);if(res.statusCode>=400)throw Error(`${res.statusCode}: ${data.reason||data.error||text}`);resolve(data);}catch(error){reject(error);}});res.on('error',reject);
 });req.setTimeout(6500,()=>req.destroy(Error('HTTP timeout')));req.on('error',reject);req.end(content);
});}
async function command(op){
 if(op!=='stop')throw Error('monitor prohibits actuator input');
 if(!session)return {released:true};
 const result=await request('/api/control/release','POST',{lease:session,sequence:++counter});session=0;return result;
}
function connectStream(){
 if(stopping||Date.now()>=deadline)return;
 summary.stream_connections++;let received=false,tail='';
 const req=http.get({host:'127.0.0.1',port:80,path:'/camera/stream',headers:{Host:'bus.local'}},res=>{
  streamResponse=res;
  if(res.statusCode!==200){res.resume();req.destroy(Error(`stream HTTP ${res.statusCode}`));return;}
  res.on('data',chunk=>{
   const text=tail+chunk.toString('latin1');const needle='Content-Type: image/jpeg';let pos=0,count=0;
   while((pos=text.indexOf(needle,pos))!==-1){count++;pos+=needle.length;}
   tail=text.slice(-(needle.length-1));
   if(count){const now=Date.now();if(lastFrame)summary.max_frame_gap_ms=Math.max(summary.max_frame_gap_ms,now-lastFrame);lastFrame=now;summary.frames+=count;if(!received){received=true;log('stream_receiving');}}
  });res.on('error',error=>log('stream_response_error',{error:error.message}));
 });
 stream=req;
 req.setTimeout(7000,()=>req.destroy(Error('no stream data for 7 seconds')));
 req.on('error',error=>{if(!stopping){summary.stream_errors++;log('stream_error',{error:error.message});}});
 req.on('close',()=>{if(!stopping){log('stream_closed');setTimeout(connectStream,2000);}});
}
async function sample(){
 try{
  const s=await request('/api/status');summary.status_ok++;
  if(previous&&s.session!==previous.session){summary.session_changes++;log('session_changed',{from:previous.session,to:s.session});}
  if(previous&&s.frames<previous.frames){summary.frame_counter_drops++;log('possible_restart',{previous_frames:previous.frames,frames:s.frames});}
  previous=s;summary.latest_status=s;
  log('status',s);
 }catch(error){summary.status_errors++;log('status_error',{error:error.message});}
 if(!stopping&&Date.now()<deadline)loopTimer=setTimeout(sample,5000);
}
async function finish(reason){
 if(stopping)return;stopping=true;clearTimeout(loopTimer);streamResponse?.destroy();stream?.destroy();
 // Stop is safe even if the mode changed; never re-enable or re-arm on recovery.
 if(session)try{log('final_stop',await command('stop'));}catch(error){log('final_stop_error',{error:error.message});}
 summary.ended=new Date().toISOString();summary.elapsed_seconds=Math.round((Date.now()-started)/1000);summary.reason=reason;
 if(lastFrame)summary.max_frame_gap_ms=Math.max(summary.max_frame_gap_ms,Math.min(Date.now(),deadline)-lastFrame);
 writeFileSync(summaryPath,JSON.stringify(summary,null,2)+'\n');log('complete',{summary:summaryPath,...summary});
 process.exit(0);
}
process.on('SIGINT',()=>finish('interrupted'));process.on('SIGTERM',()=>finish('terminated'));
log('start',{log:logPath,summary:summaryPath,no_drive_commands:true});
setTimeout(()=>finish('duration complete'),seconds*1000);
try{
 const s=await request('/api/status');log('initial_status',s);
 const created=await request('/api/control/acquire','POST',{});session=created.lease;counter=0;log('lease_created',{lease:session});
 for(let i=0;i<20;i++){
   const status=await request('/api/control/status');
   if(status.ready){summary.mode='auto-requested';break;}
   if(status.error)log('control_setup_status',{error:status.error});
   await new Promise(resolve=>setTimeout(resolve,500));
 }
 log('release_ack',await command('stop')); // Release never attaches the servo.
}catch(error){log('setup_error',{error:error.message});summary.mode='setup-failed';}
connectStream();sample();
setInterval(()=>log('progress',{frames:summary.frames,status_ok:summary.status_ok,status_errors:summary.status_errors,stream_errors:summary.stream_errors,max_frame_gap_ms:summary.max_frame_gap_ms,session_changes:summary.session_changes,frame_counter_drops:summary.frame_counter_drops}),60000).unref();
