#pragma once
#include <stdint.h>

// 12x8 matrix: signed speed at the top, absolute servo-angle marker below.
inline void displayPixels(int electricalSpeed, int angle, bool enabled, uint8_t (&pixels)[8][12]) {
  for (int row = 0; row < 8; ++row)
    for (int column = 0; column < 12; ++column) pixels[row][column] = 0;
  if (!enabled) {
    for (int row = 0; row < 8; ++row) {
      pixels[row][row + 2] = 1;
      pixels[row][9 - row] = 1;
    }
    return;
  }
  int speed = electricalSpeed < -100 ? -100 : electricalSpeed > 100 ? 100 : electricalSpeed;
  int magnitude = speed < 0 ? -speed : speed;
  int bars = (magnitude * 6 + 99) / 100;
  // Existing motor mounting: negative electrical speed is forward (right).
  for (int row = 0; row < 3; ++row)
    for (int bar = 0; bar < bars; ++bar) pixels[row][speed < 0 ? 6 + bar : 5 - bar] = 1;
  pixels[3][5] = pixels[3][6] = 1; // Speed zero/reference.
  int limited = angle < -90 ? -90 : angle > 90 ? 90 : angle;
  int column = ((limited + 90) * 10 + 90) / 180;
  for (int row = 5; row < 7; ++row)
    pixels[row][column] = pixels[row][column + 1] = 1;
  pixels[7][5] = pixels[7][6] = 1; // Steering zero/reference.
}

void displayBegin();
void displayShow(int electricalSpeed, int angle, bool enabled);
