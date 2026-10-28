# bus

Uno R4 WiFi serves its own page and HTTP API. No Python, laptop server, or server
IP configuration. Both N20 motors share one speed/direction; MG90S angle changes
as you slide. Open the board's IP from a browser on the same 2.4 GHz Wi-Fi/LAN.

```sh
make setup
export WIFI_SSID='your-wifi'
export WIFI_PASSWORD='your-password'
make boards
make install PORT=/dev/cu.usbmodemXXXX
make monitor PORT=/dev/cu.usbmodemXXXX
```

Serial (115200) prints `Open http://192.168...`. Open that address; reserve it in
your router if desired. Close the monitor before uploading. On Apple Silicon,
Arduino's bundled compiler may require Rosetta 2. Build tools: Make, POSIX shell,
awk, arduino-cli. `make test` additionally uses C++ and Node.js, with no packages.

Click Forward/Reverse to run both motors until Stop. Speed slider sets 0–100%.
Servo slider sends nominal 0–180° using 500–2500 µs; approach endpoints gradually
with linkage detached. Actual angle is not measured and varies by unit.
Motor commands refresh every 250 ms and expire after 1.5 seconds without refresh.
Leaving/hiding the page sends Stop; Wi-Fi loss stops motors. Servo stays at its
last position. Servo pulses begin only on its first command after boot.

## API

| Method | Path | Body / response |
|---|---|---|
| GET | `/` | Static UI embedded in firmware |
| GET | `/api/info` | IP, RSSI, uptime_ms, speed, servo_angle (-1 before first command), driver status |
| POST | `/api/motor` | JSON integer -100…100; negative reverses, 0 stops both |
| POST | `/api/servo` | JSON integer 0…180 |

POST requires `Content-Type: application/json`. `/api/motor` must be refreshed
within 1.5 seconds while running; info/servo requests do not refresh it. Example:
`curl -H 'Content-Type: application/json' -d '90' http://BOARD_IP/api/servo`.
Trusted LAN only: no authentication/TLS, no port forwarding. Use one control tab.
DRV8833 presence and actual movement cannot be detected with current wiring.

`make compile` builds; `make install` flashes. UI changes also require reflashing.
Generated config.h/web.h and build files are ignored. Credentials are embedded in
the firmware: never share compiled binaries. `.env` is not loaded automatically.
See [hardware.md](hardware.md) for wiring and power.
