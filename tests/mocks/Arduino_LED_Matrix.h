#pragma once
#include <stdint.h>
#include <string.h>

extern uint8_t matrixPixels[8][12];
extern int matrixBegins, matrixUpdates;
struct ArduinoLEDMatrix {
  bool begin() { ++matrixBegins; return true; }
  void renderBitmap(uint8_t (&pixels)[8][12], int, int) {
    memcpy(matrixPixels, pixels, sizeof(matrixPixels)); ++matrixUpdates;
  }
};
