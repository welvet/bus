#pragma once
#include <stdint.h>
#include <BusProtocol.h>

struct XboxInput {
  uint16_t steering = 32768, leftTrigger = 0, rightTrigger = 0;
  bool calibrate = false, reset = false, connected = false;
  uint32_t updatedAt = 0, reportAt = 0, connectedAt = 0, session = 0;
};

void xboxBegin();
void xboxPoll();
void xboxPrintStatus();
BusProtocol::Frame xboxCommand();
