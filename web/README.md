# Bus web console and API

Open **http://bus.local/** or **http://bus.lan/** on the home LAN. nginx serves
an iPhone portrait console and proxies the API to a local Node service. The
backend owns response shaping, camera sessions and UDP counters. Firmware keeps
its own source priority, neutral arming, range checks and watchdogs.

## HTTP API

Use `http://bus.local` or `http://bus.lan` as the base URL. Every POST requires
`Content-Type: application/json`; cross-origin browser writes are rejected.
Access is restricted by nginx to localhost and the home LAN, without passwords.

| Method | Path | Purpose |
| --- | --- | --- |
| POST | `/api/control/acquire` | Body `{}`; replace current owner and start Auto setup |
| GET | `/api/control/status` | Read lease, readiness, output, lights and errors |
| POST | `/api/control/input` | Replace latest normalized steering/throttle intent |
| POST | `/api/control/release` | Stop and release the matching lease |
| POST | `/api/control/lights` | Absolute headlight setting, independent of motion |
| GET | `/camera/stream` | Shared browser MJPEG stream |
| GET | `/api/video-status` | Video consumers, connection and receiver error |
| GET | `/api/status` | Raw camera, UART and firmware diagnostics |
| GET | `/api/snapshot` | One sensor-native JPEG |

### Driving lifecycle

1. POST `{}` to `/api/control/acquire`. Save the returned string `lease`.
   Setup is asynchronous and sends Stop/Auto only, never Drive.
2. Poll `/api/control/status` until `ready: true`. Check `error` on failure.
3. POST neutral to `/api/control/input` before movement:

```json
{"lease":"YOUR_LEASE","sequence":1,"steering":0,"throttle":0,"duration_ms":300}
```

4. Send fresh intent regularly; every **50 ms** is suitable for a CV/client loop:

```json
{"lease":"YOUR_LEASE","sequence":2,"steering":0.5,"throttle":0.4,"duration_ms":300}
```

`steering` and `throttle` must be finite numbers from **-1 to +1**. Positive
throttle is forward; negative is reverse. Steering follows the console mapping,
with installed servo polarity handled by the backend. `duration_ms` is an integer
from 1 to 1000. Increase the safe-integer `sequence` for every input and release
in a lease. Clients do not manage hardware counters, sessions or epochs.

Send zero steering/throttle to center and stop while retaining ownership. To
stop and release, POST to `/api/control/release`:

```json
{"lease":"YOUR_LEASE","sequence":3}
```

Acquiring control replaces the previous owner. Use one writer per lease. Missing
fresh input expires motion; after stopping/expiry, neutral throttle is required
before moving again. Ownership expires after ten seconds without input.
Successful input means the backend accepted intent, not that hardware moved.
The old `/api/command` and `/api/session` routes return 410; normal clients should
use this API rather than direct firmware UDP commands.

### Timing and motor policy

Input requests replace the latest state; they never queue movement. An independent
**50 ms pump** sends current output with a **200 ms device lifetime**. The browser
targets one update every 10 ms with one HTTP request in flight and a 300 ms input
lifetime; actual throughput depends on browser scheduling and network latency.
CV clients must stop sending movement if their perception loop becomes stale.

The backend applies a 2% throttle deadzone, then a quadratic desired magnitude
of `30 + round(70 * travel²)`. Zero stays zero. Steering has a 12% deadzone,
quadratic response and ±45° limits. **The time-based acceleration ramp is disabled
by default** (`BUS_ACCELERATION_MS=0`); the next pump tick sends mapped speed.
A positive value optionally sets the zero-to-100 ramp time in milliseconds.

Status includes `desired_speed`, unsigned `allowed_speed`, `real_speed`, integer
`output_speed`, `angle`, `rise_ms`, `armed`, `ready`, `remaining_ms` and `error`.
These are software targets, not measured physical speed or steering position.

A transient UDP acknowledgment timeout stops/disarms motion without revoking
ownership. The browser discards held input on that stop. Failed HTTP status polls
are tolerated while cached status is less than four seconds old. Stale status,
missing UART feedback, changed session/Auto epoch or manual takeover clears
ownership and motion. Late replies cannot change a newer connection. Reconnect
requires fresh control and neutral; old movement is never replayed. Reset reasons
are timestamped in the service error log and included in lease errors.

### Lights

