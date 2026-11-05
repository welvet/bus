# Xbox BLE on Uno R4 WiFi / Renesas

Experimental, serial-only adaptation of asukiaaa's ESP32 connect example.
The production bus firmware and Mac sender are unchanged. This sketch does not
send UDP, enable motors, attach a servo, or write controller rumble reports.
It holds the bus motor/servo signal pins low.

## Dependencies and build

Test target: arduino:renesas_uno:unor4wifi (installed core 1.6.0).
Libraries downloaded for this adaptation: ArduinoBLE 2.1.0,
XboxControllerNotificationParser 1.0.5 (Arduino_SpiNINA 0.0.2 dependency).

From the project root:

```sh
arduino-cli lib install 'ArduinoBLE@2.1.0' 'XboxControllerNotificationParser@1.0.5'
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi --build-path .build/xbox-renesas examples/xbox-renesas
```

Build attempted on 2026-11-05. It failed before source compilation: the installed
arm-none-eabi-g++ 7-2017q4 reports `bad CPU type in executable` on this arm64 Mac.
Compilation is NOT verified. No upload or physical test was performed.

## Hardware test, when ready

Uploading replaces the running bus sketch. Disconnect motor/servo power first.
Upload this sketch using Arduino IDE with Uno R4 WiFi selected, then open Serial
Monitor at 115200. Disconnect the controller from the Mac and hold its pairing
button until it flashes quickly. The sketch searches for the advertised name
`Xbox Wireless Controller`; optionally set controllerAddress to a known address.

Expected logs: scanning -> connecting -> discovering -> subscribed reports ->
waiting for input -> decoded buttons/sticks/triggers as you operate the controller.
Connection alone does not establish that input reception works. Diagnostics
separate connection, attribute discovery and subscription failures. No recent
input is reported as stale after one second; this is diagnostic only, not a
motor-control watchdog. Unchanged controls may not generate notifications.

## Porting choices and limits

- Replace NimBLE with ArduinoBLE on the RA4M1, using the stock ESP32-S3 bridge.
- Enable ArduinoBLE pairing support, discover HID service 1812 and report 2a4d.
- Select report reference descriptor 2908: ID 1, input type 1.
- Decode only exact 16-byte Xbox BLE input packets using the upstream parser.
- Poll BLE and rescan after disconnection; do not use ESP.restart().
- No battery reading, rumble, persistent bonding implementation, steering trim,
  actuator integration, or Wi-Fi concurrency in this proof of concept.

Important: BLE.setPairable(YES) is not equivalent to NimBLE's full security setup.
Xbox encryption/bonding and ArduinoBLE central interoperability need hardware
validation. A board connectivity-firmware update or library work might be needed;
none has been performed. Other report IDs/lengths are intentionally ignored.
The old Xbox Bluetooth Classic protocol is unsupported by the ESP32-S3 radio.

## Sources

Original example downloaded unchanged to ../../upstream/xbox-esp32/connect.ino:
https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32/blob/master/examples/connect/connect.ino

Connection implementation inspected:
https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32/blob/master/src/XboxSeriesXControllerESP32_asukiaaa.hpp

Parser:
https://github.com/asukiaaa/arduino-XboxControllerNotificationParser

ArduinoBLE:
https://github.com/arduino-libraries/ArduinoBLE

Downloaded 2026-11-05 from upstream master; upstream MIT license retained in
../../upstream/xbox-esp32/LICENSE. No upstream commit was pinned.

## Linux container build

From the project root, with Docker running:

```sh
docker build --platform linux/amd64 -f tools/docker-xbox/Dockerfile -t bus-xbox-renesas .
```

This compiles during image build and copies only the example directory into the
image. The amd64 Linux platform avoids the Mac compiler compatibility issue.
The image does not upload or execute firmware. Firmware still runs on the Uno R4.
Docker was unavailable on the development Mac on 2026-11-05, so this container
build has not yet been executed or verified.
