PYTHON ?= python3
ARDUINO_CLI ?= arduino-cli
FQBN := arduino:renesas_uno:unor4wifi
export WIFI_SSID WIFI_PASSWORD SERVER_IP SERVER_PORT

.PHONY: help venv setup boards config compile install server test clean
help:
	@echo "make venv | setup | boards | compile | install PORT=/dev/cu.usbmodem... | server | test"
venv:
	$(PYTHON) -m venv .venv
setup:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install arduino:renesas_uno
boards:
	$(ARDUINO_CLI) board list
config:
	@$(PYTHON) tools/configure.py
compile: config
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --build-path .build firmware/bus
install: compile
	@test -n "$(PORT)" || (echo "Set PORT; run make boards to find it"; exit 1)
	$(ARDUINO_CLI) upload --fqbn $(FQBN) --port "$(PORT)" --input-dir .build firmware/bus
server: .venv/bin/python
	.venv/bin/python server.py
.venv/bin/python:
	$(MAKE) venv
test:
	$(PYTHON) -m unittest discover -s tests -v
clean:
	rm -rf .build firmware/bus/config.h
