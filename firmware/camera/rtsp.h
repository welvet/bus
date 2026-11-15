#pragma once
#include <stdint.h>

namespace CameraRTSP {
struct Stats {
  bool ready;
  uint32_t clients, frames, failures, captureMs, lastAt, sourceAgeMs;
};
bool begin();
void pause(bool value);
Stats stats();
} // namespace CameraRTSP
