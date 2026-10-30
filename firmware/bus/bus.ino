#include <Arduino_FreeRTOS.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <Servo.h>
#include "config.h"

// Internal actuator command: hardware values, not the three-byte wire message.
struct Target {
  int motorPwm;
  int servoAngle;
  bool enabled;
};

QueueHandle_t targets;
WiFiUDP udp;

void setMotors(int pwm) {
  analogWrite(5, 0); analogWrite(6, 0);
  analogWrite(9, 0); analogWrite(10, 0);
  if (pwm > 0) { analogWrite(5, pwm); analogWrite(9, pwm); }
  if (pwm < 0) { analogWrite(6, -pwm); analogWrite(10, -pwm); }
}

void actuatorTask(void *) {
  Servo servo;
  Target target;
  int pwm = 0, angle = -1;
  for (;;) {
    xQueueReceive(targets, &target, portMAX_DELAY);
    if (!target.enabled) {
      if (pwm != 0) setMotors(0);
      if (servo.attached()) { servo.detach(); digitalWrite(A1, LOW); }
      pwm = 0; angle = -1;
      continue;
    }
    if (target.motorPwm != pwm) {
      setMotors(target.motorPwm);
      pwm = target.motorPwm;
    }
    if (target.servoAngle != angle) {
      if (!servo.attached()) servo.attach(A1, 500, 2500);
      servo.write(target.servoAngle);
      angle = target.servoAngle;
    }
  }
}

void networkTask(void *) {
  uint32_t receivedAt = 0;
  bool active = false;
  for (;;) {
    if (active && uint32_t(millis() - receivedAt) >= 1000) {
      Target stop = {0, 90, false};
      xQueueOverwrite(targets, &stop);
      active = false;
    }

    int size = udp.parsePacket();
    if (size <= 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (size != 3) { udp.flush(); continue; }
    uint8_t message[3];
    if (udp.read(message, sizeof(message)) != 3 || message[0] != 1) continue;
    int angle = message[1] < 128 ? message[1] : int(message[1]) - 256;
    int speed = message[2] < 128 ? message[2] : int(message[2]) - 256;
    if (angle < -90 || angle > 90 || speed < -100 || speed > 100) continue;

    Target target = {speed * 255 / 100, angle + 90, true};
    receivedAt = millis();
    active = true;
    xQueueOverwrite(targets, &target);
  }
}

void setup() {
  const uint8_t pins[] = {A1, 5, 6, 9, 10};
  for (uint8_t pin : pins) {
    pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
  }
  Serial.begin(115200);
  if (WiFi.begin(WIFI_SSID, WIFI_PASSWORD) != WL_CONNECTED) {
    Serial.println("WiFi connection failed. Restart to try again.");
    for (;;) {}
  }
  if (!udp.begin(5000)) {
    Serial.println("UDP setup failed. Restart to try again.");
    for (;;) {}
  }
  Serial.print("UDP listening: "); Serial.print(WiFi.localIP()); Serial.println(":5000");
  targets = xQueueCreate(1, sizeof(Target));
  if (!targets || xTaskCreate(networkTask, "network", 1024, nullptr, 1, nullptr) != pdPASS ||
      xTaskCreate(actuatorTask, "actuators", 384, nullptr, 2, nullptr) != pdPASS) {
    Serial.println("Task allocation failed; outputs remain off");
    for (;;) {}
  }
  vTaskStartScheduler();
  Serial.println("Scheduler failed; outputs remain off");
  for (;;) {}
}
void loop() {}
