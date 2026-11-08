#include <Arduino.h>
#include <BusProtocol.h>
#include "xbox.h"

HardwareSerial busSerial(1);
uint32_t sentAt = 0;
uint32_t pingAt = 0;
BusProtocol::Command lastCommand = BusProtocol::Command::Stop;
BusProtocol::Frame lastSent = BusProtocol::stop();
BusProtocol::Frame lastReply = {BusProtocol::Command::Stopped, 0, 0};
BusProtocol::Parser replies;
uint32_t replyAt = 0;
bool haveReply = false;

void sendCommand(const BusProtocol::Frame& command) {
  uint8_t bytes[BusProtocol::frameSize];
  if (BusProtocol::encode(command, bytes)) busSerial.write(bytes, sizeof(bytes));
  // Repeat an absolute light state, so a Renesas reset/lost frame self-recovers.
  BusProtocol::Frame lights = {BusProtocol::Command::Lights, xboxLights() ? 1 : 0, 0};
  if (BusProtocol::encode(lights, bytes)) busSerial.write(bytes, sizeof(bytes));
  lastCommand = command.command;
  lastSent = command;
  sentAt = millis();
}

void setup() {
  // Stock bridge startup states: boot the Renesas application and release reset.
  // Without these, a cold VIN-only start can leave Renesas in ROM/reset.
  pinMode(9, OUTPUT);
  pinMode(4, OUTPUT);
  digitalWrite(9, HIGH);
  digitalWrite(4, HIGH);
  Serial.begin(115200); // USB belongs to S3; no logs on the internal UART.
  Serial.setTxTimeoutMs(0); // A closed monitor must not delay control/Stop frames.
  Serial.println("Bus S3: initializing");
  busSerial.begin(BusProtocol::baud, SERIAL_8N1, 44, 43);
  sendCommand(BusProtocol::stop());
  xboxBegin();
}

void loop() {
  for (int i = 0; i < 64 && busSerial.available() > 0; ++i) {
    BusProtocol::Frame reply;
    if (replies.push(uint8_t(busSerial.read()), millis(), reply) &&
        (reply.command == BusProtocol::Command::Applied || reply.command == BusProtocol::Command::Stopped)) {
      lastReply = reply;
      replyAt = millis();
      haveReply = true;
    }
  }
  xboxPoll();
  BusProtocol::Frame command = xboxCommand();
  if (command.command != lastCommand ||
      uint32_t(millis() - sentAt) >= BusProtocol::sendIntervalMs) {
    sendCommand(command);
  }
  if (uint32_t(millis() - pingAt) >= 1000) {
    xboxPrintStatus();
    const char* ack = !haveReply || uint32_t(millis() - replyAt) >= BusProtocol::timeoutMs
        ? "missing" : lastReply.command == BusProtocol::Command::Applied ? "applied" : "stopped";
    Serial.printf("Renesas: tx=%s angle=%d speed=%d ack=%s rx_ms=%ld angle=%d speed=%d\n",
                  lastSent.command == BusProtocol::Command::Drive ? "drive" : "stop",
                  lastSent.angle, lastSent.speed, ack,
                  haveReply ? static_cast<long>(uint32_t(millis() - replyAt)) : -1L,
                  lastReply.angle, lastReply.speed);
    pingAt = millis();
  }
  delay(1);
}
