# bus

Arduino Uno R4 WiFi: the onboard ESP32-S3 reads an Xbox BLE controller and sends
serial commands to the Renesas RA4M1, which controls both motors and the servo.
USB belongs to S3. Manual driving needs no backend, Wi-Fi or credentials.
Auto control accepts camera-ESP motion on a separate wired UART.

## Firmware

- `firmware/s3/s3.ino`: internal UART transport, sending control at 50 Hz.
- `firmware/s3/xbox.cpp`: BLE connection, validated input reports, controller-loss
  handling and quiet USB logs. It exposes `xboxBegin`, `xboxPoll`, `xboxCommand`.
- `firmware/s3/control.h`: steering/trigger curves and neutral arming.
- `firmware/shared/BusProtocol.h`: the same framing, commands, checksum and timeouts
  compiled into both chips. The flash script supplies this local Arduino library.
- `firmware/bus/bus.ino`: serial reception and the restored motor/servo actuator
  task, with an independent input watchdog. No network task or credentials.
- `firmware/bus/priority.h`: Renesas-owned manual/auto selection, per-source
  arming and nonblocking auto-entry headlight indication.
- `firmware/camera/`: separate GOOUUU ESP32-S3-CAM N16R8 firmware,
  RTP video and latest-state UDP control. This is not the Uno's onboard S3 sketch.
- `firmware/camera/rtsp.cpp`: separate paced RTP/JPEG task using a pinned RTSP
  library; the camera frame is copied to PSRAM and released before sending.
- `firmware/bus/display.cpp`: built-in 12x8 LED matrix display of software-applied
  speed, servo angle and stopped state; its pixel layout is in `display.h`.

## Controls and stopping

Left stick X steers. Right trigger moves forward, left trigger reverse; the
stronger trigger wins and equal pressures stop. Triggers have a 2% release zone,
then quadratic output from a shared 30% minimum to 100%. The electrical motor
sign matches the current motor mounting.

Steering has a rescaled 12% deadzone and symmetric quadratic travel from -45 to +45 degrees,
with a fixed center of zero. Face buttons do not calibrate or change steering.
No steering trim is read or saved; previously stored trim is ignored.

B toggles the headlights on each press (holding it does not repeat). Renesas
D7 drives the XY-MOS TRIG/PWM input HIGH for on, LOW for off. Power the module
and the two 5 V resistor-equipped LEDs from the servo's 5 V buck, with a common
ground to Arduino; do not connect the buck's 5 V output to Arduino's 5 V pin.
Lights start off when S3 starts and retain their setting across controller
disconnects and motion stops. Reconnecting with B held does not toggle them;
release and press again. Light state is independent of neutral motion arming.
Two pixels on the left edge of the matrix's middle row light up when Renesas
sets D7 HIGH, including while the stopped X is displayed. USB prints
`Lights: requested=on D7=HIGH rx_ms=...` once per second; `D7=unknown` means
no recent light acknowledgment. This confirms software pin state, not LED current.
The serial task owns runtime matrix updates (10 Hz); actuator stopping remains
independent of display and UART work.

After startup, reconnection or input loss, center steering and release both
triggers before Xbox control arms. A requests auto while controls are neutral;
holding or repeatedly pressing it enables auto, never toggles it off. Moving
with A held sends manual Drive instead. Releasing movement with A still held
requests auto again; release A if you want to remain manual.
The connection transition logs are:

```text
Xbox: scanning
Xbox: connected
Xbox: disconnected
```

S3 prints `Bus S3: initializing` at startup and a `Bus S3: ping` once per second
over USB. The ping includes uptime, BLE stage, scan-result count, failed attempts,
last report-setup error and worker age to diagnose connection failures without
dumping raw inputs. Increasing worker age means the BLE worker is blocked or
has not started; `initializing` distinguishes BLE startup from active scanning.
`rx_ms` is the age of the last Xbox input report (`-1` means none), while `armed`
shows whether S3 has accepted neutral controls and is allowing Drive commands.
`BLE=input` means an input report has been received during this connection.
An old `rx_ms` is normal when controls are unchanged; it does not trigger a stop.
There are no raw input-value logs. A connected BLE link
alone does not establish that valid input is arriving or that control is armed.
Startup logs are buffered without waiting for a USB monitor. Press Enter in the
monitor to print the current scanning/connected state again.
Controller address: `40:8e:2c:32:c8:74`, configured in `firmware/s3/xbox.cpp`.

