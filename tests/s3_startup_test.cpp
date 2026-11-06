#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <BusProtocol.h>
#include "../firmware/s3/xbox.h"

constexpr int OUTPUT = 1, HIGH = 1, SERIAL_8N1 = 0;
struct Call { std::string name; int pin; int value; };
std::vector<Call> calls;
std::vector<uint8_t> transmitted;
int usbChecks = 0;
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
void delay(uint32_t ms) { clockMs += ms; }
void pinMode(int pin, int mode) { calls.push_back({"mode", pin, mode}); }
void digitalWrite(int pin, int value) { calls.push_back({"write", pin, value}); }
struct USB {
  void begin(int) { calls.push_back({"usb",0,0}); }
  void setTxTimeoutMs(int ms) { assert(ms == 0); }
  void println(const char*) {}
  template<typename... Args> void printf(const char*, Args...) {}
  operator bool() { ++usbChecks; return false; } // No USB cable/host.
} Serial;
struct HardwareSerial {
  explicit HardwareSerial(int instance) { assert(instance == 1); }
  void begin(uint32_t baud, int, int rx, int tx) {
    assert(baud == 115200 && rx == 44 && tx == 43); calls.push_back({"uart",0,0});
  }
  size_t write(const uint8_t* bytes, size_t size) {
    transmitted.insert(transmitted.end(), bytes, bytes + size);
    calls.push_back({"send",0,0}); return size;
  }
  int available() { return 0; }
  int read() { return -1; }
};
void xboxBegin() { calls.push_back({"xbox",0,0}); }
void xboxPoll() {}
void xboxPrintStatus() {}
BusProtocol::Frame xboxCommand() { return BusProtocol::stop(); }
#include "../firmware/s3/s3.ino"

int main() {
  setup();
  assert(calls.size() == 8);
  assert(calls[0].name == "mode" && calls[0].pin == 9 && calls[0].value == OUTPUT);
  assert(calls[1].name == "mode" && calls[1].pin == 4 && calls[1].value == OUTPUT);
  assert(calls[2].name == "write" && calls[2].pin == 9 && calls[2].value == HIGH);
  assert(calls[3].name == "write" && calls[3].pin == 4 && calls[3].value == HIGH);
  assert(calls[4].name == "usb" && calls[5].name == "uart");
  assert(calls[6].name == "send" && calls[7].name == "xbox");
  assert(usbChecks == 0 && clockMs == 0);
  BusProtocol::Parser parser;
  BusProtocol::Frame frame;
  bool ready = false;
  for (auto byte : transmitted) ready = parser.push(byte,0,frame) || ready;
  assert(ready && frame.command == BusProtocol::Command::Stop);
  std::cout << "S3 boot/reset initialization, absent USB and startup Stop passed\n";
}
