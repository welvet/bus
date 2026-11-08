#include <Arduino_FreeRTOS.h>
#include <Servo.h>
#include <BusProtocol.h>
#include "display.h"

struct Target {
  int motorPwm;
  int servoAngle;
  bool enabled;
  uint32_t receivedAt;
};
QueueHandle_t targets;
QueueHandle_t feedback;
BusProtocol::Parser parser;
bool receiverArmed = false, receiverActive = false;
uint32_t receivedAt = 0;
uint32_t reportedAt = 0;

void setMotors(int pwm) {
  analogWrite(5, 0); analogWrite(6, 0);
  analogWrite(9, 0); analogWrite(10, 0);
  if (pwm > 0) { analogWrite(5, pwm); analogWrite(9, pwm); }
  if (pwm < 0) { analogWrite(6, -pwm); analogWrite(10, -pwm); }
}

void actuatorTask(void*) {
  Servo servo;
  Target target = {0, 90, false, 0};
  int pwm = 0, angle = -1;
  for (;;) {
    uint32_t age = uint32_t(millis() - target.receivedAt);
    if (target.enabled && age >= BusProtocol::timeoutMs) target.enabled = false;
    if (!target.enabled) {
      if (pwm != 0) setMotors(0);
      if (servo.attached()) { servo.detach(); digitalWrite(13, LOW); }
      pwm = 0; angle = -1;
    } else {
      if (target.motorPwm != pwm) { setMotors(target.motorPwm); pwm = target.motorPwm; }
      if (target.servoAngle != angle) {
        if (!servo.attached()) servo.attach(13, 500, 2500);
        servo.write(target.servoAngle);
        angle = target.servoAngle;
      }
    }
    // Report the software-applied target without doing UART I/O in this task.
    int speed = (pwm * 100 + (pwm >= 0 ? 127 : -127)) / 255;
    BusProtocol::Frame status = target.enabled
        ? BusProtocol::Frame{BusProtocol::Command::Applied, angle - 90, speed}
        : BusProtocol::Frame{BusProtocol::Command::Stopped, 0, 0};
    xQueueOverwrite(feedback, &status);
    displayShow(status.speed, status.angle, target.enabled);
    // The actuator task owns the watchdog, even if the serial task stalls.
    TickType_t wait = target.enabled
        ? pdMS_TO_TICKS(BusProtocol::remainingMs(target.receivedAt, millis()))
        : portMAX_DELAY;
    if (xQueueReceive(targets, &target, wait) != pdTRUE) target.enabled = false;
  }
}

void receiveSerial() {
  uint32_t now = millis();
  if (receiverActive && uint32_t(now - receivedAt) >= BusProtocol::timeoutMs) {
    receiverActive = receiverArmed = false;
  }
  // Bound work so garbage traffic cannot prevent timeout/arming checks.
  for (int i = 0; i < 64 && Serial.available() > 0; ++i) {
    BusProtocol::Frame frame;
    if (!parser.push(uint8_t(Serial.read()), millis(), frame)) continue;
    if (frame.command == BusProtocol::Command::Lights) {
      digitalWrite(7, frame.angle); // Active-high XY-MOS trigger; independent of motion.
      continue; // Light traffic must never feed the motor watchdog.
    }
    if (frame.command != BusProtocol::Command::Drive && frame.command != BusProtocol::Command::Stop) continue;
    now = millis();
    if (receiverActive && uint32_t(now - receivedAt) >= BusProtocol::timeoutMs)
      receiverActive = receiverArmed = false;
    Target target = {0, 90, false, now};
    if (frame.command == BusProtocol::Command::Stop) {
      receiverActive = receiverArmed = false;
    } else {
      // After boot/stop/timeout, ignore moving commands until a zero-speed one.
      if (!receiverArmed && frame.speed != 0) continue;
      receiverActive = receiverArmed = true;
      receivedAt = now;
      target = {frame.speed * 255 / 100, frame.angle + 90, true, now};
    }
    xQueueOverwrite(targets, &target);
  }
}

void sendStatus() {
  BusProtocol::Frame status;
  if (uint32_t(millis() - reportedAt) >= 100 && xQueuePeek(feedback, &status, 0) == pdTRUE) {
    uint8_t bytes[BusProtocol::frameSize];
    if (BusProtocol::encode(status, bytes)) Serial.write(bytes, sizeof(bytes));
    reportedAt = millis();
  }
}

void serialTask(void*) {
  for (;;) { receiveSerial(); sendStatus(); vTaskDelay(pdMS_TO_TICKS(5)); }
}

void setup() {
  const uint8_t pins[] = {13, 5, 6, 9, 10, 7};
  for (uint8_t pin : pins) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  displayBegin();
  Serial.begin(BusProtocol::baud);
  targets = xQueueCreate(1, sizeof(Target));
  feedback = xQueueCreate(1, sizeof(BusProtocol::Frame));
  if (!targets || !feedback || xTaskCreate(serialTask, "serial", 1024, nullptr, 1, nullptr) != pdPASS ||
      xTaskCreate(actuatorTask, "actuators", 512, nullptr, 2, nullptr) != pdPASS) {
    for (;;) {} // Outputs remain low if allocation fails.
  }
  vTaskStartScheduler();
  for (;;) {}
}
void loop() {}
