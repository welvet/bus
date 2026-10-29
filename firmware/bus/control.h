#pragma once
#include <stdint.h>

namespace bus {
constexpr uint16_t TCP_PORT = 5000;
constexpr uint8_t CONTROL_MESSAGE = 1;
constexpr uint32_t COMMAND_TIMEOUT_MS = 1000;
constexpr int16_t SERVO_UNSET = 127;
enum class Result { Partial, Command, Invalid };
struct Target {
  uint32_t session = 0;
  uint32_t received = 0;
  int16_t servo = SERVO_UNSET;
  int8_t motor = 0;
  bool active = false;
};
inline bool expired(const Target &target, uint32_t now) {
  return uint32_t(now - target.received) >= COMMAND_TIMEOUT_MS;
}
inline int signedByte(uint8_t value) { return value < 128 ? value : int(value) - 256; }
inline int pwm(int percent) { return percent * 255 / 100; }
inline int servoAngle(int offset) { return offset + 90; }

// TCP is a stream: only a complete valid [1, angle, speed] replaces both targets.
class Session {
  uint8_t frame[3] = {};
  uint8_t size = 0;
public:
  Target target;
  void open(uint32_t id, uint32_t now) {
    target = Target(); target.session = id; target.received = now; target.active = true;
    size = 0;
  }
  void close() { target.active = false; target.motor = 0; target.servo = SERVO_UNSET; size = 0; }
  Result feed(uint8_t byte, uint32_t now) {
    if (!target.active || expired(target, now)) { close(); return Result::Invalid; }
    if (size == 0 && byte != CONTROL_MESSAGE) { close(); return Result::Invalid; }
    frame[size++] = byte;
    if (size < sizeof(frame)) return Result::Partial;
    size = 0;
    int angle = signedByte(frame[1]), speed = signedByte(frame[2]);
    if (angle < -90 || angle > 90 || speed < -100 || speed > 100) {
      close(); return Result::Invalid;
    }
    target.servo = int16_t(angle);
    target.motor = int8_t(speed);
    target.received = now; // Repeated targets still refresh the connection timeout.
    return Result::Command;
  }
};
// Once the actuator watchdog trips, later messages cannot revive the same session.
class Gate {
  uint32_t session = 0;
  bool blocked = true;
public:
  bool allow(const Target &target, uint32_t now) {
    if (target.session != session) { session = target.session; blocked = false; }
    if (!target.active || expired(target, now)) blocked = true;
    return !blocked;
  }
};
}
