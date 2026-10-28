#include <WiFiS3.h>
#include <Servo.h>
#include "config.h"
#include "web.h"
#include "control.h"

WiFiServer server(80);
WiFiClient client;
Servo steering;
Control control;
const uint8_t INPUTS[] = {5, 6, 9, 10};
const int SERVO_MIN_US = 500, SERVO_MAX_US = 2500;
bool serving = false;
uint32_t lastJoin = 0, clientSince = 0;
char requestBuffer[2048];
size_t requestSize = 0;

void motor(uint8_t a, uint8_t b, int pwm) {
  analogWrite(a, 0); analogWrite(b, 0);
  if (pwm > 0) analogWrite(a, pwm);
  if (pwm < 0) analogWrite(b, -pwm);
}
void setMotor(int speed) {
  if (speed != control.speed) {
    int pwm = speed * 255 / 100;
    motor(5, 6, pwm); motor(9, 10, pwm);
    Serial.print("Both motors speed: "); Serial.println(speed);
  }
  control.motor(speed, millis());
}
void checkTimeout() {
  if (control.expire(millis())) {
    motor(5, 6, 0); motor(9, 10, 0);
    Serial.println("Browser timeout: motors stopped");
  }
}
void respond(int code, const char *type, const char *body) {
  client.print("HTTP/1.1 "); client.print(code);
  client.println(code == 200 ? " OK" : " Error");
  client.print("Content-Type: "); client.println(type);
  client.print("Content-Length: "); client.println(strlen(body));
  client.println("Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n");
  size_t length = strlen(body), sent = 0;
  while (sent < length && client.connected() && uint32_t(millis() - clientSince) < 1000) {
    checkTimeout();
    size_t chunk = length - sent;
    if (chunk > 256) chunk = 256;
    size_t written = client.write((const uint8_t *)body + sent, chunk);
    if (!written) break;
    sent += written;
  }
  client.stop(); requestSize = 0;
}
void route(const Request &request) {
  if (!strcmp(request.method, "GET")) {
    if (!strcmp(request.path, "/")) return respond(200, "text/html; charset=utf-8", INDEX_HTML);
    if (!strcmp(request.path, "/controls.js")) return respond(200, "text/javascript; charset=utf-8", CONTROLS_JS);
    if (!strcmp(request.path, "/api/info")) {
      char info[256];
      snprintf(info, sizeof(info),
        "{\"ip\":\"%s\",\"rssi\":%ld,\"uptime_ms\":%lu,\"speed\":%d,\"servo_angle\":%d,\"driver\":\"not_monitored\"}",
        WiFi.localIP().toString().c_str(), (long)WiFi.RSSI(), (unsigned long)millis(), control.speed, control.angle);
      return respond(200, "application/json", info);
    }
    return respond(404, "application/json", "{\"error\":\"Not found\"}");
  }
  if (strcmp(request.method, "POST")) return respond(405, "application/json", "{}");
  if (request.crossSite) return respond(403, "application/json", "{}");
  if (!request.json) return respond(415, "application/json", "{}");
  int value;
  if (!strcmp(request.path, "/api/motor")) {
    if (!integer(request.body, -100, 100, value)) return respond(400, "application/json", "{}");
    setMotor(value);
  } else if (!strcmp(request.path, "/api/servo")) {
    if (!integer(request.body, 0, 180, value)) return respond(400, "application/json", "{}");
    if (control.angle != value) {
      if (!steering.attached()) steering.attach(3, SERVO_MIN_US, SERVO_MAX_US);
      steering.write(value); control.angle = value;
      Serial.print("Servo D3 angle: "); Serial.println(value);
    }
  } else return respond(404, "application/json", "{}");
  respond(200, "application/json", "{\"ok\":true}");
}
void setup() {
  for (uint8_t pin : INPUTS) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  Serial.begin(115200);
  Serial.println("Joining WiFi; motors stopped");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastJoin = millis();
}
void loop() {
  checkTimeout();
  if (WiFi.status() != WL_CONNECTED) {
    setMotor(0);
    if (serving) { client.stop(); server.end(); serving = false; Serial.println("WiFi disconnected"); }
    if (uint32_t(millis() - lastJoin) >= 10000) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD); lastJoin = millis();
    }
    return;
  }
  if (!serving) {
    server.begin(); serving = true;
    Serial.print("Open http://"); Serial.println(WiFi.localIP());
  }
  if (!client) {
    client = server.available(); requestSize = 0; clientSince = millis();
    if (!client) return;
  }
  if (uint32_t(millis() - clientSince) >= 1000) { client.stop(); requestSize = 0; return; }
  // Bound each read pass so a slow or oversized client cannot bypass the watchdog.
  unsigned budget = 128;
  while (budget-- && client.available()) {
    if (requestSize == sizeof(requestBuffer) - 1) return respond(413, "application/json", "{}");
    int value = client.read();
    if (value <= 0) return respond(400, "application/json", "{}");
    requestBuffer[requestSize++] = char(value);
  }
  requestBuffer[requestSize] = 0;
  Request request;
  int result = parseRequest(requestBuffer, requestSize, request);
  if (result < 0) respond(400, "application/json", "{}");
  else if (result > 0) route(request);
}
