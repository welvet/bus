# Agent notes

Keep this project small: Uno R4 WiFi core 1.6.0 with bundled Arduino_FreeRTOS,
WiFiS3 and Servo 1.3.0; Make + shell/awk. No HTTP/UI/client/Python.
FQBN: `arduino:renesas_uno:unor4wifi`.

Preserve D5/D6 + D9/D10 shared motor speed, D3 servo, two-byte signed protocol,
250 ms ping interval and 750 ms heartbeat timeout. Only network task uses WiFi;
only actuator task controls outputs. Latest-target queue must not backlog motion.
Disconnect/timeout disables motors and detaches servo; stale sessions stay off.
Run `make test` and firmware compilation in CI. Never commit credentials/config.h
or build output. Do not assume hardware testing or measured latency. Keep docs
short and wire format explicit. Work on master as requested.
