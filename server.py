"""Local browser controls + LAN UDP replies; standard library only."""
import json
import os
import select
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.speeds = (0, 0)
        self.until = 0.0
        self.seen = None
        self.board_ip = None

    def command(self, m1, m2):
        if any(type(v) is not int or not -255 <= v <= 255 for v in (m1, m2)):
            raise ValueError("Motor speeds must be integers from -255 to 255")
        with self.lock:
            self.speeds = m1, m2
            self.until = time.monotonic() + 0.6

    def status(self):
        with self.lock:
            age = None if self.seen is None else max(0, time.monotonic() - self.seen)
            return {"connected": age is not None and age < 1,
                    "board_ip": self.board_ip,
                    "last_seen_seconds": None if age is None else round(age, 1),
                    "driver_status": "not_monitored"}

    def reply(self, packet, address=None):
        try:
            seq = packet.decode("ascii")
            if not seq.isdecimal() or not 0 <= int(seq) <= 0xFFFFFFFF:
                return None
        except (ValueError, UnicodeError):
            return None
        with self.lock:
            self.seen = time.monotonic()
            if address is not None:
                self.board_ip = address[0]
            speeds = self.speeds if self.seen < self.until else (0, 0)
        return f"{seq} {speeds[0]} {speeds[1]}".encode()


PAGE = b"""<!doctype html><meta name="viewport" content="width=device-width">
<title>Bus motors</title><style>body{font:20px system-ui;max-width:650px;margin:40px auto;padding:16px}button{font:inherit;padding:18px;margin:6px}#stop{background:#fbb}</style>
<h1>Bus motor test</h1><p>Each click runs a 0.6-second pulse. Lift wheels first.</p>
<p role="status" aria-live="polite"><strong>Board:</strong> <span id="status">Checking...</span><br>
<small id="board-details">Waiting for first contact</small></p>
<p><strong>DRV8833:</strong> Not monitored<br><small>Current wiring cannot detect the driver or read faults.</small></p>
<p><label for="speed">Speed (both motors): <output id="speed-value" for="speed">47%</output></label><br>
<input id="speed" type="range" min="0" max="100" value="47" style="width:100%" oninput="document.querySelector('#speed-value').textContent=this.value+'%'"></p>
<div>Motor 1 <button onclick="run(1,0)">Forward</button><button onclick="run(-1,0)">Reverse</button></div>
<div>Motor 2 <button onclick="run(0,1)">Forward</button><button onclick="run(0,-1)">Reverse</button></div>
<button id="stop" onclick="run(0,0)">Stop both</button><p id="error"></p>
<script>async function run(m1,m2){const speed=Math.round(Number(document.querySelector('#speed').value)*255/100);m1*=speed;m2*=speed;try{let r=await fetch('/motors',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({m1,m2})});if(!r.ok)throw Error(await r.text());document.querySelector('#error').textContent=''}catch(e){document.querySelector('#error').textContent=e.message}}
async function updateStatus(){const status=document.querySelector('#status');const details=document.querySelector('#board-details');try{const r=await fetch('/status',{signal:AbortSignal.timeout(2000)});if(!r.ok)throw Error('Status unavailable');const s=await r.json();status.textContent=s.connected?'Connected over Wi-Fi':s.last_seen_seconds===null?'Waiting for Arduino':'Disconnected';status.style.color=s.connected?'#16733a':'#9b341c';details.textContent=s.last_seen_seconds===null?'No heartbeat received yet':s.board_ip+' | Last seen '+s.last_seen_seconds.toFixed(1)+'s ago'}catch(e){status.textContent='Server offline';status.style.color='#9b341c';details.textContent='Board connection unknown'}finally{setTimeout(updateStatus,500)}}updateStatus();</script>"""


def handler(state):
    class Handler(BaseHTTPRequestHandler):
        def respond(self, code, body, content_type="application/json"):
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == "/":
                self.respond(200, PAGE, "text/html; charset=utf-8")
            elif self.path == "/status":
                self.respond(200, json.dumps(state.status()).encode())
            else:
                self.respond(404, b'{}')

        def do_POST(self):
            # JSON-only, no CORS: other websites cannot issue motor commands.
            if self.path != "/motors":
                return self.respond(404, b'{}')
            if self.headers.get("Content-Type") != "application/json":
                return self.respond(415, b'{}')
            if self.headers.get("Sec-Fetch-Site") == "cross-site":
                return self.respond(403, b'{}')
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= 128:
                    raise ValueError("Invalid body size")
                body = json.loads(self.rfile.read(size))
                state.command(body["m1"], body["m2"])
            except (ValueError, KeyError, TypeError):
                return self.respond(400, b'{"error":"Invalid motor command"}')
            self.respond(200, b'{"ok":true}')
    return Handler


def main():
    state = State()
    http = ThreadingHTTPServer(("127.0.0.1", int(os.environ.get("HTTP_PORT", "8000"))), handler(state))
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(("0.0.0.0", int(os.environ.get("SERVER_PORT") or "8766")))
    threading.Thread(target=http.serve_forever, daemon=True).start()
    print(f"Open http://localhost:{http.server_port}; UDP port {udp.getsockname()[1]}", flush=True)
    try:
        while True:
            if select.select([udp], [], [], 0.2)[0]:
                packet, address = udp.recvfrom(256)
                reply = state.reply(packet, address)
                if reply is not None:
                    udp.sendto(reply, address)
    except KeyboardInterrupt:
        pass
    finally:
        state.command(0, 0)
        udp.close()
        http.shutdown()
        http.server_close()


if __name__ == "__main__":
    main()
