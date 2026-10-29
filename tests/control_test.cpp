#include "../firmware/bus/control.h"
#include <assert.h>
#include <iostream>
using namespace bus;
Result command(Session &s, uint8_t angle, uint8_t speed, uint32_t now) {
  assert(s.feed(1, now) == Result::Partial);
  assert(s.feed(angle, now) == Result::Partial);
  return s.feed(speed, now);
}
int main() {
  Session s;
  s.open(1, 100);
  assert(s.target.active && s.target.motor == 0 && s.target.servo == SERVO_UNSET);
  // Split messages update neither target nor receipt time until all bytes arrive.
  assert(s.feed(1, 110) == Result::Partial);
  assert(s.feed(166, 120) == Result::Partial);
  assert(s.target.servo == SERVO_UNSET && s.target.motor == 0 && s.target.received == 100);
  assert(s.feed(156, 130) == Result::Command);
  assert(s.target.servo == -90 && s.target.motor == -100 && s.target.received == 130);
  // Combined frames retain TCP ordering and the latest complete target.
  const uint8_t combined[] = {1, 90, 100, 1, 0, 0};
  for (uint8_t b : combined) assert(s.feed(b, 200) != Result::Invalid);
  assert(s.target.servo == 0 && s.target.motor == 0);
  assert(servoAngle(-90) == 0 && servoAngle(0) == 90 && servoAngle(90) == 180);
  assert(pwm(-100) == -255 && pwm(0) == 0 && pwm(100) == 255);
  // Identical messages keep the session live without changing stored outputs.
  for (uint32_t t = 250; t < 5000; t += 10) {
    assert(command(s, 0, 0, t) == Result::Command);
    assert(s.target.servo == 0 && s.target.motor == 0 && s.target.received == t);
  }
  Gate gate;
  assert(gate.allow(s.target, 5989)); assert(!gate.allow(s.target, 5990));
  Target late = s.target; late.received = 6000;
  assert(!gate.allow(late, 6001));
  assert(s.feed(1, 5990) == Result::Invalid && !s.target.active);
  s.open(2, 7000); assert(gate.allow(s.target, 7000));
  assert(s.target.motor == 0 && s.target.servo == SERVO_UNSET);
  s.close(); assert(!gate.allow(s.target, 7001));
  // Incomplete traffic never refreshes the timeout.
  s.open(3, 0); s.feed(1, 998); s.feed(0, 999);
  assert(s.target.received == 0 && s.feed(50, 1000) == Result::Invalid);
  // Unknown types and invalid signed fields close the session atomically.
  for (uint8_t type : {0, 2, 3, 255}) { s.open(4, 0); assert(s.feed(type, 1) == Result::Invalid); }
  for (const auto &pair : {std::pair<uint8_t,uint8_t>{91,0}, {165,0}, {0,101}, {0,155}, {128,128}}) {
    s.open(5, 0); assert(command(s, pair.first, pair.second, 1) == Result::Invalid);
    assert(!s.target.active && s.target.motor == 0 && s.target.servo == SERVO_UNSET);
  }
  // Closing a partial message clears framing before the next connection.
  s.open(6, 0); s.feed(1, 1); s.feed(30, 1); s.close(); s.open(7, 10);
  assert(command(s, 90, 100, 11) == Result::Command && s.target.servo == 90 && s.target.motor == 100);
  s.open(8, UINT32_MAX - 100); assert(!expired(s.target, 100)); assert(expired(s.target, 899));
  std::cout << "Three-byte framing, atomic targets, repeated commands, 1s timeout, session reset and rollover passed\n";
}