Scanning runs continuously for the first 30 seconds after startup or disconnection.
After that, S3 scans for 1 second every 5 seconds to reduce idle radio power;
reconnection can consequently take several seconds longer. The schedule lives in
`firmware/s3/scan.h`. UART Stop messages and the BLE worker heartbeat continue
during the gaps. Scanning ends when a controller is found; connected control is
unaffected. Both processors and the LED matrix remain powered during idle gaps.

Stopping is enforced on both chips:

- The BLE disconnect callback immediately invalidates control; S3 sends a Stop
  frame on the next main-loop iteration, and continues sending Stop at 50 Hz.
- A BLE worker stalled for **250 ms** also disarms control. At least one valid
  input report from the current connection is required. After that, unchanged
  controls stay valid while BLE is connected and its worker is live; inactivity
  is not a disconnect. Old-session reports cannot arm a reconnection.
- Renesas stops motors and detaches servo pulses on the selected source's Stop,
  or after **1000 ms** without accepted Drive frames from that source.
  Its actuator task owns the timeout, independently
  of serial reception. Malformed traffic does not refresh it. Old queued targets
  are checked for expiry before application. After stop/timeout, moving commands
  are rejected until a zero-speed Drive frame arrives.

Radio loss first has to be detected by BLE link supervision; this is not a claim
of instantaneous RF-loss detection. Detaching stops servo pulses and
releases holding torque; it does not command the servo to return to center.

## Auto-control priority

Renesas boots in **manual** mode. It arbitrates the existing onboard S3 link
(`Serial`, controller) and a new camera link (`Serial1`, D0 RX / D1 TX), both
115200 baud, 8N1. Connect camera TX -> D0/RX and camera RX <- D1/TX through a
3.3 V / 5 V logic-level converter; share GND. D0/D1 carry 5 V logic on the Uno.
The camera is powered separately from the 5 V buck, not from a signal pin.

| Event | Renesas behavior |
|---|---|
| Controller A / `Auto` | Enter auto, stop old motion and wait for camera arming. Repeats are idempotent. |
| Camera `Auto` | Same explicit activation, allowing use without Xbox. Rejected while fresh nonzero manual demand exists. |
| Nonzero controller Drive angle or speed | Switch to manual immediately; camera Drive is ignored. Existing manual neutral arming still applies. |
| Neutral controller Drive while auto | Ignore for motion/watchdog; maintain manual readiness. |
| Controller Stop/disconnect while auto | Ignore for camera motion; auto remains usable without Xbox. |
| Camera Drive while manual | Ignore for motion; never implicitly activate auto. |
| Selected-source Stop or 1000 ms Drive timeout | Disable motors/servo and require a zero-speed Drive before motion resumes. Mode stays selected. |

Mode entry stops the previous source; no inactive motion target is cached for
later execution. The camera must send a zero-speed Drive after entering auto
and after its Stop/timeout. Neutral controller frames, Auto requests, lighting,
status and invalid traffic cannot keep camera motion alive. The actuator task's
independent watchdog and stale-target checks remain in effect if reception stalls.
Controller input is processed before camera input each serial-task iteration;
a fresh nonzero manual demand blocks camera Auto requests until neutral, Stop,
or expiry. Only an explicit Auto can enable auto after manual override.

Headlights blink twice (150 ms on/off phases) on an actual transition into auto,
without delays. Repeated Auto does not restart the blink. B's saved light state
is restored afterward; returning to manual cancels the blink. Both UARTs receive
Applied/Stopped, LightsApplied and Mode every 100 ms. USB prints
`Mode: manual`, `Mode: auto` or `Mode: unknown` when mode feedback is stale.

The camera ESP owns UDP sessions, increasing counters, duplicate/reorder rejection
and the latest state's duration in **milliseconds**. A newer state replaces the
previous state immediately; there is no sequence queue. It emits ordinary
Drive frames at 50 Hz while a state is valid, and Stop at expiry or Wi-Fi loss.
The motion payload on UART is the existing absolute angle in degrees
and signed electrical motor percentage (negative is forward on this toy), with
no additional manual curve or 30% minimum applied by Renesas.

The camera reads Mode and discards its state on manual override. Auto is never
prepended to Drive; activation requires A or a separate explicit UDP request.
Each auto entry has an epoch so delayed states from a previous auto run are
rejected. The six-byte UART protocol itself has no session or sequence IDs.
The backend must stop sending states if video/CV stalls; the camera cannot infer
backend health while it keeps receiving new commands. Renesas independently
stops if the camera process fails to forward Drive. Keep actuator power off for
initial communication tests; a successful build does not verify hardware behavior.

