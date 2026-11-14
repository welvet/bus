# bus

Arduino Uno R4 WiFi: the onboard ESP32-S3 reads an Xbox BLE controller and sends
serial commands to the Renesas RA4M1, which controls both motors and the servo.
USB belongs to S3. Manual driving needs no backend, Wi-Fi or credentials.
The auto-control draft also accepts camera-ESP motion on a separate wired UART.

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
- `firmware/camera/`: separate GOOUUU ESP32-S3-CAM N16R8 firmware, MJPEG HTTP
  feed and latest-state UDP control. This is not the Uno's onboard S3 sketch.
- `firmware/bus/display.cpp`: built-in 12x8 LED matrix display of software-applied
  speed, servo angle and stopped state; its pixel layout is in `display.h`.
- `firmware/bus/bus.ino.old`: unchanged archive of the previous UDP firmware.

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

Target: GOOUUU ESP32-S3-CAM v1.5, N16R8 (16 MB flash, **8 MB OPI PSRAM**),
OV5640 DVP camera. The camera map is Espressif's
[BOARD_ESP32S3_GOOUUU](https://github.com/espressif/esp32-camera/blob/master/examples/camera_example/main/camera_pinout.h):
XCLK=15, SCCB SDA/SCL=4/5, VSYNC=6, HREF=7, PCLK=13,
data D0..D7=11/9/8/10/12/18/17/16, no reset/power-down pins. The earlier seller
pinout image has shifted labels: GPIO4-7 are **not free UART pins**.

Default camera UART: GPIO21 TX -> Uno D0 RX; GPIO47 RX <- Uno D1 TX, through
a 3.3 V / 5 V converter. Confirm actual wiring before uploading. No SD card is
used. Regulated external 5 V/GND can power the board; converter LV is 3.3 V,
HV is 5 V, and grounds are common. Keep external buck 5 V off Arduino's 5 V pin.

Recorded harness: red = buck 5 V before the headlight MOSFET, black = shared GND;
yellow = camera GPIO21 TX -> LV1 / HV1 -> Uno D0 RX;
blue = camera GPIO47 RX <- LV4 / HV4 <- Uno D1 TX.
Converter LV is supplied from Arduino 3.3 V, HV from Arduino 5 V; its GND joins
the common ground. Channels 1 and 4 are interchangeable with other matched
LV/HV channel pairs. The signal pins remain GPIO21/GPIO47 regardless of converter
channel numbering. External UART communication still requires verification.

Copy `firmware/camera/config.example.h` to `firmware/camera/config.h`, fill Wi-Fi
and UART pins. The real config is ignored by Git; never commit it. Firmware uses
station mode only. USB logs are on the camera's **TTL/CH340 USB** at 115200 baud,
with no USB-open wait. It logs sensor PID, probe JPEG dimensions and PSRAM to
confirm detection, then prints its IP after joining Wi-Fi.

```powershell
make camera-build
make camera PORT=COMx
make monitor PORT=COMx
```

`tools/camera.ps1` requires an explicit CH340 camera port and never invokes the
Uno bridge swap. Connect only the intended camera if several CH340 boards are
present. Its build output lives in `.build/camera/`. Reset/BOOT recovery uses
the camera board's own buttons. The Uno's `s3-custom` target is a different board.

| Endpoint | Purpose |
|---|---|
| `GET http://IP/status` | Camera PID/capture count, session/counter/epoch, mode, remaining duration, Renesas motion acknowledgment age/state |
| `POST http://IP/session` | Stop local motion, rotate session and invalidate all previous-session UDP packets; returns the new session |
| `GET http://IP/snapshot` | One VGA JPEG, or 503 if camera initialization/capture fails |
| `GET http://IP:81/stream` | One MJPEG stream client, target at most 10 fps, VGA JPEG quality 12 |
| UDP `IP:4210` | JSON latest-state commands with JSON acknowledgment to sender |

HTTP control/status and video run in separate server tasks; UART/UDP processing
never waits for a stream client. Capture uses two PSRAM buffers with latest-frame
selection, not an application frame backlog. TCP/client buffering can still add
latency. `X-Capture-Us` in each MJPEG part is the sensor-frame uptime timestamp.
No UDP video broadcast. Backend opens the stream and can relay it to viewers.
`bus-camera.local` is advertised by mDNS when supported on the client network;
otherwise use the IP printed on serial. These APIs are for the trusted local LAN.

Backend startup: POST `/session`, read `/status`, then explicitly enable auto
(or press Xbox A). Wait for `/status` to report `mode: auto` and take its `epoch`.
Send a state, using the returned numeric `session` and a strictly increasing
32-bit `counter` shared across all operations:

```json
{"op":"auto","session":123,"counter":1}
{"op":"drive","session":123,"counter":2,"epoch":1,"angle":10,"speed":20,"duration_ms":500}
{"op":"stop","session":123,"counter":3}
```

These are separate UDP datagrams, at most 512 bytes each. All fields are integers.
Angle is -45..45 degrees from center; speed is -100..100 with **positive forward**
(camera negates it for the electrical UART convention). Duration is 1..2000 ms,
measured from acceptance; a duplicate/older packet never extends it. Duration 0
is rejected: use Stop explicitly. No motor curve is added, so low percentages
may not overcome friction. Counter comparison supports wraparound, rejecting
differences of half the uint32 range or more. A new backend process should create
a new session rather than guess the old counter. Boot and Wi-Fi reconnect rotate
the session. A new auto epoch rejects delayed states from an earlier activation.

UDP acknowledgment: `{"accepted":true,"counter":2,"reason":"ok"}`. This means
accepted by the camera, not physically applied. Rejections give `session`,
`old-counter`, `epoch`, `not-auto`, `range`, `op`, `json` or `renesas-offline`.
Mode/actuation feedback is available separately through `/status`.
Auto is sent once per accepted explicit request; lost/rejected activation can
be retried with a newer counter. It never toggles or gets embedded in Drive.
The camera emits a zero-speed arming frame before motion after a Stop/expiry.
There is no automatic movement at boot, on stream connection or on Wi-Fi join.

Observed hardware verification: camera CH340 USB on COM7, ESP32-S3 with 16 MB
flash and 8 MB PSRAM, OV5640 PID 0x5640, successful 640x480 JPEG capture, Wi-Fi
join, HTTP status/snapshot and 29 MJPEG parts over a three-second read. UDP Stop
was acknowledged and its duplicate rejected. No Auto or moving command was sent.
Renesas status was absent during this check (`mode: unknown`); external UART
wiring/power and motion remain unverified. The observed IP was 192.168.8.209
(DHCP can change it).

## Browser control on the Mac

A local static console at http://bus.local/ (also bus.lan) shows the camera and
provides a landscape joystick, a persistent control switch and desktop arrow keys.
nginx proxies camera HTTP/MJPEG; a small loopback Node bridge translates the
normalized HTTP intent API into camera UDP commands, applying response curves
and the acceleration limit on the Mac. See [web/README.md](web/README.md)
for deployment, API bodies, stopping behavior and verification limits.

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
