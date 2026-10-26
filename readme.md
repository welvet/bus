# bus

Arduino Uno R4 WiFi + DRV8833 + two N20 motors. The Arduino joins your
2.4 GHz Wi-Fi and polls a Python server on your laptop over UDP.
Browser buttons pulse either motor independently. Motors start stopped;
commands expire after 0.6 seconds and firmware stops after 350 ms without replies.

## Run

On Apple Silicon Macs, Arduino’s bundled compiler currently requires Rosetta 2
(`softwareupdate --install-rosetta`, follow Apple’s license prompt).

Install Python 3, Make and [arduino-cli](https://arduino.github.io/arduino-cli/latest/installation/), then:

```sh
make venv
make setup
make boards
export WIFI_SSID='your-wifi'
export WIFI_PASSWORD='your-password'
export SERVER_IP=192.168.1.50  # laptop LAN IPv4; reserve it in your router
make install PORT=/dev/cu.usbmodemXXXX
make server
```

Open **http://localhost:8000** on that laptop. No Python dependencies are needed. `SERVER_PORT` defaults to 8766
(UDP); `HTTP_PORT` defaults to 8000. Allow Python UDP traffic through the laptop
firewall; both devices must share a LAN without client isolation. HTTP binds
only to localhost. This is a trusted-LAN prototype; UDP commands are unauthenticated and unencrypted.

`make compile` builds without uploading; `make test` checks server/config behavior.
Wi-Fi credentials and server IP are embedded during compilation in ignored
`firmware/bus/config.h`. `.env` is ignored but is **not** automatically loaded.
Never commit credentials or compiled firmware. Keep USB attached for initial power;
serial diagnostics use 115200 baud. There is no automatic motor run on boot.

See [hardware.md](hardware.md) before applying motor power. Wiring, jumper state,
and physical operation still need to be checked on the actual hardware.
