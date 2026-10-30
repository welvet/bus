# Agent notes

One application source file: firmware/bus/bus.ino. Plain Target struct; no custom
classes/header logic. Wi-Fi connects once in setup; no reconnect/status polling.
UDP :5000 receives exactly [1, signed angle -90..90, signed speed -100..100].
Network task validates, converts to hardware Target, refreshes receipt time and
queues stop/detach after 1000ms without valid input. No separate heartbeat.
Actuator task blocks on the latest-target queue and updates only changed outputs.
Keep D5/D6 + D9/D10 shared motors and A1 servo. No UI/Python.
Native macOS Xbox controller sender lives in controller/main.c: plain C with
IOKit/CoreFoundation, no third-party packages. No address means dry run; do not
send physical actuation commands during testing without user authorization.

Core 1.6.0 includes Arduino_FreeRTOS; Servo 1.3.0. Test sketch with host mocks using
make test; compile with dummy credentials when tooling permits. Never commit
credentials/config.h or build output. Do not claim physical testing. Work on master; commit/push when requested.
