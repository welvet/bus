# bus

Uno R4 WiFi: persistent binary TCP control, FreeRTOS network/actuator tasks and
one latest-target queue. No HTTP, UI, Python or client. Both N20 motors share
one signed speed; MG90S is on D3. Network calls never run in the actuator task.

```sh
make setup
export WIFI_SSID='your-wifi'
export WIFI_PASSWORD='your-password'
make boards
make install PORT=/dev/cu.usbmodemXXXX
make monitor PORT=/dev/cu.usbmodemXXXX
```

Serial prints `TCP listening: IP:5000`. Connect directly to that IP on port 5000.
The previous nginx HTTP routes and browser UI no longer apply. Close the monitor
before flashing. Setup pins Arduino core 1.6.0 (bundled Arduino_FreeRTOS) and Servo
1.3.0. On Apple Silicon the compiler may require Rosetta. `make test` uses native
C++ only. UI changes from earlier versions are removed; no client is included.

## Wire format

Every message is exactly **two raw binary bytes**: `[type, value]`. No text,
JSON, newline or length prefix. Value is signed int8, two's complement.

| Type byte | Meaning | Value |
|---|---|---|
| `0x01` | Both motors | -100…100 percent; 0 stops, negative reverses |
| `0x02` | Servo offset | -90…90 degrees; 0 = center, -90/90 = nominal 0/180° |
| `0x03` | Ping | Must be 0; board echoes `[0x03, 0x00]` |

Examples: `01 64` = full forward; `01 9C` = full reverse; `02 00` = servo center.
Hex `02 A6`/`02 5A` select servo extremes. Byte value 180 is **not** servo 180°.
Motor/servo commands have no reply. TCP handles ordering/delivery; parsing handles
fragmented and combined messages. Unknown types/out-of-range values close the socket.
Only one controller is accepted; extras are refused while it is connected.

Send ping immediately after connecting, then every **250 ms**, independently of
motor/servo activity. After **750 ms** without a valid ping, outputs stop and the
connection is closed. Ordinary commands do not refresh the heartbeat. The higher
priority actuator task checks the deadline every 5 ms, even if a modem call stalls;
the network task closes its socket when it can run. Detected TCP closure or Wi-Fi
loss also disables outputs. The actuator watchdog latches a failed session off;
late messages cannot revive it. Reconnect and issue fresh commands to resume.

Motor commands persist while the session stays healthy. Servo pulses start only
on its first command and are detached on disconnect/timeout; torque then releases
(the servo may move under load). New sessions start with motors stopped and servo
disabled. Servo uses 500–2500 µs: calibrate endpoints for the actual unit and test
with linkage detached. No motor-motion or servo-position feedback exists.

Wi-Fi/S3 modem, TCP, client buffering and scheduling still add latency; no hardware
latency measurement has been made. A future client should disable Nagle and avoid
queueing old targets. Trusted LAN only: no authentication/TLS or port forwarding.
See [hardware.md](hardware.md). Generated config.h/build output contain credentials;
do not share them. `.env` is not loaded automatically.
