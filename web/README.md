# Bus LAN console and control service

http://bus.local/ or http://bus.lan/. Static landscape camera/cone UI; Node backend
owns all web/CV motor policy, camera sessions, UDP counters and acceleration.
The browser sends raw normalized intent. It does not map PWM, minimum output,
steering limits or nonlinear response. Firmware retains its independent range
checks, source priority, neutral arming and watchdogs. No firmware flash needed.

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
| `/camera/stream` | GET | On-demand shared sensor-native MJPEG |
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

Image left, upright 40° cone right; full steering at zero/full throttle, reverse
switch. Dragging takes control; touch alone doesn't. Neutral arming happens before
a held gesture applies. Release centers and sets desired speed zero immediately.
Control switch releases the lease. Hidden/portrait pages release video and motion;
errors appear at the top. The cone remains interactive. Desktop arrows supported.
Readouts show backend output/angle, not locally calculated motor targets.

## Deployment

- Static root: web/public. nginx block: web/nginx-bus.conf.
- `node web/bridge.mjs`, loopback 127.0.0.1:8792, no npm dependencies.
- Camera: 192.168.8.209, HTTP :80 / video :81 / UDP :4210.
  Optional BUS_CAMERA_IP, PORT and BUS_ACCELERATION_MS env vars.
- LaunchAgent: ~/Library/LaunchAgents/local.bus.web-bridge.plist.
  Logs: .build/web-bridge*.log. Login startup.
- nginx: ~/.config/nginx/nginx.conf; original backup nginx.conf.before-bus-web.
  Other virtual hosts preserved. Access localhost/home LAN only, no passwords.
- camera-relay.mjs opens one upstream for the first consumer and aborts immediately
  when the last leaves. Slow consumers keep only the latest pending frame and
  disconnect after five seconds without draining. No idle video transfer/retries.
  This does not power down ESP Wi-Fi/camera hardware.
- Camera view: -90° rotation and horizontal mirroring in CSS. Raw API unchanged.

## Validation

`cd web && npm test` (no install needed), or `node --test web/*.test.mjs`.

Pure tests cover response shaping, 0/50/100 timing, changes mid-ramp, immediate
reductions, low-target credit, reset/reverse and stalls. Controller tests cover
neutral arming, expiry even before the timer runs, lease ownership, manual
takeover and invalid input. UI tests assert raw intent rather than motor values.
HTTP/UDP/video tests use simulated transports/loopback clients, not the bus.
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
real, ignored camera config. No board was flashed by the agent.

The camera accepts session/counter-protected UDP `{op:"lights",session,counter,
on:true}` and forwards a Lights frame on its UART. Renesas applies camera or
controller light selections without touching the motion queues/watchdogs.
Repeated identical Xbox light frames do not undo a web selection; changing
Xbox B's selected on/off value applies that selection again. Auto-entry blink
retains its existing temporary behavior and restores the saved light state.

Lights verification: camera firmware compiles for ESP32-S3 N16R8/OPI PSRAM
using dummy credentials in ignored .build staging. Renesas protocol/actuator
and camera motion host tests pass. Full Renesas compilation remains blocked
by the installed Intel compiler (`bad CPU type in executable`) on this Mac.
No firmware uploads or physical light tests were performed.
