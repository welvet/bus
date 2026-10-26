"""Local browser controls + authenticated LAN UDP replies; standard library only."""
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
        self.seen = 0.0

    def command(self, m1, m2):
        if any(type(v) is not int or not -255 <= v <= 255 for v in (m1, m2)):
            raise ValueError("Motor speeds must be integers from -255 to 255")
        with self.lock:
            self.speeds = m1, m2
            self.until = time.monotonic() + 0.6

    def reply(self, packet, token):
        try:
            auth, seq = packet.decode("ascii").split()
            if auth != token or not seq.isdecimal() or not 0 <= int(seq) <= 0xFFFFFFFF:
                return None
        except (ValueError, UnicodeError):
            return None
        with self.lock:
            self.seen = time.monotonic()
            speeds = self.speeds if self.seen < self.until else (0, 0)
        return f"{token} {seq} {speeds[0]} {speeds[1]}".encode()


PAGE = b"""<!doctype html><meta name="viewport" content="width=device-width">
<title>Bus motors</title><style>body{font:20px system-ui;max-width:650px;margin:40px auto;padding:16px}button{font:inherit;padding:18px;margin:6px}#stop{background:#fbb}</style>
<h1>Bus motor test</h1><p>Each click runs a 0.6-second pulse at PWM 120/255. Lift wheels first.</p>
<p id="status">Checking Arduino...</p>
<div>Motor 1 <button onclick="run(120,0)">Forward</button><button onclick="run(-120,0)">Reverse</button></div>
<div>Motor 2 <button onclick="run(0,120)">Forward</button><button onclick="run(0,-120)">Reverse</button></div>
<button id="stop" onclick="run(0,0)">Stop both</button><p id="error"></p>
<script>async function run(m1,m2){try{let r=await fetch('/motors',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({m1,m2})});if(!r.ok)throw Error(await r.text());document.querySelector('#error').textContent=''}catch(e){document.querySelector('#error').textContent=e.message}}
setInterval(async()=>{try{let r=await fetch('/status');let s=await r.json();document.querySelector('#status').textContent=s.connected?'Arduino connected':'Waiting for Arduino'}catch(e){document.querySelector('#status').textContent='Server offline'}},500);</script>"""


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
                with state.lock:
                    connected = time.monotonic() - state.seen < 1
                self.respond(200, json.dumps({"connected": connected}).encode())
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
    token = os.environ.get("BUS_TOKEN", "")
    if not 16 <= len(token) <= 64 or not token.isascii() or not token.isalnum():
        raise SystemExit("Set BUS_TOKEN to the same 16–64 ASCII letters/digits used when flashing")
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
                reply = state.reply(packet, token)
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
