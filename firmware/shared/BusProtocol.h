#pragma once
#include <stddef.h>
#include <stdint.h>

namespace BusProtocol {
constexpr uint32_t baud = 115200, sendIntervalMs = 20, timeoutMs = 1000;
inline uint32_t remainingMs(uint32_t receivedAt, uint32_t now) {
  uint32_t age = uint32_t(now - receivedAt);
  return age >= timeoutMs ? 0 : timeoutMs - age;
}
constexpr size_t frameSize = 6;
enum class Command : uint8_t { Drive = 1, Stop = 2, Applied = 3, Stopped = 4, Lights = 5 };
struct Frame { Command command; int angle; int speed; };
inline Frame stop() { return {Command::Stop, 0, 0}; }
inline int signedByte(uint8_t value) { return value < 128 ? value : int(value) - 256; }
inline uint8_t checksum(const uint8_t* bytes, size_t count) {
  uint8_t crc = 0;
  for (size_t i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = uint8_t((crc << 1) ^ ((crc & 0x80) ? 0x07 : 0));
  }
  return crc;
}
inline bool valid(const Frame& frame) {
  if (frame.command == Command::Lights)
    return (frame.angle == 0 || frame.angle == 1) && frame.speed == 0;
  if (frame.command == Command::Stop || frame.command == Command::Stopped)
    return frame.angle == 0 && frame.speed == 0;
  return (frame.command == Command::Drive || frame.command == Command::Applied) &&
         frame.angle >= -90 && frame.angle <= 90 &&
         frame.speed >= -100 && frame.speed <= 100;
}
inline bool encode(const Frame& frame, uint8_t* bytes) {
  if (!valid(frame)) return false;
  bytes[0] = 0xa5; bytes[1] = 0x5a; bytes[2] = uint8_t(frame.command);
  bytes[3] = uint8_t(frame.angle); bytes[4] = uint8_t(frame.speed);
  bytes[5] = checksum(bytes, 5);
  return true;
}

struct Parser {
  uint8_t bytes[frameSize] = {};
  size_t used = 0;
  uint32_t lastByteAt = 0;
  bool push(uint8_t byte, uint32_t now, Frame& frame) {
    if (used && uint32_t(now - lastByteAt) >= 50) used = 0;
    lastByteAt = now;
    if (used == 0) {
      if (byte == 0xa5) bytes[used++] = byte;
      return false;
    }
    if (used == 1 && byte != 0x5a) {
      used = byte == 0xa5 ? 1 : 0;
      return false;
    }
    bytes[used++] = byte;
    if (used != frameSize) return false;
    Frame candidate = {Command(bytes[2]), signedByte(bytes[3]), signedByte(bytes[4])};
    if (checksum(bytes, 5) == bytes[5] && valid(candidate)) {
      used = 0;
      frame = candidate;
      return true;
    }
    // Recover a header already present in the rejected frame's suffix.
    used = 0;
    for (size_t i = 1; i < frameSize; ++i) {
      if (bytes[i] == 0xa5 && (i + 1 == frameSize || bytes[i + 1] == 0x5a)) {
        used = frameSize - i;
        for (size_t j = 0; j < used; ++j) bytes[j] = bytes[i + j];
        break;
      }
    }
    return false;
  }
};
} // namespace BusProtocol
