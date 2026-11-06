.DEFAULT_GOAL := help
.NOTPARALLEL:
ARDUINO_CLI ?= arduino-cli
POWERSHELL ?= powershell.exe
CXX ?= c++
RENESAS_SKETCH ?= firmware/bus
S3_SKETCH ?= firmware/s3
S3_FQBN ?= esp32:esp32:esp32s3:FlashSize=8M,PSRAM=disabled,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=dio

.PHONY: help setup boards s3-origin renesas s3-custom monitor test
help:
	@echo "make s3-origin renesas s3-custom"
	@echo "make setup | boards | monitor [PORT=COM4] | test"
setup:
	$(ARDUINO_CLI) core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
	$(ARDUINO_CLI) core install arduino:renesas_uno@1.6.0
	$(ARDUINO_CLI) core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
	$(ARDUINO_CLI) lib install "Servo@1.3.0"
	$(ARDUINO_CLI) lib install --no-deps "XboxSeriesXControllerESP32_asukiaaa@1.1.3" "NimBLE-Arduino@2.5.2" "XboxControllerNotificationParser@1.0.4" "XboxSeriesXHIDReportBuilder_asukiaaa@1.0.1"
boards:
	$(ARDUINO_CLI) board list
s3-origin:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File tools/flash.ps1 -Action s3-origin -ArduinoCli "$(ARDUINO_CLI)"
renesas:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File tools/flash.ps1 -Action renesas -ArduinoCli "$(ARDUINO_CLI)" -Sketch "$(RENESAS_SKETCH)"
s3-custom:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File tools/flash.ps1 -Action s3-custom -ArduinoCli "$(ARDUINO_CLI)" -Sketch "$(S3_SKETCH)" -S3Fqbn "$(S3_FQBN)"
monitor:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File tools/flash.ps1 -Action monitor -ArduinoCli "$(ARDUINO_CLI)" -Port "$(PORT)"
test:
	$(POWERSHELL) -NoProfile -Command "New-Item -ItemType Directory -Force .build | Out-Null"
	$(CXX) -std=c++11 -Wall -Wextra -Werror -static -Ifirmware/shared -Itests/mocks tests/protocol_test.cpp -o .build/protocol_test.exe
	.build/protocol_test.exe
	$(CXX) -std=c++11 -Wall -Wextra -Werror -static -Ifirmware/shared -Itests/mocks tests/s3_startup_test.cpp -o .build/s3_startup_test.exe
	.build/s3_startup_test.exe
