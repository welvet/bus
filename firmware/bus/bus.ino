#include <Arduino_FreeRTOS.h>
#include <Servo.h>
#include <BusProtocol.h>
#include "display.h"
#include "priority.h"

struct Target {
  int motorPwm;
  int servoAngle;
  bool enabled;
  uint32_t receivedAt;
};
QueueHandle_t targets;
QueueHandle_t feedback;
BusProtocol::Parser parser, cameraParser;
Priority::State priority;
Priority::Lights lightState;
bool lightsOn = false;
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

    // The actuator task owns the watchdog, even if the serial task stalls.
    TickType_t wait = target.enabled
        ? pdMS_TO_TICKS(BusProtocol::remainingMs(target.receivedAt, millis()))
        : portMAX_DELAY;
    if (xQueueReceive(targets, &target, wait) != pdTRUE) target.enabled = false;
  }
}

void receiveFrame(Priority::Source source, const BusProtocol::Frame& frame) {
  uint32_t now = millis();
  if (frame.command == BusProtocol::Command::Lights && BusProtocol::valid(frame)) {
    if (source == Priority::Source::Controller) lightState.controller(frame.angle != 0);
    else lightState.camera(frame.angle != 0);
    return; // Lighting never refreshes the actuator watchdog.
  }
  bool wasAuto = priority.automatic;
  BusProtocol::Frame selected;
  if (priority.receive(source, frame, now, selected)) {
    Target target = {0, 90, false, now};
    if (selected.command == BusProtocol::Command::Drive)
      target = {selected.speed * 255 / 100, selected.angle + 90, true, now};
    xQueueOverwrite(targets, &target);
  }
  if (wasAuto != priority.automatic) {
    if (priority.automatic) lightState.enteredAuto(now);
    else lightState.manual();
  }
}

void updateLights() {
  bool value = lightState.output(millis());
  if (value != lightsOn) { digitalWrite(7, value ? HIGH : LOW); lightsOn = value; }
}

template<typename Port>
void receivePort(Port& port, BusProtocol::Parser& decoder, Priority::Source source) {
  for (int i = 0; i < 64 && port.available() > 0; ++i) {
    BusProtocol::Frame frame;
    if (decoder.push(uint8_t(port.read()), millis(), frame)) receiveFrame(source, frame);
  }
}

void receiveSerial() {
  priority.expire(millis());
  // Controller first; current manual demand also rejects camera Auto requests.
  receivePort(Serial, parser, Priority::Source::Controller);
  receivePort(Serial1, cameraParser, Priority::Source::Camera);
  updateLights();
}

void sendStatus() {
  BusProtocol::Frame status;
  if (uint32_t(millis() - reportedAt) >= 100 && xQueuePeek(feedback, &status, 0) == pdTRUE) {
    uint8_t bytes[BusProtocol::frameSize];
    if (BusProtocol::encode(status, bytes)) Serial.write(bytes, sizeof(bytes));
    displayShow(status.speed, status.angle, status.command == BusProtocol::Command::Applied, lightsOn);
    BusProtocol::Frame lights = {BusProtocol::Command::LightsApplied, lightsOn ? 1 : 0, 0};
    if (BusProtocol::encode(lights, bytes)) Serial.write(bytes, sizeof(bytes));
    BusProtocol::Frame mode = {BusProtocol::Command::Mode, priority.automatic ? 1 : 0, 0};
    if (BusProtocol::encode(mode, bytes)) Serial.write(bytes, sizeof(bytes));
    // Both links get the same applied motion, physical light output and mode.
    for (const auto& reply : {status, lights, mode}) {
      if (BusProtocol::encode(reply, bytes)) Serial1.write(bytes, sizeof(bytes));
    }
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
  Serial1.begin(BusProtocol::baud); // D0 RX / D1 TX, external camera via level shifter.
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
