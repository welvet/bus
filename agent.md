# Agent notes

Keep this project small: Arduino WiFiS3 + Servo 1.3.0, embedded static HTML/JS,
Make and shell/awk build scripts. No Python or laptop server. FQBN:
`arduino:renesas_uno:unor4wifi`.

Preserve D5/D6 and D9/D10 wiring, shared motor speed, D3 servo, stopped boot and
motor command lease. Info/servo calls must not renew motor commands. Never commit
credentials, config.h/web.h or build output. Run `make test` (native C++ + Node),
then compile firmware in CI with dummy credentials. Hardware operation must be
verified separately. Update concise docs on interface changes. Work on master.
