#pragma once
#include <BusProtocol.h>

namespace Priority {
enum class Source : uint8_t { Controller, Camera };
struct InputState { bool armed = false; uint32_t at = 0; };

// Serial-task owned. The actuator task independently expires every target.
struct State {
  bool automatic = false, active = false, manualDemand = false;
  uint32_t receivedAt = 0, manualAt = 0;
  InputState controller, camera;

  void expire(uint32_t now) {
    if (uint32_t(now - controller.at) >= BusProtocol::timeoutMs) controller.armed = false;
    if (uint32_t(now - camera.at) >= BusProtocol::timeoutMs) camera.armed = false;
    if (active && uint32_t(now - receivedAt) >= BusProtocol::timeoutMs) active = false;
    if (manualDemand && uint32_t(now - manualAt) >= BusProtocol::timeoutMs) manualDemand = false;
  }

  // True means publish result as a new actuator target, including mode-entry Stop.
  bool receive(Source source, const BusProtocol::Frame& frame, uint32_t now,
               BusProtocol::Frame& result) {
    using BusProtocol::Command;
    expire(now);
    if (!BusProtocol::valid(frame)) return false;
    bool manual = source == Source::Controller;
    InputState& input = manual ? controller : camera;
    if (frame.command == Command::Auto) {
      // A is sent only with neutral controls; camera requests cannot take over
      // against a current nonzero Xbox command.
      if (manual) {
        manualDemand = false;
        controller.armed = true; // S3 sends Auto only after neutral arming.
        controller.at = now; // Maintain manual readiness while A is held.
      }
      if (manualDemand || automatic) return false;
      automatic = true;
      camera.armed = active = false;
      result = BusProtocol::stop();
      return true;
    }
    if (frame.command == Command::Stop) {
      input.armed = false;
      if (manual) manualDemand = false;
      if (manual ? automatic : !automatic) return false; // Inactive source.
      active = false;
      result = BusProtocol::stop();
      return true;
    }
    if (frame.command != Command::Drive) return false;
    if (manual) {
      manualDemand = frame.angle != 0 || frame.speed != 0;
      manualAt = now;
      if (manualDemand && automatic) {
        automatic = active = false;
        camera.armed = false;
        result = BusProtocol::stop();
        // Even an unarmed nonzero command cancels auto, but cannot move.
        if (!controller.armed && frame.speed != 0) return true;
      }
    }
    if (!input.armed && frame.speed != 0) return false;
    input.armed = true;
    input.at = now;
    if (manual ? automatic : !automatic) return false; // Inactive source cannot feed watchdog.
    active = true;
    receivedAt = now;
    result = frame;
    return true;
  }
};

struct Lights {
  bool saved = false, blinking = false;
  uint32_t startedAt = 0;
  void enteredAuto(uint32_t now) { blinking = true; startedAt = now; }
  void manual() { blinking = false; }
  bool output(uint32_t now) {
    uint32_t age = uint32_t(now - startedAt);
    if (blinking && age >= 600) blinking = false;
    return blinking ? (age / 150) % 2 == 0 : saved;
  }
};
} // namespace Priority