```sh
curl http://bus.local/api/control/lights \
  -H 'Content-Type: application/json' -d '{"on":false}'
```

`on` is an absolute boolean, not a toggle. Repeated `false` keeps lights off;
the backend supplies fresh counters. No lease, Auto or Drive is needed, and
light commands never extend a movement deadline.

Responses return immediately. The backend serializes delivery, retaining the
latest desired state during rapid changes. Status exposes:

- `lights.requested`: backend's desired state, or null after reset.
- `lights.pending`: command delivery is in progress.
- `lights.on`: fresh firmware-reported state, otherwise null.
- `lights.error`: asynchronous delivery error, otherwise null.
- `lights.supported`: whether the observed camera advertises the light API.

The button follows requested state immediately. Acceptance is not confirmation
of physical illumination. Unsupported firmware is reported through `lights.error`.
Both camera ESP32 and Renesas need light-support firmware; onboard Xbox S3 does
not. Changed Xbox light selections can override a web setting; repeated identical
Xbox selections do not. Auto-entry indication temporarily blinks the lights.

## Console and video

Portrait-only, safe-area-aware camera view with a small bottom-right cone and
unchanged thumb circle. Lights sit above R on the left. Dragging takes control;
release centers steering and stops. The power button releases ownership.
Landscape/hidden pages release motion and video. Errors use a fixed overlay;
telemetry remains accessible but visually hidden. Add to Home Screen from Safari
for standalone display. The manifest requests portrait; iOS may still rotate.

Video always fits without cropping, with -90° rotation and horizontal mirroring
in CSS. API JPEGs remain sensor-native. Camera reconnect retries every two seconds
while the page is visible in portrait.

One FFmpeg receiver reads `rtsp://192.168.8.209:8554/mjpeg/1` over RTP/UDP for all
viewers and remuxes JPEG to MJPEG without re-encoding. First viewer starts it;
last viewer leaving stops it. Slow viewers retain one latest pending frame and
are disconnected after five seconds without draining. A five-second complete-frame
watchdog closes stalled receivers. This does not power down the camera hardware.
The former camera port-81 HTTP stream is removed.

Packet reordering is disabled (`max_delay=0`, `reorder_queue_size=0`) to avoid the
previous configured 100 ms reorder wait. Late/out-of-order Wi-Fi packets may lose
frames instead. Frame rate alone is not a glass-to-glass latency measurement;
raw capture timestamps are not preserved through the FFmpeg remux.

## Deployment

- Static root: `web/public`; nginx host block: `web/nginx-bus.conf`.
- Backend: `node web/bridge.mjs`, listening on `127.0.0.1:8792`.
- No npm dependencies; a working FFmpeg executable is required.
- Camera: `192.168.8.209`, HTTP 80, RTSP 8554, control UDP 4210.
- Options: `BUS_CAMERA_IP`, `BUS_RTSP_PORT`, `PORT`, `BUS_ACCELERATION_MS`,
  `BUS_FFMPEG`. Set `BUS_FFMPEG` to an absolute executable path for launchd.
- LaunchAgent: `~/Library/LaunchAgents/local.bus.web-bridge.plist`.
- Logs: `.build/web-bridge.log` and `.build/web-bridge-error.log`.
- Live nginx config: `~/.config/nginx/nginx.conf`; preserve other virtual hosts.

For code-only updates:

```sh
launchctl kickstart -k gui/$(id -u)/local.bus.web-bridge
```

After changing the LaunchAgent environment:

```sh
launchctl bootout gui/$(id -u) ~/Library/LaunchAgents/local.bus.web-bridge.plist
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/local.bus.web-bridge.plist
```

Check `/api/video-status`: `transport` should be `rtsp/udp`; video becomes
connected while a viewer is present, then stops with zero consumers. Viewing
video does not acquire motion control. Do not start a second backend on port 8792.

## Validation

```sh
node --test web/*.test.mjs
```

Tests use simulated firmware/transports, receiver processes and localhost clients.
They cover shaping, optional ramp, expiry, ownership, reconnect, lights, UI input
and bounded shared video relay. They do not drive the bus. Physical controls,
iPhone layout and end-to-end video latency require device verification.

`node web/monitor-camera.mjs 600` records a ten-minute camera stability sample in
ignored `.build/` logs. It briefly acquires Auto then releases; it never sends
Drive, but it does replace the current control owner. Run it while not driving.
