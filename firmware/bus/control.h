#pragma once
#include <stdint.h>

namespace bus {
constexpr uint16_t TCP_PORT = 5000;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 250;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 750;
constexpr int16_t SERVO_UNSET = 127;
enum Type : uint8_t { MOTOR = 1, SERVO = 2, PING = 3 };
enum class Result { Partial, Command, Ping, Invalid };
struct Target {
  uint32_t session = 0;
  uint32_t heartbeat = 0;
  int16_t servo = SERVO_UNSET;
  int8_t motor = 0;
  bool active = false;
};
inline bool expired(const Target &target, uint32_t now) {
  return uint32_t(now - target.heartbeat) >= HEARTBEAT_TIMEOUT_MS;
}
inline int signedByte(uint8_t value) { return value < 128 ? value : int(value) - 256; }
inline int pwm(int percent) { return percent * 255 / 100; }
inline int servoAngle(int offset) { return offset + 90; }

// TCP is a stream: retain an incomplete pair, decode every complete pair in order.
class Session {
  uint8_t type = 0;
public:
  Target target;
  void open(uint32_t id, uint32_t now) {
    target = Target(); target.session = id; target.heartbeat = now; target.active = true;
    type = 0;
  }
  void close() { target.active = false; target.motor = 0; target.servo = SERVO_UNSET; type = 0; }
  Result feed(uint8_t byte, uint32_t now) {
    if (!target.active || expired(target, now)) { close(); return Result::Invalid; }
    if (type == 0) {
      if (byte < MOTOR || byte > PING) { close(); return Result::Invalid; }
      type = byte;
      return Result::Partial;
    }
    const uint8_t messageType = type;
    type = 0;
    int value = signedByte(byte);
    if (messageType == MOTOR && value >= -100 && value <= 100) target.motor = int8_t(value);
    else if (messageType == SERVO && value >= -90 && value <= 90) target.servo = int16_t(value);
    else if (messageType == PING && value == 0) { target.heartbeat = now; return Result::Ping; }
    else { close(); return Result::Invalid; }
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
