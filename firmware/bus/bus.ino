#include <Arduino_FreeRTOS.h>
#include <WiFiS3.h>
#include <Servo.h>
#include "config.h"
#include "control.h"

QueueHandle_t targets;
TaskHandle_t networkHandle;
constexpr int SERVO_MIN_US = 500, SERVO_MAX_US = 2500;
const uint8_t INPUTS[] = {5, 6, 9, 10};

uint32_t nowMs() { return uint32_t(xTaskGetTickCount()) * portTICK_PERIOD_MS; }
void motor(uint8_t a, uint8_t b, int value) {
  analogWrite(a, 0); analogWrite(b, 0);
  if (value > 0) analogWrite(a, value);
  if (value < 0) analogWrite(b, -value);
}
void setMotors(int percent) {
  motor(5, 6, bus::pwm(percent)); motor(9, 10, bus::pwm(percent));
}
void publish(const bus::Session &session) { xQueueOverwrite(targets, &session.target); }

// Only this task touches outputs. No modem calls or serial logging in this path.
void actuatorTask(void *) {
  Servo servo;
  bus::Target target;
  bus::Gate gate;
  uint32_t session = 0, notified = 0;
  int speed = 0, angle = bus::SERVO_UNSET;
  for (;;) {
    xQueueReceive(targets, &target, pdMS_TO_TICKS(5));
    if (target.session != session) {
      setMotors(0); speed = 0;
      if (servo.attached()) { servo.detach(); digitalWrite(3, LOW); }
      angle = bus::SERVO_UNSET; session = target.session;
    }
    if (!gate.allow(target, nowMs())) {
      if (speed) { setMotors(0); speed = 0; }
      if (servo.attached()) { servo.detach(); digitalWrite(3, LOW); }
      angle = bus::SERVO_UNSET;
      if (target.active && notified != session) {
        notified = session;
        xTaskNotify(networkHandle, session, eSetValueWithOverwrite);
      }
      continue;
    }
    if (speed != target.motor) { setMotors(target.motor); speed = target.motor; }
    if (target.servo != bus::SERVO_UNSET && angle != target.servo) {
      // Set the pulse before enabling, avoiding an unintended center command.
      servo.write(bus::servoAngle(target.servo));
      if (!servo.attached()) {
        servo.attach(3, SERVO_MIN_US, SERVO_MAX_US);
        servo.write(bus::servoAngle(target.servo));
      }
      angle = target.servo;
    }
  }
}

void networkTask(void *) {
  WiFiServer server(bus::TCP_PORT);
  WiFiClient client;
  bus::Session session;
  uint32_t nextSession = 0, lastJoin = 0, lastWiFiCheck = 0;
  bool serving = false, wifiUp = false, firstJoin = true;
  for (;;) {
    uint32_t now = nowMs(), rejected = 0;
    bool watchdog = xTaskNotifyWait(0, UINT32_MAX, &rejected, 0) == pdTRUE &&
                    rejected == session.target.session;
    if (session.target.active && (watchdog || bus::expired(session.target, now))) {
      session.close(); publish(session); // Stop before any potentially blocking modem call.
      client.stop();
      Serial.println("Command timeout: connection closed; outputs disabled");
    }
    bool checkLink = firstJoin || uint32_t(now - lastWiFiCheck) >= 100;
    if (checkLink) {
      lastWiFiCheck = now;
      wifiUp = WiFi.status() == WL_CONNECTED;
    }
    if (!wifiUp) {
      if (session.target.active) { session.close(); publish(session); client.stop(); }
      if (serving) { server.end(); serving = false; Serial.println("WiFi disconnected"); }
      if (firstJoin || uint32_t(now - lastJoin) >= 10000) {
        firstJoin = false; lastJoin = now;
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (!serving) {
      server.begin(); serving = true;
      Serial.print("TCP listening: "); Serial.print(WiFi.localIP());
      Serial.print(':'); Serial.println(bus::TCP_PORT);
    }
    if (!session.target.active) {
      client = server.accept();
      if (client) {
        if (++nextSession == 0) ++nextSession;
        session.open(nextSession, nowMs()); publish(session);
        Serial.println("TCP controller connected");
      }
    } else {
      // One client owns outputs; explicitly refuse additional controllers.
      if (checkLink) { WiFiClient extra = server.accept(); if (extra) extra.stop(); }
      int available = client.available();
      if (available <= 0 && checkLink && !client.connected()) {
        session.close(); publish(session); client.stop();
        Serial.println("TCP controller disconnected; outputs disabled");
      } else {
        if (available > 0) {
          uint8_t bytes[64];
          size_t size = available < int(sizeof(bytes)) ? size_t(available) : sizeof(bytes);
          int count = client.read(bytes, size);
          for (int i = 0; i < count; ++i) {
            bus::Result result = session.feed(bytes[i], nowMs());
            if (result == bus::Result::Invalid) {
              publish(session); client.stop();
              Serial.println("Invalid/stale TCP message: connection closed");
              break;
            }
            if (result == bus::Result::Command) publish(session);

          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void setup() {
  for (uint8_t pin : INPUTS) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  pinMode(3, OUTPUT); digitalWrite(3, LOW);
  Serial.begin(115200);
  // Use the bundled core's scheduler explicitly, avoiding an extra sketch task.
  targets = xQueueCreate(1, sizeof(bus::Target));
  if (!targets || xTaskCreate(networkTask, "network", 1024, nullptr, 1, &networkHandle) != pdPASS ||
      xTaskCreate(actuatorTask, "actuators", 384, nullptr, 2, nullptr) != pdPASS) {
    Serial.println("Task allocation failed; outputs remain off");
    for (;;) {}
  }
  vTaskStartScheduler();
  Serial.println("Scheduler failed; outputs remain off");
  for (;;) {}
}
void loop() {}
