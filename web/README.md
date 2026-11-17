# Bus LAN console and control service

http://bus.local/ or http://bus.lan/. Static portrait/landscape camera/cone UI; Node backend
owns all web/CV motor policy, camera sessions, UDP counters and acceleration.
The browser sends raw normalized intent. It does not map PWM, minimum output,
steering limits or nonlinear response. Firmware retains its independent range
checks, source priority, neutral arming and watchdogs. The final camera RTP firmware is required.

## Motor policy

`motion-policy.mjs` maps normalized steering/throttle (-1..1). Throttle has a 2%
deadzone, then desired motor magnitude is `30 + round(70 * travel²)`. Zero remains
zero. Steering uses the Xbox 12% deadzone and quadratic response, capped at ±45°;
the backend also owns the installed steering sign correction.

Three values are exposed:

- `desired_speed`: signed, shaped requested motor percentage.
- `allowed_speed`: unsigned acceleration allowance in percentage points.
- `real_speed` / integer `output_speed`: signed minimum magnitude of desired and
  allowed. These are software command values, NOT measured physical speed.

Allowance rises at 100 percentage points/second: from zero, 50 at 0.5 seconds
and 100 at 1 second. The 30% floor applies ONLY to desired speed; the allowance
starts at zero, so early ramp outputs can be below 30%. A low desired target caps
both output and allowance, preventing saved acceleration credit. Reductions are
immediate; the next increase ramps from the lower output. Desired speed can be
updated throughout the ramp without resetting an ongoing increase. Reversal
resets to zero and ramps in the new direction. Zero input resets allowance.
Steering is not ramped. A scheduler gap above 200 ms discards acceleration credit.
`BUS_ACCELERATION_MS` configures the zero-to-100 time (default 1000).

The backend emits bounded Drive frames at 20 Hz, with a 200 ms device lifetime.
Input is a latest-state lease, not a queue. Stop, input expiry, manual takeover,
failed commands or stale camera status reset the allowance and require neutral
before moving again. Missing/expired client input cannot be kept alive merely
because the backend remains running. Up to twelve acknowledgments can be in flight;
there is no queued movement backlog. Source/epoch/session rejection remains on
the camera/Renesas too.

## Clean HTTP API (same origin, LAN)

All POSTs require `Content-Type: application/json`. No cross-origin writes.

| Route | Method | Purpose |
| --- | --- | --- |
| `/api/control/acquire` | POST `{}` | Replace current lease, begin camera Auto setup, no Drive |
| `/api/control/status` | GET | Local control state, desired/allowed/output, readiness and camera diagnostics |
| `/api/control/input` | POST | Replace latest normalized steering/throttle intent |
| `/api/control/release` | POST | Release matching lease and Stop |
| `/api/status` | GET | Raw camera diagnostics only |
| `/api/snapshot` | GET | Sensor-native JPEG |
| `/camera/stream` | GET | Shared browser MJPEG, received from camera RTP/UDP through FFmpeg |
| `/api/video-status` | GET | Relay consumers and upstream state |

Acquire returns a string `lease` immediately. Setup happens asynchronously;
GET control/status until `ready: true`. `error` explains camera/UART issues.
Then send neutral once:

```json
{"lease":"returned-lease","sequence":1,"steering":0,"throttle":0,"duration_ms":300}
```

Then update desired intent, for example:

```json
{"lease":"returned-lease","sequence":2,"steering":0.5,"throttle":1,"duration_ms":300}
```

Repeat latest state before duration expires; browser targets 100 Hz (10 ms) and 300 ms.
`steering`/`throttle` are finite numbers -1..1, positive throttle forward, negative
reverse. Steering coordinates match the browser's current mapping. Exact servo
sign lives in the backend; use readouts for diagnostics. `duration_ms` is integer
1..1000. `sequence` is a strictly increasing safe integer across input/release
operations for this lease. No hardware counters, epochs or sessions are needed.
A neutral input centers and stops while keeping the lease; release detaches:

```json
{"lease":"returned-lease","sequence":3}
```

POST the last body to /api/control/release. A stopped/expired controller requires
neutral throttle before a subsequent moving input. An idle lease expires after
10 seconds without input; setup itself sends only Stop/Auto, never Drive.
Acquire is explicit takeover: it invalidates the previous client. Don't run
multiple writers against one lease. Successful input means backend intent was
accepted; output/ramp state updates separately and does not prove hardware motion.
The old `/api/command` and `/api/session` HTTP routes return **410**, so HTTP
clients cannot bypass the policy with raw motor commands. Camera LAN firmware
endpoints still exist for diagnostics and recovery; do not use direct UDP for
normal browser/CV control.

## Browser

