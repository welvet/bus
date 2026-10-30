# bus

Uno R4 WiFi, one firmware file: `firmware/bus/bus.ino`. Wi-Fi connects once in
setup; no reconnect/check loop. Restart the board after connection failure/loss.
Two FreeRTOS tasks: network receives UDP and handles timeout; actuator receives
one latest `Target` struct and writes only changed outputs. No classes, HTTP/UI,
Python, ping or replies. A native Mac Xbox sender is available below.

```sh
make setup
export WIFI_SSID='your-wifi'
export WIFI_PASSWORD='your-password'
make boards
make install PORT=/dev/cu.usbmodemXXXX
make monitor PORT=/dev/cu.usbmodemXXXX
```

Serial prints `UDP listening: IP:5000`. Each datagram is exactly **3 bytes**:

| Byte | Meaning |
|---|---|
| 1 | Fixed type `1` |
| 2 | Signed angle -90…90°; zero centered |
| 3 | Signed speed -100…100%; both motors, negative reverses |

Signed bytes use two's complement. Hex `01 00 64` means center/full forward;
`01 A6 9C` means nominal 0°/full reverse. Repeat valid datagrams less than one
second apart, even when unchanged. Malformed datagrams are ignored and do not
refresh the timeout. At one second without valid input, network queues motor
stop/servo detach once. A later valid datagram can enable outputs again. There
is no UDP connection or disconnect event; Wi-Fi loss stops new input, then timeout
stops outputs. Timeout handling runs in the network task, not an independent
watchdog. UDP does not guarantee delivery or ordering; use one sender.

Internal `Target` contains signed PWM -255…255, servo angle 0…180 and enabled flag;
it contains no wire type, framing, session or timestamp. Queue length is one.
Actuator blocks on the queue; network sleeps 5 ms after an empty UDP poll.

D5/D6 and D9/D10 drive motors, D13 drives servo (500–2500 µs; calibrate travel).
Servo starts disabled; timeout detaches pulses and releases torque. See
[hardware.md](hardware.md). Core 1.6.0 bundles Arduino_FreeRTOS; Servo 1.3.0 is
installed by setup. `make test` exercises this sketch with host mocks; firmware
compilation and physical operation need separate verification. Generated config.h
contains credentials; never share it or compiled firmware. Trusted LAN only.

## Xbox controller on Mac

One plain C application file, `controller/main.c`, reads the paired Xbox through
native IOKit HID callbacks. No Go, browser, third-party libraries or additional
driver. Requires Apple's Command Line Tools (`xcode-select --install`).

```sh
make controller                         # dry run: print values, send nothing
make controller BOARD_IP=192.168.8.142   # live UDP control, once ready
make controller-test
```

Left stick X steers (-90 to 90). Right trigger drives forward; left drives reverse.
The sender reverses the wire speed sign to match the current motor mounting.
The stronger trigger wins at its own pressure (RT 80%, LT 30% gives +80%); equal
pressures stop. Trigger inputs at or below 2% count as released. Center steering
and release both triggers once after connection before transmission starts.
Steering has a rescaled 12% deadzone. The sender repeats all three bytes
at approximately 50 Hz (every 20 ms) even without input changes. On controller loss/Ctrl-C it
sends zero motor speed once and stops transmitting; board timeout then detaches
the servo. No address means no UDP socket. Logs do not confirm board reception:
UDP has no reply. The native mapping was checked against Xbox BLE product 0x0B13.

Advanced: `.build/controller --addr BOARD_IP:5000 --deadzone 0.12`.
Use `--duration 10s` for a timed test. Keep other controller apps closed when driving.

### Run in background (macOS)

`make controller-install BOARD_IP=192.168.8.142` installs/starts a user LaunchAgent.
It runs at login and restarts automatically after exit, with a 5-second restart
throttle. Center steering and release triggers after each restart to enable control.
The installed binary lives in `~/Library/Application Support/Bus`, so `make clean`
does not remove it. Re-run install after controller code changes to update it.

Use `make controller-stop`, `make controller-start`, or `make controller-status`.
Stop unloads it until explicitly started or the next login. Logs are in
`~/Library/Logs/Bus/controller.log` and `controller-error.log`. The service uses
`--quiet`: connection/error logs only, avoiding continuous joystick-value logs.
Do not run a foreground sender at the same time as the service.

### Servo center calibration

Hold the left stick until the wheels/servo are straight, press **Y** (Xbox top
face button, equivalent to PlayStation Triangle), then release the stick.
`make controller-calibrate` does the same from the command line. Holding Y does
not repeatedly recalibrate; each press captures once. The current servo target
becomes its new center and is held while the stick returns to neutral. Trim is
stored at `~/Library/Application Support/Bus/steering-trim` and survives restarts.
Press **B** to reset the saved center to **0°**; release the stick to resume steering.
Steering uses a quadratic curve with travel limits relative to the calibrated
center: **center − 71° to center + 71°**, clipped to the servo's −90°/+90° range.
For example, center +10° gives endpoints −61° and +81°. Half usable stick travel
gives 25% of the available steering travel on that side.
Triggers are unaffected. Calibration stays on the Mac; no firmware change.

Trigger motor output: released (up to 2%) stops; above that, full trigger travel
uses a quadratic curve from a shared 15% minimum running output to 100% in
either direction. Half usable travel gives 36% output for finer slow control.
