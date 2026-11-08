#pragma once
#include <stdint.h>

struct LightSwitch {
  bool on = false, down = false, haveSession = false;
  uint32_t session = 0;
  bool update(bool pressed, uint32_t currentSession) {
    if (!haveSession || session != currentSession) {
      haveSession = true;
      session = currentSession;
      down = pressed; // Reconnecting with B held is not a new press.
    } else {
      if (pressed && !down) on = !on;
      down = pressed;
    }
    return on;
  }
};
