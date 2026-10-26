# Hardware and connections

- Arduino Uno R4 WiFi; Mac USB supplies initial Arduino power/programming.
- DRV8833 dual H-bridge module; 2 × N20 6 V geared DC motors.
- Sandstrøm 10,000 mAh power bank, 18 W maximum; initial motor supply is 5 V.
- Later: 2 × MG90S servos, USB-C PD 9 V trigger and two buck converters.

## Control — 26 AWG

| Color | Arduino | DRV8833 |
|---|---|---|
| Yellow | D5 | IN1 |
| Green | D6 | IN2 |
| Yellow | D9 | IN3 |
| Green | D10 | IN4 |
| Black | GND | GND |

## Motors and initial power — 22 AWG

| DRV8833 | Connection |
|---|---|
| OUT1 / OUT2 | Motor 1 red / black |
| OUT3 / OUT4 | Motor 2 red / black |
| VCC | Power bank +5 V |
| GND | Power bank ground and Arduino ground |

Swapping a motor's leads reverses its direction. Never connect motor supply positive
to Arduino 5V or VIN. Bypass the breadboard for motor current; signals/grounds may
use it. Verify voltage/polarity and supply/stall-current capacity before connection.
Solder headers or wires into the module's currently unsoldered through-holes.

Module left, top to bottom: IN4, IN3, GND, VCC, IN2, IN1.
Right: OUT1, OUT2, OUT3, OUT4, ULT (fault output, unconnected initially).
The driver has no identification interface. Current wiring cannot detect its
presence or motor motion. ULT could support fault reporting with an additional
input wire and suitable pull-up after verifying the module circuit; an inactive
fault signal alone would not prove that a driver is connected.
**Verify J1 en/sleep on the actual board:** nSLEEP must be high to enable the chip;
do not guess the solder-jumper orientation. See the module schematic or measure it.
[TI datasheet](https://www.ti.com/lit/ds/symlink/drv8833.pdf).

First test: lift/unload motors, inspect common ground and J1, start the server,
then pulse each motor separately. A low PWM pulse may not overcome gearbox friction.
Disconnect motor power if wiring or motion is unexpected.

## Later

Power bank → PD trigger (verify bank supports 9 V) → parallel buck inputs:
6 V → DRV8833/motors; regulated 5 V → servos/Arduino. All grounds common.
Keep peak load within 18 W including converter losses. Verify Arduino 5 V pin powering
and USB backfeed protection before simultaneous USB/external power. Servos are not
implemented yet. Planned carrier: PLA+ in Plasticity with mounts, cable channels and
removable connectors. No physical wiring or testing is assumed complete.