## Camera board and API

The final camera firmware uses **RTSP with RTP/JPEG over UDP** for video,
HTTP for status/session setup, and the existing UDP latest-state control API.
The old HTTP video server and temporary number/raw-UART/performance probes are
removed. Camera video and actuator UART run independently; video never enables
motion. Firmware identifies itself as `bus-camera-rtp-1` in `/status`.

Target: GOOUUU ESP32-S3-CAM v1.5, N16R8 (16 MB flash, **8 MB OPI PSRAM**),
OV5640 DVP camera. The map matches Espressif's
[BOARD_ESP32S3_GOOUUU](https://github.com/espressif/esp32-camera/blob/master/examples/camera_example/main/camera_pinout.h):
XCLK=15, SCCB SDA/SCL=4/5, VSYNC=6, HREF=7, PCLK=13,
data D0..D7=11/9/8/10/12/18/17/16; no reset/power-down pins.
The seller's shifted-label image is misleading: GPIO4-7 belong to the camera.

Recorded four-wire harness:

| Camera pin | Wire / supply | Converter / Arduino |
|---|---|---|
| 5V | Red, regulated buck 5 V before headlight MOSFET | Camera power only; do not join buck 5 V to Arduino 5 V |
| GND | Black, shared ground | Camera, converter and Uno grounds common |
| GPIO21 TX | Yellow | LV4 / HV4 -> Uno D0 RX |
| GPIO47 RX | Blue | LV1 / HV1 <- Uno D1 TX |

Converter LV is supplied from Arduino 3.3 V and HV from Arduino 5 V. UART is
115200 baud, 8N1. GPIOs are 3.3 V only. Regulated 5 V enters the camera's 5V
power pin. Power off before soldering/reconnecting. For USB flashing, disconnect
camera buck 5 V first unless the board's USB power isolation has been verified.

Copy `firmware/camera/config.example.h` to `firmware/camera/config.h`, fill Wi-Fi
and UART pins. The real config is ignored by Git. Station mode only; no AP or
credentials in tracked source. Logs use the camera's TTL/CH340 USB at 115200,
with no USB-open wait. Startup logs confirm sensor PID, JPEG dimensions and
PSRAM; Wi-Fi logs print the current IP and RTSP URL. No repetitive debug logs.

```powershell
make camera-build
make camera PORT=COM7
make monitor PORT=COM7
```

`tools/camera.ps1` requires an explicit CH340 camera port and never invokes the
Uno bridge swap. It verifies the pinned
[MicroRtspServer Arduino library](https://github.com/Isuru-rana/esp32cam-rtsp-arduino-ide),
revision `f8934e73387a1faeb4ab40870434f2e1ae236300` (version 1.1.0), under ignored
`.build/deps/ESP32CameraRTSP`. The upstream MIT license stays in the checkout.
The library parses quantization tables and packetizes sensor JPEGs as RFC 2435
with paced RTP fragments. A PSRAM copy lets the camera framebuffer return
immediately. No JPEG re-encoding runs on the ESP. Sensor-native 640x480, target
10 fps. Wi-Fi modem sleep is disabled for low latency.

| Interface | Purpose |
|---|---|
| `rtsp://IP:8554/mjpeg/1` | RTP/JPEG video; choose UDP in player/backend |
| `GET http://IP/status` | Firmware ID, camera/RTP counters, mode, session/counter/epoch, remaining duration, light and Renesas feedback, compact Wi-Fi/memory/UART health |
| `POST http://IP/session` | Cancel local motion and rotate the UDP session; returns its numeric ID |
| `GET http://IP/rtsp` | Ready/clients, frames provided to packetizer, capture timing and source-frame age |
| `GET http://IP/snapshot` | One JPEG when no RTSP client is attached; otherwise 503 to avoid competing capture |
| UDP `IP:4210` | JSON latest-state commands and acknowledgment to sender |

The observed DHCP address is **192.168.8.209**; it can change. `bus-camera.local`
is advertised via mDNS where supported. APIs are for the trusted home LAN.
Browsers cannot display RTSP directly: the Node relay receives it through FFmpeg
and serves browser MJPEG. The camera's former `:81/stream` URL no longer exists.
Standalone viewing:

```sh
ffplay -rtsp_transport udp -fflags nobuffer -flags low_delay -analyzeduration 100000 -probesize 32768 -max_delay 100000 rtsp://192.168.8.209:8554/mjpeg/1
```

Backend startup: POST `/session`, read `/status`, explicitly enable auto (or
press Xbox A). Wait for `mode: auto`, then use that `epoch`. Every operation
shares a strictly increasing uint32 counter within its session. Example separate
UDP datagrams (substitute the real session and epoch):

```json
{"op":"auto","session":123,"counter":1}
{"op":"drive","session":123,"counter":2,"epoch":1,"angle":10,"speed":20,"duration_ms":500}
{"op":"lights","session":123,"counter":3,"on":true}
{"op":"stop","session":123,"counter":4}
```

Angle is -45..45 degrees; speed is -100..100 with **positive forward**, negated
by camera for the electrical UART convention. Duration is 1..2000 ms from
acceptance. A newer Drive replaces the old state; no queue. Old/duplicate packets
never extend duration. Stop is explicit (duration 0 is invalid). Drive is emitted
at 50 Hz until expiry, followed by Stop. Zero-speed arming precedes motion after
Stop/timeout. No manual curve/minimum is added, so low speed may not move motors.

Lights is an absolute boolean, independent of motion; it cannot arm, activate
Auto or refresh a Drive duration. Renesas feedback is `lights_on/lights_age_ms`.
Auto is explicit and idempotent, never automatically included in Drive. Counter
comparison handles wraparound; differences of half the uint32 range or more are
rejected. A new backend process should POST a new session. Boot and Wi-Fi
reconnect rotate it. Manual takeover clears the camera state, and a new auto
epoch rejects delayed movement from older auto runs. Backend must stop supplying
new states if its video/CV stalls; camera cannot infer health from valid commands.

UDP acknowledgment example: `{"accepted":true,"counter":2,"reason":"ok"}`.
This means camera acceptance, not physical actuation. Reasons include `session`,
`old-counter`, `epoch`, `not-auto`, `range`, `op`, `json` and `renesas-offline`.
`frames`/RTSP counters show frames supplied to packetization, not delivered
frames. `renesas_enabled`, speed and angle are software outputs, not sensors.
Status contains no physical distance or collision detection.

## Camera validation

Host tests cover timed replacement, sessions/counters/epochs, duplicate rejection,
neutral arming, manual takeover, lighting independence, watchdogs and rollover.
A standard FFmpeg RTP/UDP receiver decoded 588 640x480 frames in 60 seconds
(about 9.8 fps) on the final camera build. The browser relay delivered 144 frames
at 9.93 fps in a separate test. Renesas feedback stayed fresh,
with zero UART frame errors. Test hardware has been observed; motor/servo motion
and assembly reliability still require the user's physical test. No live motion
commands are sent by automated checks.

## Browser control on the Mac

A local static console at http://bus.local/ (also bus.lan) shows the camera and
provides a portrait iPhone console, joystick, reverse, lights and control release.
nginx proxies camera HTTP/MJPEG; a small loopback Node bridge translates the
normalized HTTP intent API into camera UDP commands, applying response curves
on the Mac. The time-based acceleration ramp is disabled by default. See [web/README.md](web/README.md)
for the [HTTP API reference](web/README.md#http-api), deployment, stopping
behavior and verification limits.

## Renesas LED matrix

The built-in matrix works from USB power alone, so motor/servo power can stay
off while checking the command path. It shows the actuator task's applied target:

- **Top three rows:** speed bar growing outward from the center. Forward fills
  the right half; reverse fills the left half. More LEDs means higher speed.
  Each half has six steps. Zero speed leaves the bar empty.
- **Bottom rows:** a two-column marker moves left/right with the absolute servo
  target, from -90 to +90 degrees. Fixed center dots provide a zero reference.
- **X:** stopped, unarmed or timed out; motors are off and servo pulses detached.

The active zero-speed display retains its angle marker and center references,
so it differs from the stopped X. Frames change only when the applied target
changes, with no animation delays. Matrix GPIO is internal; D13 stays reserved
for the servo. Arduino_LED_Matrix is bundled with the pinned Renesas core.
The matrix is initialized before the scheduler so the initial stopped X is
available before any controller input. A display of a target is not feedback
from the motor driver or servo and cannot prove they have power or moved.

## Shared serial protocol

Internal UART: **115200 baud, 8N1**. S3 GPIO43 TX -> Renesas console RX;
S3 GPIO44 RX <- Renesas console TX. Renesas uses `Serial`; S3 uses an explicit
hardware UART. S3's `Serial` is the separate USB interface. No extra wiring.
At S3 startup, GPIO9 (Renesas boot mode) and GPIO4 (Renesas reset) are configured
as outputs and driven HIGH, matching the stock bridge. This selects normal
Renesas boot and releases reset before USB/BLE initialization. Neither startup
nor motor control waits for a USB cable or monitor. GPIO9 here is the S3's
internal GPIO, not the Arduino D9 motor signal.

Each frame is six bytes, serialized explicitly without C struct padding:

| Bytes | Meaning |
|---|---|
| 0-1 | Header `A5 5A` |
| 2 | `1` = Drive, `2` = Stop, `5` = Lights, `7` = Auto; replies `3` = Applied, `4` = Stopped, `6` = LightsApplied, `8` = Mode |
| 3 | Signed angle -90..90 (two's complement) |
| 4 | Signed electrical speed -100..100 (two's complement) |
| 5 | CRC-8, polynomial `07`, initial zero, over bytes 0-4 |

Stop requires zero angle and speed; it disables outputs and detaches the servo.
Stop only affects motion when it comes from the selected source. Auto requires
zero angle/speed and requests auto, never toggles it. Mode uses byte 3 as
0=manual or 1=auto, with byte 4 zero; it reports selection, not backend health.
Drive with zero speed stops motors while keeping the commanded servo angle.
Lights uses byte 3 as an absolute off/on value (0/1), with byte 4 zero. S3
repeats it with each control frame, so dropped frames or a Renesas reset recover
the selected light state. Lights frames never arm motion or refresh its watchdog. Camera-source Lights
frames are now accepted too; repeated identical Xbox light frames do not overwrite
a camera selection. A changed Xbox light selection applies again.
LightsApplied reports D7's software state using the same 0/1 payload at 10 Hz.
Unknown commands, invalid ranges, bad checksums and incomplete frames are ignored.
The parser discards a partial frame after a 50 ms inter-byte gap and searches for
headers again after corruption. No text logs are sent on this UART.
The former two-byte increment experiment is removed.

Renesas sends a framed status every 100 ms, obtained from the actuator task:
Applied reports its software-applied angle/electrical speed; Stopped has zero
payload and means outputs are disabled. UART writes stay in the serial task so
acknowledgments cannot block the actuator watchdog. Replies received by Renesas
are ignored and cannot become control commands. S3 displays status once a second:

Renesas flushes each reply before reusing its buffer or writing another frame.
In core 1.6.0, UART `write()` waits only for TX_DATA_EMPTY rather than completion
of the final transmitted byte. Without the flush, back-to-back status replies
lost their checksum bytes on hardware. After the fix, camera Wi-Fi diagnostics
observed 63 valid replies over 2.25 seconds with no new rejected frames or UART
errors, and fresh motion-status feedback. This verifies the return path, not
physical motor motion or manual/auto takeover.

```text
Renesas: tx=drive angle=0 speed=0 ack=applied rx_ms=37 angle=0 speed=0
```

`tx=stop` means S3 is withholding control (check BLE link/worker and neutral
arming). `ack=missing` means no valid Renesas status arrived in the last second;
it does not prove which part of the UART/firmware path failed. `ack=stopped`
means Renesas reports its disabled state. `ack=applied` confirms software target
application, not driver presence, motor motion, servo power or actual angle.
These status commands require updating both chips.

## Build and upload (Windows)

Requires GNU Make, Windows PowerShell and Arduino CLI. The local setup provides
`make` as an alias for `mingw32-make`; restart the terminal after PATH changes.

```powershell
make setup
make s3-origin renesas s3-custom
make monitor
```

**Both chips must be updated** for the new protocol. Close the serial monitor
before flashing. Each Make target does only its own step:

| Target | Action |
|---|---|
| `s3-origin` | Restore Arduino's stock S3 bridge 0.6.1. |
| `renesas` | Build/upload `firmware/bus` through the stock bridge. |
| `s3-custom` | Build/flash `firmware/s3`. |

Goals run in the written order, even with `-j`; failure stops the sequence.
Repeated targets execute only once in Make. The flash script detects COM numbers
after firmware changes; connect only the target board. Stock is usually COM3,
custom S3 COM4 on the development machine. `make boards` lists ports.
`make monitor` detects the port; `make monitor PORT=COM4` overrides it.
Ctrl+C stops the monitor. USB flashing/monitoring belongs to S3 while custom
firmware is installed; Renesas uploads require the stock bridge again.

`make setup` installs Renesas core **1.6.0**, Espressif core **3.3.12**, Servo
**1.3.0**, XboxSeriesXControllerESP32_asukiaaa **1.1.3**, NimBLE-Arduino **2.5.2**,
XboxControllerNotificationParser **1.0.4**, and XboxSeriesXHIDReportBuilder_asukiaaa
**1.0.1**. The S3 target uses 8 MB flash, no PSRAM, DIO 80 MHz, hardware CDC/JTAG
and USB CDC on boot. The [Xbox library](https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32)
handles discovery/security; our input handler accepts only HID report ID 1,
input type 1, exact length 16, parsed successfully, before refreshing control.

Build and test without flashing:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/flash.ps1 -Action renesas -BuildOnly
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/flash.ps1 -Action s3-custom -BuildOnly
make test
```

Host tests cover CRC/framing/resynchronization, invalid ranges, arming,
dual-UART parser isolation, camera-only activation, neutral/disconnected Xbox
while auto drives, manual takeover, repeated A requests and blink timing,
held controls without repeated notifications, disconnection/worker-stall stops,
watchdog expiry with the serial task idle,
rollover, unchanged outputs, stale queued targets, steering/trigger curves and
fixed steering center and LED matrix speed/direction/angle/stopped patterns. These tests do
not establish physical motor/servo performance or confirm timer behavior on board.
The separate S3 startup test verifies boot/reset pin initialization before USB,
startup without an attached USB host, and Stop transmission before Xbox startup.
Flashing is performed by the user; use build-only checks unless asked to upload.
No GitHub Actions workflow is present.

Web headlights require updating both `firmware/bus` and `firmware/camera`; the
onboard `firmware/s3` does not need changes. See web/README.md.

## Recovery

The stock 0.6.1 image and official reboot utility are cached/downloaded under
`.build/recovery/stock`; the image's SHA-256 is checked. The flash script finds
esptool from the installed core or local cache. A custom merged image replaces
all S3 partitions/data, including bonding storage. Renesas bootloader and
eFuses are not rewritten.

If automatic S3 download entry fails, unplug USB, short **Download to GND** on the
ESP header while reconnecting, then remove the short. Follow Arduino's
[illustrated recovery instructions](https://support.arduino.cc/hc/en-us/articles/16379769332892-Restore-the-USB-connectivity-firmware-on-UNO-R4-WiFi-with-espflash).
The ROM downloader can normally replace a broken S3 sketch. No complete backup
of the original board image exists; the official recovery image is cached.

## Hardware notes

| Arduino signal | Connection |
|---|---|
| D5 / D6 | DRV8833 IN1 / IN2, motor 1 |
| D9 / D10 | DRV8833 IN3 / IN4, motor 2 |
| D13 | MG90S servo signal, 500-2500 microsecond pulses |
| D7 | Headlight XY-MOS TRIG/PWM, active HIGH |
| D0 / D1 | Camera ESP UART RX / TX through 3.3 V / 5 V level converter |
| GND | Common signal/power ground |

Recorded power: power bank -> verified 9 V PD trigger -> Arduino VIN and buck
inputs; 6 V buck -> motors/DRV8833; 5 V buck -> servo. Leave Arduino 5V unconnected
to the external chain. Never connect motor supply positive to Arduino 5V or VIN.
Verify polarity/capacity, current wiring and the actual DRV8833 nSLEEP/J1.
Keep motor/servo power disconnected during communication tests.

USB can remain connected while external power enters VIN within the board's
6-24 V input range. The recorded 9 V VIN arrangement is within that range; keep
the external 5 V buck off the Arduino 5V pin and keep grounds common. The
[official power schematic](https://docs.arduino.cc/resources/schematics/ABX00087-schematics.pdf)
shows the USB isolation diode and VIN regulator. USB alone does not power the
separately supplied DRV8833/motors or servo in this wiring arrangement.

## Development notes

This README is the single human/agent guide. Work on `master` unless a review
branch/PR is requested; commit/push only
when requested. Keep firmware plain and small. The browser UI lives separately under web/. Preserve actuator
pins. Never commit credentials, config.h, flash dumps or build output; keep local
artifacts under ignored `.build/`. Do not send actuation commands without
permission. Distinguish compilation, host tests and observed hardware results.
