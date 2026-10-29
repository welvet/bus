ARDUINO_CLI ?= arduino-cli
CXX ?= c++
CC ?= cc
FQBN := arduino:renesas_uno:unor4wifi
export WIFI_SSID WIFI_PASSWORD

.PHONY: help setup boards config compile install monitor test clean controller controller-build controller-test
help:
	@echo "make setup | boards | compile | install PORT=... | monitor PORT=... | test | controller [BOARD_IP=...]"
setup:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install arduino:renesas_uno@1.6.0
	$(ARDUINO_CLI) lib install "Servo@1.3.0"
boards:
	$(ARDUINO_CLI) board list
config:
	@sh tools/configure.sh
compile: config
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --build-path .build firmware/bus
install: compile
	@test -n "$(PORT)" || (echo "Set PORT; run make boards to find it"; exit 1)
	$(ARDUINO_CLI) upload --fqbn $(FQBN) --port "$(PORT)" --input-dir .build firmware/bus
monitor:
	@test -n "$(PORT)" || (echo "Set PORT"; exit 1)
	$(ARDUINO_CLI) monitor --port "$(PORT)" --config baudrate=115200
test:
	@mkdir -p .build
	sed '/^#include /d' firmware/bus/bus.ino > .build/sketch.inc
	$(CXX) -std=c++11 -Wall -Wextra -Werror tests/firmware_test.cpp -o .build/control_test
	.build/control_test
clean:
	rm -rf .build firmware/bus/config.h firmware/bus/web.h

controller-build:
	@mkdir -p .build
	$(CC) -std=c11 -Wall -Wextra -Werror controller/main.c -framework IOKit -framework CoreFoundation -lm -o .build/controller
controller: controller-build
	.build/controller $(if $(BOARD_IP),--addr "$(BOARD_IP):5000",)
controller-test:
	@mkdir -p .build
	$(CC) -std=c11 -Wall -Wextra -Werror controller/test.c -framework IOKit -framework CoreFoundation -lm -o .build/controller_test
	.build/controller_test
