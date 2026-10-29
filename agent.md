# Agent notes

Keep this project small: Uno R4 WiFi core 1.6.0 with Arduino_FreeRTOS, WiFiS3
and Servo 1.3.0; Make + shell/awk. No HTTP/UI/client/Python.
FQBN: `arduino:renesas_uno:unor4wifi`.

Preserve D5/D6 + D9/D10 shared motor speed and D3 servo. TCP message is exactly
[1, signed angle -90..90, signed speed -100..100]. No ping/reply. Every complete
valid message renews the 1000ms timeout, including identical targets. Partial or
invalid messages must not renew it. Only changed outputs are written.
Only network task uses WiFi; only actuator task controls outputs. Latest-target
queue must not backlog motion. Disconnect/timeout stops motors and detaches servo;
stale sessions stay off. Run `make test` and firmware CI. Never commit credentials,
config.h or build output. Do not claim hardware testing or measured latency.
Keep docs short and protocol explicit. Work on master as requested.
