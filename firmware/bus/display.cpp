#include "display.h"
#include <Arduino_LED_Matrix.h>

namespace {
ArduinoLEDMatrix matrix;
bool ready = false, haveFrame = false, lastEnabled = false;
int lastSpeed = 0, lastAngle = 0;
}

void displayBegin() {
  ready = matrix.begin();
  haveFrame = false;
  displayShow(0, 0, false);
}

void displayShow(int electricalSpeed, int angle, bool enabled) {
  if (!ready || (haveFrame && electricalSpeed == lastSpeed && angle == lastAngle && enabled == lastEnabled)) return;
  uint8_t pixels[8][12];
  displayPixels(electricalSpeed, angle, enabled, pixels);
  matrix.renderBitmap(pixels, 8, 12); // Fixed-size frame copy; no animation/delay.
  lastSpeed = electricalSpeed;
  lastAngle = angle;
  lastEnabled = enabled;
  haveFrame = true;
}
