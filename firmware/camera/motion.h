#pragma once
#include <BusProtocol.h>
#include <string.h>

namespace CameraMotion {
constexpr uint32_t maxDurationMs = 2000;
inline bool newer(uint32_t value, uint32_t previous) {
  uint32_t delta = value - previous;
  return delta != 0 && delta < 0x80000000u;
}
struct State {
  uint32_t session = 0, counter = 0, epoch = 0, startedAt = 0, duration = 0, modeAt = 0;
  bool haveCounter = false, haveMode = false, automatic = false;
  bool active = false, needsNeutral = true, requestAuto = false;
  int angle = 0, speed = 0; // Public speed is positive=forward.
  void stop() { active = false; needsNeutral = true; }
  void reset(uint32_t token) {
    session = token; haveCounter = false; requestAuto = false; stop();
  }
  void mode(bool value, uint32_t now) {
    if (!haveMode || automatic != value) {
      stop(); requestAuto = false;
      if (value) ++epoch;
    }
    haveMode = true; automatic = value; modeAt = now;
  }
  bool online(uint32_t now) const {
    return haveMode && uint32_t(now - modeAt) < BusProtocol::timeoutMs;
  }
  const char* accept(uint32_t token, uint32_t sequence, uint32_t activation,
                     const char* op, int targetAngle, int targetSpeed,
                     uint32_t milliseconds, uint32_t now) {
    if (token != session) return "session";
    if (haveCounter && !newer(sequence, counter)) return "old-counter";
    if (strcmp(op, "drive") == 0) {
      if (!online(now) || !automatic) return "not-auto";
      if (activation != epoch) return "epoch";
      if (targetAngle < -45 || targetAngle > 45 || targetSpeed < -100 || targetSpeed > 100 ||
          milliseconds == 0 || milliseconds > maxDurationMs) return "range";
      // A replacement after expiry must rearm, even if no tick ran in between.
      if (active && uint32_t(now - startedAt) >= duration) stop();
      angle = targetAngle; speed = targetSpeed; startedAt = now;
      duration = milliseconds; active = true;
    } else if (strcmp(op, "auto") == 0) {
      if (!online(now)) return "renesas-offline";
      if (!automatic) requestAuto = true; // No Auto piggybacked on Drive.
    } else if (strcmp(op, "stop") == 0) {
      stop(); requestAuto = false;
    } else return "op";
    counter = sequence; haveCounter = true;
    return nullptr;
  }
  BusProtocol::Frame tick(uint32_t now, bool wifi) {
    if (!wifi || !online(now)) {
      stop(); requestAuto = false;
      // Forget auto across an outage; the next Mode=auto creates a new epoch.
      haveMode = false;
      return BusProtocol::stop();
    }
    if (requestAuto) {
      requestAuto = false;
      return {BusProtocol::Command::Auto,0,0};
    }
    if (!automatic || !active) return BusProtocol::stop();
    if (uint32_t(now - startedAt) >= duration) { stop(); return BusProtocol::stop(); }
    if (needsNeutral) { needsNeutral = false; return {BusProtocol::Command::Drive,0,0}; }
    return {BusProtocol::Command::Drive,angle,-speed};
  }
};
} // namespace CameraMotion
