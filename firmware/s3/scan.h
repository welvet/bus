#pragma once
#include <stdint.h>

// Keep reconnects responsive initially, then listen for 1 s every 5 s.
struct ScanSchedule {
  uint32_t disconnectedAt = 0;
  uint32_t startedAt = 0;
  bool started = false;

  void reset(uint32_t now) { disconnectedAt = now; started = false; }
  bool fast(uint32_t now) const { return uint32_t(now - disconnectedAt) < 30000; }
  bool due(uint32_t now) const {
    return fast(now) || !started || uint32_t(now - startedAt) >= 5000;
  }
  uint32_t duration(uint32_t now) const {
    if (!fast(now)) return 1000;
    uint32_t remaining = 30000 - uint32_t(now - disconnectedAt);
    return remaining < 4000 ? remaining : 4000;
  }
  void begin(uint32_t now) { startedAt = now; started = true; }
};
