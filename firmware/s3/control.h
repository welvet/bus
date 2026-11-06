#pragma once
#include "xbox.h"
#include <math.h>

namespace Control {
struct State {
  bool armed = false, centering = false, yDown = false, bDown = false;
  int trim = 0, lastAngle = 0;
  uint32_t session = 0;
};
inline int clamp(int value, int low, int high) {
  return value < low ? low : value > high ? high : value;
}
inline bool fresh(const XboxInput& input, uint32_t now) {
  return input.connected && input.reportAt != 0 && uint32_t(now - input.updatedAt) < 250 &&
         uint32_t(now - input.reportAt) < BusProtocol::timeoutMs &&
         uint32_t(now - input.reportAt) <= uint32_t(now - input.connectedAt);
}
inline int steering(uint16_t raw) {
  double axis = 2.0 * raw / 65535.0 - 1.0;
  if (fabs(axis) <= 0.12) return 0;
  return int(round(copysign((fabs(axis) - 0.12) / 0.88, axis) * 90));
}
inline int angle(int stick, int trim, bool centering) {
  int center = clamp(trim, -90, 90);
  if (centering) return center;
  int travel = stick < 0 ? center - clamp(center - 71, -90, 90)
                         : clamp(center + 35, -90, 90) - center;
  double position = clamp(stick, -90, 90) / 90.0;
  return center + int(round(position * fabs(position) * travel));
}
inline int speed(uint16_t rawLeft, uint16_t rawRight) {
  double left = rawLeft / 1023.0, right = rawRight / 1023.0;
  if (left <= 0.02) left = 0;
  if (right <= 0.02) right = 0;
  if (left == right) return 0;
  double travel = ((left > right ? left : right) - 0.02) / 0.98;
  int value = 30 + int(round(travel * travel * 70));
  return right > left ? value : -value;
}
inline BusProtocol::Frame update(State& state, const XboxInput& input, uint32_t now) {
  if (!fresh(input, now) || input.leftTrigger > 1023 || input.rightTrigger > 1023) {
    state.armed = false;
    state.centering = false;
    state.yDown = input.calibrate;
    state.bDown = input.reset;
    return BusProtocol::stop();
  }
  if (state.session != input.session) {
    state.session = input.session;
    state.armed = false;
    state.centering = false;
    state.yDown = input.calibrate;
    state.bDown = input.reset;
  }
  int stick = steering(input.steering);
  bool wasArmed = state.armed;
  if (!state.armed && stick == 0 && input.leftTrigger <= 20 && input.rightTrigger <= 20)
    state.armed = true;
  if (wasArmed && input.calibrate && !state.yDown) {
    state.trim = state.lastAngle;
    state.centering = true;
  }
  if (wasArmed && input.reset && !state.bDown) {
    state.trim = 0;
    state.centering = true;
  }
  state.yDown = input.calibrate;
  state.bDown = input.reset;
  if (stick == 0) state.centering = false;
  if (!state.armed) return BusProtocol::stop();
  state.lastAngle = angle(stick, state.trim, state.centering);
  // Electrical direction matches the existing motor mounting.
  return {BusProtocol::Command::Drive, state.lastAngle, -speed(input.leftTrigger, input.rightTrigger)};
}
} // namespace Control
