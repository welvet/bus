import http from 'node:http';

// One lazy upstream; downstream clients hold at most one pending latest frame.
export function createCameraRelay({ host, streamPort = 81 }) {
  const clients = new Set();
  let upstream = null, frames = 0;
  const boundary = 'busrelay';
  function stopUpstream() {
    const old = upstream; upstream = null;
    old?.response?.destroy(); old?.request?.destroy();
  }
  function remove(client) {
    clearTimeout(client.timer); clients.delete(client);
    if (!clients.size) stopUpstream();
  }
  function write(client, frame) {
    if (client.res.destroyed) return;
    if (!client.res.write(frame)) {
      client.blocked = true;
      client.timer = setTimeout(() => client.res.destroy(), 5000);
    }
  }
  function publish(header, jpeg) {
    frames++;
    const frame = Buffer.concat([Buffer.from(`--${boundary}\r\n${header}\r\n\r\n`), jpeg, Buffer.from('\r\n')]);
    for (const client of clients) {
      if (client.blocked) client.latest = frame;
      else write(client, frame);
    }
  }
  function connect() {
    const current = { request:null, response:null };
    upstream = current;
    function failed() {
      if (upstream !== current) return;
      stopUpstream();
      for (const client of [...clients]) client.res.destroy();
    }
    const request = http.get({ hostname:host, port:streamPort, path:'/stream', agent:false }, response => {
      if (upstream !== current) { response.destroy(); return; }
      current.response = response;
      if (response.statusCode !== 200 || !response.headers['content-type']?.startsWith('multipart/x-mixed-replace')) { failed(); return; }
      let buffer = Buffer.alloc(0), header = null, length = 0;
      response.on('data', chunk => {
        if (upstream !== current) return;
        buffer = Buffer.concat([buffer,chunk]);
        if (buffer.length > 2 * 1024 * 1024) { failed(); return; }
        for (;;) {
          if (header === null) {
            const end = buffer.indexOf('\r\n\r\n');
            if (end < 0) { if (buffer.length > 4096) failed(); break; }
            const text = buffer.subarray(0,end).toString('ascii');
            const match = /(?:^|\r\n)Content-Length:\s*(\d+)\s*(?:\r\n|$)/i.exec(text);
            if (!match || !/Content-Type:\s*image\/jpeg/i.test(text)) { failed(); break; }
            length = Number(match[1]);
            if (length < 1 || length > 1024 * 1024) { failed(); break; }
            const capture = /(?:^|\r\n)(X-Capture-Us:\s*\d+)/i.exec(text);
            header = `Content-Type: image/jpeg\r\nContent-Length: ${length}${capture ? '\r\n' + capture[1] : ''}`;
            buffer = buffer.subarray(end+4);
          }
          if (buffer.length < length) break;
          publish(header,buffer.subarray(0,length));
          buffer = buffer.subarray(length); header = null;
        }
      });
      response.on('end',failed); response.on('error',failed); response.on('aborted',failed);
    });
    current.request = request;
    request.setTimeout(5000,failed); request.on('error',failed);
  }
  return {
    stats:() => ({ consumers:clients.size, connected:!!upstream?.response, connecting:!!upstream && !upstream.response, frames }),
    handle(req,res) {
      if (req.method !== 'GET') { res.writeHead(405).end(); return; }
      res.writeHead(200,{'Content-Type':`multipart/x-mixed-replace; boundary=${boundary}`,'Cache-Control':'no-store','X-Accel-Buffering':'no'});
      res.flushHeaders();
      const client = {res,blocked:false,latest:null,timer:null};
      clients.add(client);
      res.on('close',()=>remove(client));
      res.on('drain',()=>{
        clearTimeout(client.timer);client.timer=null;client.blocked=false;
        const latest=client.latest;client.latest=null;
        if(latest) write(client,latest);
      });
      if (!upstream) connect();
    },
    close() { stopUpstream(); for(const client of [...clients]) client.res.destroy(); }
  };
}