Fullscreen camera with an upright 40° cone overlaid on the right; full steering
at zero/full throttle, reverse switch. Dragging takes control; touch alone doesn't. Neutral arming happens before
a held gesture applies. Release centers and sets desired speed zero immediately.
Control switch releases the lease. Hidden pages release video and motion; rotating clears the current gesture;
errors appear at the top. The cone remains interactive. Desktop arrows supported.
Readouts show backend output/angle, not locally calculated motor targets.

## Deployment

- Static root: web/public. nginx block: web/nginx-bus.conf.
- `node web/bridge.mjs`, loopback 127.0.0.1:8792, no npm dependencies. FFmpeg is required.
- Camera: 192.168.8.209, HTTP :80 / RTSP :8554 / control UDP :4210.
  Optional BUS_CAMERA_IP, BUS_RTSP_PORT, PORT and BUS_ACCELERATION_MS env vars. Set BUS_FFMPEG to the absolute ffmpeg executable path for a LaunchAgent.
- LaunchAgent: ~/Library/LaunchAgents/local.bus.web-bridge.plist.
  Logs: .build/web-bridge*.log. Login startup.
- nginx: ~/.config/nginx/nginx.conf; original backup nginx.conf.before-bus-web.
  Other virtual hosts preserved. Access localhost/home LAN only, no passwords.
- camera-relay.mjs launches one FFmpeg RTP/UDP receiver for the first consumer and stops it immediately
  when the last leaves. It uses stream copy to remux JPEG into browser MJPEG, with no image re-encoding. Slow consumers keep only the latest pending frame and
  disconnect after five seconds without draining. No idle video transfer/retries. A five-second complete-frame watchdog closes a stalled receiver; /api/video-status exposes an error.
  This does not power down ESP Wi-Fi/camera hardware.
- Camera view: -90° rotation and horizontal mirroring in CSS. Fill-screen crop
  by default; fit/fill icon shows the uncropped frame. Raw API unchanged.

## Validation

`cd web && npm test` (no install needed), or `node --test web/*.test.mjs`.

Pure tests cover response shaping, 0/50/100 timing, changes mid-ramp, immediate
reductions, low-target credit, reset/reverse and stalls. Controller tests cover
neutral arming, expiry even before the timer runs, lease ownership, manual
takeover and invalid input. UI tests assert raw intent rather than motor values.
HTTP/UDP/video tests use simulated transports, receiver processes and loopback clients, not the bus.
Read-only deployment checks are safe; no live acquire/input calls were made by
the agent for acceleration testing. iOS visual QA is unavailable in Work settings.
Real driving response still needs hardware verification.

UI cadence: a 10 ms timer targets 100 input updates/second, with one HTTP input
request in flight at a time. While active, each input response supplies backend
state; no redundant status GET precedes it. Slow browser/Mac networking can
still lower the actual rate. Input lifetime stays 300 ms; expiry resets output
and acceleration instead of performing a gradual decay. Backend outputs at
20 Hz with a 200 ms device lifetime independently of browser input requests.

Backend input handlers only replace the latest observed intent and deadline;
they never send Drive directly. A separate asynchronous 50 ms pump emits one
current acceleration-limited command per tick, without waiting for UDP ACKs.
No expired intent is replayed. Immediate Stop/release remains independent of
that cadence. The UI keeps one HTTP request in flight and samples the newest
gesture at the next free 10 ms slot, rather than queuing stale gestures. Actual
UI throughput is limited by browser scheduling/network RTT. Idle/setup status
reads are throttled to 250 ms. The 300 ms client lease is unchanged.

The UI handles neutral-required races automatically: if a moving request
finds the backend expired/disarmed since the last response, it sends a newer
zero-throttle/zero-steering input first. Only a still-held live gesture resumes
on a subsequent update, with acceleration restarted by the backend. Released
gestures are not replayed. The interlock and 300 ms expiry remain unchanged.

## Headlights

Header Lights switch uses `POST /api/control/lights` with `{"on":true}` or
`{"on":false}`. This is independent of the motion lease and never calls
Acquire, Auto or Drive, nor refreshes a motion deadline. Backend status includes
`lights.supported`, `lights.requested`, `lights.on` (fresh reported D7 state,
otherwise null). Requested state is not proof of LED current.

**Requires new camera AND Renesas firmware.** The onboard S3 sketch need not
change. Camera status advertises `lights_api:1`, `lights_on`, `lights_age_ms`.
An older camera is rejected with HTTP 501 and an explicit firmware-update message.
Do not flash the dummy-credential .build camera artifact; compile using your
real, ignored camera config.

The camera accepts session/counter-protected UDP `{op:"lights",session,counter,
on:true}` and forwards a Lights frame on its UART. Renesas applies camera or
controller light selections without touching the motion queues/watchdogs.
Repeated identical Xbox light frames do not undo a web selection; changing
Xbox B's selected on/off value applies that selection again. Auto-entry blink
retains its existing temporary behavior and restores the saved light state.

