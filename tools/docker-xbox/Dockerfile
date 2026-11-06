FROM debian:bookworm-slim
ARG ARDUINO_CLI_VERSION=1.3.1
ARG TARGETARCH
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
RUN case "$TARGETARCH" in amd64) cli_arch=64bit ;; arm64) cli_arch=ARM64 ;; *) exit 1 ;; esac \
    && curl -fsSL "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_${cli_arch}.tar.gz" \
       | tar -xz -C /usr/local/bin arduino-cli
RUN arduino-cli core update-index \
    && arduino-cli core install arduino:renesas_uno@1.6.0 \
    && arduino-cli lib install 'ArduinoBLE@2.1.0' 'XboxControllerNotificationParser@1.0.4'
WORKDIR /src
COPY examples/xbox-renesas /src/examples/xbox-renesas
RUN arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi \
    --build-path /build/xbox-renesas /src/examples/xbox-renesas
