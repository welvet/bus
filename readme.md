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
The old nginx HTTP routes do not apply. Close the monitor before flashing. Core
1.6.0 includes Arduino_FreeRTOS; setup also installs Servo 1.3.0. On Apple Silicon
the compiler may require Rosetta. `make test` uses native C++ only.

## Wire format

Each message is exactly **three raw binary bytes**: `[1, angle, speed]`.
No JSON, newline, separate ping or reply. Byte 1 is fixed `0x01`, reserved for
future message types. Bytes 2/3 are signed int8, two's complement:

| Byte | Meaning | Range |
|---|---|---|
| 1 | Message type | Always 1 |
| 2 | Servo offset | -90…90°; 0 = center; -90/90 = nominal 0/180° |
| 3 | Both motors' speed | -100…100%; 0 stops motors, negative reverses |

Examples (hex): `01 00 64` = centered servo/full forward;
`01 A6 9C` = nominal 0°/full reverse; `01 00 00` = center/stop motors.
Byte value 180 is **not** servo 180°; use signed +90.

Send the latest complete target as often as desired, always less than **1 second**
apart, even when unchanged. Every valid full message refreshes the timeout.
The board stores both targets and only changes each output when that target
changes. Split TCP messages and multiple messages in one read are supported.
Incomplete bytes do not refresh the timer. Invalid types/values close the socket.

After **1000 ms** without a complete valid message, motors stop, servo pulses
detach, and the connection closes. Detected TCP closure/Wi-Fi loss does the same.
The actuator task checks the deadline every 5 ms independently of modem calls;
network socket closure follows when its task can run. A timed-out session stays
disabled despite late messages: reconnect and send a fresh target. New connections
start with motors stopped/servo disabled and must send within one second. Only
one controller is accepted; extras are refused.

Servo detachment releases torque (the mechanism may move under load). Servo
pulses start on the first complete command. Pulse span is 500–2500 µs: calibrate
for the actual servo and test with linkage detached. There is no position feedback.
Wi-Fi/S3 modem, TCP and scheduling add latency; hardware latency is unmeasured.
A future client should disable Nagle and avoid buffering old targets. Trusted LAN
only: no authentication/TLS/port forwarding. See [hardware.md](hardware.md).
Never share generated config.h or firmware binaries containing credentials.
