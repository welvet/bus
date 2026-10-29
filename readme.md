# bus

Uno R4 WiFi, one firmware file: `firmware/bus/bus.ino`. Wi-Fi connects once in
setup; no reconnect/check loop. Restart the board after connection failure/loss.
Two FreeRTOS tasks: network receives UDP and handles timeout; actuator receives
one latest `Target` struct and writes only changed outputs. No classes, HTTP/UI,
client, Python, ping or replies.

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

D5/D6 and D9/D10 drive motors, D3 drives servo (500–2500 µs; calibrate travel).
Servo starts disabled; timeout detaches pulses and releases torque. See
[hardware.md](hardware.md). Core 1.6.0 bundles Arduino_FreeRTOS; Servo 1.3.0 is
installed by setup. `make test` exercises this sketch with host mocks; firmware
compilation and physical operation need separate verification. Generated config.h
contains credentials; never share it or compiled firmware. Trusted LAN only.
