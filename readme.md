# bus

Arduino Uno R4 WiFi + DRV8833 + two N20 motors. The Arduino joins your
2.4 GHz Wi-Fi and polls a Python server on your laptop over UDP.
Use Both motors to run both at the same speed/direction, or the individual rows
to run one motor. Click Forward/Reverse to run continuously until Stop. Mouse, touch,
and Space/Enter work. One speed slider (0–100%) sets both motors' speed. The UI shows Wi-Fi heartbeat status,
board IP and time since last contact. DRV8833 presence and motor motion cannot be
detected with the current wiring; driver status is shown as not monitored. Motors start stopped.
The browser refreshes commands while running. Stop, leaving/hiding the page, or
connection loss stops the motors. Reconnecting does not restart them automatically. If browser communication fails, commands expire after 0.6 seconds;
firmware stops after 350 ms without server replies.

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
With Node.js installed, `node tests/test_controls.cjs` checks start/stop controls.
Wi-Fi credentials and server IP are embedded during compilation in ignored
`firmware/bus/config.h`. `.env` is ignored but is **not** automatically loaded.
Never commit credentials or compiled firmware. Keep USB attached for initial power;
serial diagnostics use 115200 baud. Logs show the backend address, accepted motor
command changes (signed PWM from -255 to 255), and communication loss stops.
These confirm output commands, not physical motor motion. View with
`arduino-cli monitor --port YOUR_PORT --config baudrate=115200`;
close the monitor before uploading. There is no automatic motor run on boot.

See [hardware.md](hardware.md) before applying motor power. Wiring, jumper state,
and physical operation still need to be checked on the actual hardware.

## MG90S servo

Signal: D3; red: external regulated 5 V; brown/black: common supply/Arduino GND.
Run `make setup` again to install Servo 1.3.0, then `make install PORT=...` with
Wi-Fi/server variables set. Restart `make server` and refresh the UI.
Move the angle slider to set the servo immediately (0–180 nominal degrees). D3 starts sending pulses only
after the first angle command. The servo holds its last target during Stop or
network loss; motor Stop controls only the N20s. Remove power to release servo torque.
Targets persist in server memory until restart and may be reapplied on board restart.
The displayed target is commanded, not measured. Firmware uses a conservative
500–2500 microsecond pulse range, matching the 180° control specification in this
[MG90S manufacturer datasheet](https://www.tinytronics.nl/product_files/000263_Data_Sheet_of_MG90S_Analog_Servo_Motor.pdf).
[TowerPro's product page](https://towerpro.com.tw/product/mg90s-3/) does not specify
pulse endpoints. Actual travel varies by unit: calibrate SERVO_MIN_US/SERVO_MAX_US
in the sketch if necessary. Approach endpoints gradually; stop if it binds or buzzes.
Start at 90 with the horn/linkage detached. Firmware and server must both be updated.
