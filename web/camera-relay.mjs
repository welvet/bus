import {spawn} from 'node:child_process';

export function rtspArguments({host,rtspPort=8554}) {
  return ['-hide_banner','-loglevel','warning','-nostdin',
    '-rtsp_transport','udp','-timeout','5000000','-fflags','nobuffer',
    '-analyzeduration','100000','-probesize','32768','-max_delay','0','-reorder_queue_size','0',
    '-i',`rtsp://${host}:${rtspPort}/mjpeg/1`,'-map','0:v:0','-an','-sn','-dn',
    '-c:v','copy','-flush_packets','1','-f','mpjpeg','pipe:1'];
}

// One RTSP/RTP-UDP receiver for all browser clients. JPEGs are remuxed, not
// decoded/re-encoded; a slow viewer retains only its latest pending frame.
export function createCameraRelay({host,rtspPort=8554,ffmpeg=process.env.BUS_FFMPEG||'ffmpeg',spawnProcess=spawn}) {
  const clients=new Set();
  let upstream=null,frames=0,lastError=null;
  const boundary='busrelay';
  function stopUpstream() {
    const old=upstream;upstream=null;
    if(!old)return;
    clearTimeout(old.watchdog);
    old.child.stdout.destroy();
    old.child.kill('SIGTERM');
    const force=setTimeout(()=>old.child.kill('SIGKILL'),1000);force.unref();
    old.child.once('close',()=>clearTimeout(force));
  }
  function remove(client) {
    clearTimeout(client.timer);clients.delete(client);
    if(!clients.size)stopUpstream();
  }
  function write(client,frame) {
    if(client.res.destroyed)return;
    if(!client.res.write(frame)){
      client.blocked=true;
      client.timer=setTimeout(()=>client.res.destroy(),5000);
    }
  }
  function publish(jpeg) {
    frames++;
    const frame=Buffer.concat([Buffer.from(`--${boundary}\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\n\r\n`),jpeg,Buffer.from('\r\n')]);
    for(const client of clients){
      if(client.blocked)client.latest=frame;
      else write(client,frame);
    }
  }
  function connect() {
    let child;
    try{child=spawnProcess(ffmpeg,rtspArguments({host,rtspPort}),{stdio:['ignore','pipe','pipe'],windowsHide:true});}
    catch(error){lastError=error.message;for(const c of [...clients])c.res.destroy();return;}
    const current={child,watchdog:null,connected:false,stderr:''};
    upstream=current;lastError=null;
    function failed(reason){
      if(upstream!==current)return;
      lastError=reason;stopUpstream();
      for(const client of [...clients])client.res.destroy();
    }
    function alive(){
      clearTimeout(current.watchdog);
      current.watchdog=setTimeout(()=>failed('RTSP produced no complete frame for 5 seconds'),5000);
    }
    let buffer=Buffer.alloc(0),length=null;
    child.stdout.on('data',chunk=>{
      if(upstream!==current)return;
      buffer=Buffer.concat([buffer,chunk]);
      if(buffer.length>2*1024*1024){failed('MJPEG receive buffer exceeded limit');return;}
      for(;;){
        if(length===null){
          const end=buffer.indexOf('\r\n\r\n');
          if(end<0){if(buffer.length>4096)failed('MJPEG header exceeded limit');break;}
          const text=buffer.subarray(0,end).toString('ascii');
          const match=/(?:^|\r\n)Content-Length:\s*(\d+)\s*(?:\r\n|$)/i.exec(text);
          if(!match||!/Content-Type:\s*image\/jpeg/i.test(text)){failed('Invalid MJPEG header');break;}
          length=Number(match[1]);
          if(length<1||length>1024*1024){failed('Invalid JPEG size');break;}
          buffer=buffer.subarray(end+4);
        }
        if(buffer.length<length)break;
        current.connected=true;alive();
        publish(buffer.subarray(0,length));
        buffer=buffer.subarray(length);length=null;
      }
    });
    child.stderr.on('data',chunk=>{current.stderr=(current.stderr+chunk.toString()).slice(-4096);});
    child.on('error',error=>failed(`FFmpeg: ${error.message}`));
    child.on('close',code=>failed(`FFmpeg exited (${code}): ${current.stderr.trim()}`));
    alive();
  }
  return {
    stats:()=>({consumers:clients.size,connected:!!upstream?.connected,connecting:!!upstream&&!upstream.connected,
      frames,transport:'rtsp/udp',last_error:lastError}),
    handle(req,res){
      if(req.method!=='GET'){res.writeHead(405).end();return;}
      res.writeHead(200,{'Content-Type':`multipart/x-mixed-replace; boundary=${boundary}`,'Cache-Control':'no-store','X-Accel-Buffering':'no'});
      res.flushHeaders();
      const client={res,blocked:false,latest:null,timer:null};clients.add(client);
      res.on('close',()=>remove(client));
      res.on('drain',()=>{
        clearTimeout(client.timer);client.timer=null;client.blocked=false;
        const latest=client.latest;client.latest=null;if(latest)write(client,latest);
      });
      if(!upstream)connect();
    },
    close(){stopUpstream();for(const client of [...clients])client.res.destroy();}
  };
}
