#include "../firmware/bus/control.h"
#include <assert.h>
#include <iostream>
using namespace bus;
int main() {
  Session s;
  s.open(1, 100);
  assert(s.target.active && s.target.motor == 0 && s.target.servo == SERVO_UNSET);
  // A fragmented frame is not an action until both bytes arrive.
  assert(s.feed(MOTOR, 110) == Result::Partial && s.target.motor == 0);
  assert(s.feed(156, 120) == Result::Command && s.target.motor == -100);
  const uint8_t coalesced[] = {SERVO, 166, MOTOR, 100, PING, 0};
  for (uint8_t b : coalesced) assert(s.feed(b, 200) != Result::Invalid);
  assert(s.target.motor == 100 && s.target.servo == -90 && s.target.heartbeat == 200);
  assert(servoAngle(-90) == 0 && servoAngle(0) == 90 && servoAngle(90) == 180);
  assert(pwm(-100) == -255 && pwm(0) == 0 && pwm(100) == 255);
  assert(s.feed(SERVO, 201) == Result::Partial); assert(s.feed(90, 201) == Result::Command);
  assert(s.target.servo == 90);
  Gate gate;
  assert(gate.allow(s.target, 949)); assert(!gate.allow(s.target, 950));
  // A stalled network task cannot revive a watchdog-rejected session with a late ping.
  Target late = s.target; late.heartbeat = 960;
  assert(!gate.allow(late, 961));
  assert(s.feed(PING, 950) == Result::Invalid && !s.target.active);
  s.open(2, 1000); assert(gate.allow(s.target, 1000));
  assert(s.target.motor == 0 && s.target.servo == SERVO_UNSET);
  s.close(); assert(!gate.allow(s.target, 1001) && s.target.motor == 0);
  // Commands never substitute for an explicit heartbeat.
  s.open(3, 2000);
  for (uint32_t t = 2100; t < 2700; t += 100) { s.feed(MOTOR, t); s.feed(10, t); }
  assert(s.target.heartbeat == 2000); assert(s.feed(MOTOR, 2750) == Result::Invalid);
  // Malformed types, values and pings close the session.
  for (const auto &pair : {std::pair<uint8_t,uint8_t>{MOTOR,101}, {MOTOR,128}, {SERVO,91}, {SERVO,165}, {PING,1}}) {
    s.open(4, 0); s.feed(pair.first, 1); assert(s.feed(pair.second, 2) == Result::Invalid && !s.target.active);
  }
  s.open(5, 0); assert(s.feed(0, 1) == Result::Invalid);
  s.open(6, 0); assert(s.feed(4, 1) == Result::Invalid);
  // Partial frames expire and cannot leak into the next connection.
  s.open(7, 0); s.feed(SERVO, 1); s.close(); s.open(8, 100);
  assert(s.feed(MOTOR, 101) == Result::Partial); assert(s.feed(0, 102) == Result::Command);
  // Unsigned uptime rollover and regular 250ms heartbeats.
  s.open(9, UINT32_MAX - 100); assert(!expired(s.target, 100)); assert(expired(s.target, 650));
  s.open(10, 0);
  for (uint32_t t = 250; t < 10000; t += 250) {
    assert(s.feed(PING, t) == Result::Partial); assert(s.feed(0, t) == Result::Ping);
    assert(!expired(s.target, t + 249));
  }
  std::cout << "Binary framing, signed values, heartbeat, disconnect, stale-session gate and rollover passed\n";
}
