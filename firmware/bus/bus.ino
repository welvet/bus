#include <WiFiS3.h>
#include <WiFiUdp.h>
#include "config.h"

const uint8_t INPUTS[] = {5, 6, 9, 10};
WiFiUDP udp;
IPAddress server;
unsigned long lastPoll = 0, lastReply = 0, lastJoin = 0;
unsigned long sequence = 0;
bool joined = false;

void motor(uint8_t a, uint8_t b, int speed) {
  // Remove power before changing direction; coast when speed is zero.
  analogWrite(a, 0);
  analogWrite(b, 0);
  if (speed > 0) analogWrite(a, speed);
  if (speed < 0) analogWrite(b, -speed);
}
void stopMotors() {
  motor(5, 6, 0);
  motor(9, 10, 0);
}
void setup() {
  for (uint8_t pin : INPUTS) pinMode(pin, OUTPUT);
  stopMotors();
  Serial.begin(115200);
  server.fromString(SERVER_IP);
  // WiFi.begin can block; motors are stopped before every reconnect.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastJoin = millis();
}
void loop() {
  unsigned long now = millis();
  if (now - lastReply > 350) stopMotors();
  if (WiFi.status() != WL_CONNECTED) {
    stopMotors();
    if (joined) udp.stop();
    joined = false;
    if (now - lastJoin >= 10000) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      lastJoin = millis();
    }
    return;
  }
  if (!joined) {
    stopMotors();
    joined = udp.begin(8767) != 0;
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    if (!joined) return;
  }
  if (now - lastPoll >= 100) {
    lastPoll = now;
    sequence++;
    udp.beginPacket(server, SERVER_PORT);
    udp.print(sequence);
    udp.endPacket();
  }
  int size = udp.parsePacket();
  if (size > 0) {
    char packet[128], extra;
    unsigned long received;
    int m1, m2;
    bool sourceOK = udp.remoteIP() == server && udp.remotePort() == SERVER_PORT;
    int count = udp.read(packet, sizeof(packet) - 1);
    if (count < 0) return;
    packet[count] = 0;
    if (sourceOK && size < (int)sizeof(packet) &&
        sscanf(packet, "%lu %d %d %c", &received, &m1, &m2, &extra) == 3 &&
        received == sequence &&
        now - lastPoll < 100 && abs(m1) <= 255 && abs(m2) <= 255) {
      lastReply = now;
      motor(5, 6, m1);
      motor(9, 10, m2);
    }
  }
}
