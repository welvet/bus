# Agent notes

Keep this project small: Python standard library, WiFiS3 from the UNO R4 core,
and Make. Board FQBN: `arduino:renesas_uno:unor4wifi`.

Preserve D5/D6 and D9/D10 motor wiring, stopped boot, expiring commands, and
communication watchdog. Never commit credentials, config.h, .venv or build output.
Run `make test`; compile firmware with placeholder environment values when changing it.
Do not assume hardware is connected or tested. Update readme.md/hardware.md when
interfaces or wiring change. Work on master as requested.