Validation: camera and Renesas firmware compile on the Windows toolchain.
Firmware host tests and all 27 web tests pass. The final camera firmware was
flashed and tested over Wi-Fi; physical light and motor checks remain separate.

## Deploying the final RTP version on the Mac

The API and nginx routes stay the same; browsers still open `/camera/stream`.
Only `camera-relay.mjs` changes the upstream to RTSP/RTP over UDP. The former
camera `http://192.168.8.209:81/stream` endpoint is removed in final firmware.

1. Install FFmpeg (`brew install ffmpeg`) and locate it with `command -v ffmpeg`.
2. Update the repository to the final camera/relay revision, or replace its
   `web` directory with the prepared `bus-rtp-web.zip` contents.
3. In `~/Library/LaunchAgents/local.bus.web-bridge.plist`, set the environment
   variable `BUS_FFMPEG` to that absolute path (usually `/opt/homebrew/bin/ffmpeg`).
   LaunchAgents may not inherit the interactive shell's Homebrew PATH.
4. Restart the existing service:

```sh
launchctl bootout gui/$(id -u) ~/Library/LaunchAgents/local.bus.web-bridge.plist
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/local.bus.web-bridge.plist
launchctl kickstart -k gui/$(id -u)/local.bus.web-bridge
```

5. Reload the console. `/api/video-status` must report `transport: "rtsp/udp"`;
   with video visible it should become connected. Hidden/closed viewers must
   leave zero consumers and stop FFmpeg. Check `.build/web-bridge*.log` for errors.

For a foreground smoke test, stop the existing service first to free port 8792,
then run `BUS_FFMPEG="$(command -v ffmpeg)" node web/bridge.mjs`. No browser/API
changes or npm packages are needed. Camera motor/servo tests are separate from
viewing video; loading the stream does not acquire a control lease.

The relay keeps bounded latest-frame buffering and low probe/analyze delays.
It cannot guarantee glass-to-glass latency or fix a congested Wi-Fi network.
Raw sensor capture timestamps are not carried through FFmpeg's MJPEG remux;
`/status` and `/rtsp` expose the camera's current source-frame age for diagnostics.
## Game-style overlay

The camera occupies the viewport behind all controls. HUD buttons provide lights,
video reconnect, fit/fill and motion control; telemetry and reverse are compact
overlays. The error banner has its own absolutely positioned anchor with bounded
height/scrolling and never participates in control layout. Cone geometry, raw
intent API, motor policy and timing are unchanged. Rotation clears the held gesture.
No new external library, asset requests or camera consumers are introduced.
Local browser access is disabled in Work settings, so visual/touch QA must be
checked on the user's device; existing browser-control regression tests pass.

### Reconnecting after power off

Connection failure, missing UART feedback, changed session/Auto epoch, or expired
ownership clears the browser lease and all motion/ramp state. Old asynchronous
replies are ignored. Reconnect never replays held input: slide again to acquire a
new session, which starts with neutral. The camera image retries every two
seconds while visible. Status polling never acquires control.
Only one process can listen on the bridge's localhost port 8792; LaunchAgent
manages that process. Reset reasons are logged with `[bus] control reset`.

Portrait layout keeps the camera full-screen, with a large cone at bottom right
and a large reverse switch on the left. Errors remain in a fixed top overlay.

Light requests return immediately with `lights.requested`, `pending`, and `error`.
The backend delivers absolute on/off settings asynchronously, serializing rapid
changes to the latest desired value. Repeated off is safe with fresh counters.
The UI shows requested state ahead of delayed physical feedback (`lights.on`);
delivery errors appear in the fixed error overlay. Pending means delivery to
the camera, not confirmation of physical illumination.

### iPhone console

The console is portrait-only, edge-to-edge, with safe-area spacing for controls.
Landscape pauses motion and video. Add to Home Screen from Safari to launch
without browser chrome. The standalone manifest requests portrait orientation;
iOS browser tabs still follow system rotation. Telemetry stays accessible but
visually hidden, and errors stay in a fixed overlay above the controls.

Transient UDP acknowledgement loss stops/disarms motion and resets acceleration
without revoking the lease. Fresh neutral is required; the browser discards held
input on the reported stop. A failed status request is tolerated while the last
status is under four seconds old. Stale status and actual session/epoch changes
still revoke ownership. Lease errors include the preceding reset reason.

Acceleration ramp is disabled by default (`BUS_ACCELERATION_MS=0`): the next
50 ms output tick sends the mapped desired speed immediately. The 30% minimum,
input response mapping, steering limits, neutral interlock and expiry remain.
A positive BUS_ACCELERATION_MS can re-enable the optional time ramp.

RTSP receiver packet reordering is disabled (`max_delay=0`,
`reorder_queue_size=0`) to avoid the previous 100 ms reorder wait.
Late/out-of-order Wi-Fi packets may cause frame loss instead of added latency.
