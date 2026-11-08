#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <utility>
#include <vector>
#include <BusProtocol.h>
#include "../firmware/s3/control.h"
#include "../firmware/s3/lights.h"

struct EndTask {};
using TickType_t = uint32_t;
using QueueHandle_t = void*;
constexpr int LOW = 0, OUTPUT = 1, pdTRUE = 1, pdFALSE = 0, pdPASS = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
void vTaskDelay(uint32_t) {}
std::vector<std::pair<int, int>> pwmWrites, digitalWrites;
std::vector<int> servoWrites;
std::vector<uint32_t> detachedAt;
int attaches = 0;
bool slowServo = false;
void pinMode(int, int) {}
void digitalWrite(int pin, int value) { digitalWrites.emplace_back(pin, value); }
void analogWrite(int pin, int value) { pwmWrites.emplace_back(pin, value); }
struct MockSerial {
  std::deque<uint8_t> input;
  std::vector<uint8_t> output;
  void begin(uint32_t baud) { assert(baud == 115200); }
  int available() { return int(input.size()); }
  int read() { int value = input.front(); input.pop_front(); return value; }
  size_t write(const uint8_t* bytes, size_t count) {
    output.insert(output.end(), bytes, bytes + count); return count;
  }
} Serial;
struct Servo {
  bool enabled = false;
  bool attached() const { return enabled; }
  void attach(int pin, int low, int high) {
    assert(pin == 13 && low == 500 && high == 2500); enabled = true; ++attaches;
  }
  void detach() { enabled = false; detachedAt.push_back(clockMs); }
  void write(int angle) { servoWrites.push_back(angle); if (slowServo) clockMs += 2; }
};
QueueHandle_t xQueueCreate(int count, size_t) {
  static uintptr_t id = 0; assert(count == 1); return reinterpret_cast<void*>(++id);
}
int xTaskCreate(void (*)(void*), const char*, int, void*, int, void*) { return pdPASS; }
void vTaskStartScheduler() { throw EndTask(); }
int xQueueReceive(QueueHandle_t, void*, uint32_t);
int xQueuePeek(QueueHandle_t, void*, uint32_t);
void xQueueOverwrite(QueueHandle_t, const void*);
#include "../firmware/bus/bus.ino"
#include "../firmware/bus/display.cpp"
uint8_t matrixPixels[8][12];
int matrixBegins = 0, matrixUpdates = 0;

std::vector<Target> queued;
std::vector<BusProtocol::Frame> feedbackFrames;
struct Arrival { uint32_t at; Target target; bool end; };
std::deque<Arrival> arrivals;
std::vector<size_t> writeCounts;
std::vector<uint32_t> waits;
int xQueueReceive(QueueHandle_t, void* destination, uint32_t wait) {
  waits.push_back(wait);
  writeCounts.push_back(pwmWrites.size());
  if (arrivals.empty()) throw EndTask();
  uint32_t until = uint32_t(arrivals.front().at - clockMs);
  if (wait != portMAX_DELAY && until > wait) { clockMs += wait; return pdFALSE; }
  Arrival next = arrivals.front(); arrivals.pop_front(); clockMs = next.at;
  if (next.end) throw EndTask();
  *static_cast<Target*>(destination) = next.target;
  return pdTRUE;
}
void xQueueOverwrite(QueueHandle_t queue, const void* source) {
  if (queue == feedback) feedbackFrames.push_back(*static_cast<const BusProtocol::Frame*>(source));
  else queued.push_back(*static_cast<const Target*>(source));
}
int xQueuePeek(QueueHandle_t queue, void* destination, uint32_t) {
  assert(queue == feedback);
  if (feedbackFrames.empty()) return pdFALSE;
  *static_cast<BusProtocol::Frame*>(destination) = feedbackFrames.back(); return pdTRUE;
}
void runActuators() { try { actuatorTask(nullptr); } catch (const EndTask&) {} }
void resetReceiver() {
  parser = BusProtocol::Parser{}; receiverArmed = receiverActive = false; receivedAt = 0;
  Serial.input.clear(); queued.clear();
}
std::vector<uint8_t> bytes(BusProtocol::Frame frame) {
  std::vector<uint8_t> result(BusProtocol::frameSize);
  assert(BusProtocol::encode(frame, result.data())); return result;
}
void feed(const std::vector<uint8_t>& input, uint32_t at) {
  clockMs = at; Serial.input.insert(Serial.input.end(), input.begin(), input.end()); receiveSerial();
}
BusProtocol::Frame drive(int angle, int speed) { return {BusProtocol::Command::Drive, angle, speed}; }
void testFraming() {
  BusProtocol::Frame result;
  BusProtocol::Parser p;
  const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
  assert(BusProtocol::checksum(check, sizeof(check)) == 0xf4);
  for (int angle = -90; angle <= 90; ++angle) {
    for (int speed : {-100, -1, 0, 1, 100}) {
      auto data = bytes(drive(angle, speed));
      for (size_t i = 0; i < data.size(); ++i) {
        bool ready = p.push(data[i], 10, result);
        assert(ready == (i + 1 == data.size()));
      }
      assert(result.angle == angle && result.speed == speed);
    }
  }
  auto data = bytes(drive(10, 50));
  for (size_t bit = 0; bit < data.size() * 8; ++bit) {
    p = BusProtocol::Parser{}; auto bad = data; bad[bit / 8] ^= uint8_t(1u << (bit % 8));
    for (auto value : bad) assert(!p.push(value, 20, result));
    bool accepted = false;
    for (auto value : data) accepted = p.push(value, 21, result) || accepted;
    assert(accepted); // Resynchronizes after corruption.
  }
  p = BusProtocol::Parser{};
  for (size_t i = 0; i < 3; ++i) assert(!p.push(data[i], 0, result));
  for (size_t i = 3; i < data.size(); ++i) assert(!p.push(data[i], 50, result));
  bool accepted = false;
  for (auto value : data) accepted = p.push(value, 51, result) || accepted;
  assert(accepted);
  for (auto invalid : {drive(91,0), drive(0,101), BusProtocol::Frame{BusProtocol::Command::Stop,0,1}})
    assert(!BusProtocol::encode(invalid, data.data()));
  for (int field : {2,3,4}) {
    auto invalid = bytes(drive(0,0)); invalid[size_t(field)] = 127;
    invalid[5] = BusProtocol::checksum(invalid.data(),5);
    p = BusProtocol::Parser{};
    for (auto value : invalid) assert(!p.push(value, 60, result));
  }
  for (size_t missing = 0; missing < data.size(); ++missing) {
    auto truncated = bytes(drive(10,50)); truncated.erase(truncated.begin() + missing);
    p = BusProtocol::Parser{};
    for (auto value : truncated) assert(!p.push(value,70,result));
    bool recovered = false;
    for (auto value : bytes(drive(10,50))) recovered = p.push(value,71,result) || recovered;
    assert(recovered);
  }
}
void testReceiver() {
  resetReceiver();
  feed(bytes({BusProtocol::Command::Applied,0,0}),0);
  feed(bytes({BusProtocol::Command::Stopped,0,0}),0);
  assert(queued.empty() && !receiverArmed); // Replies cannot act as control commands.
  feed(bytes(drive(0, 100)), 0); assert(queued.empty()); // Startup must first stop.
  feed(bytes(drive(10, 0)), 10); assert(queued.size() == 1 && queued.back().enabled);
  feed(bytes(drive(-90, -100)), 20); assert(queued.back().motorPwm == -255 && queued.back().servoAngle == 0);
  auto bad = bytes(drive(0,100)); bad.back() ^= 1;
  feed(bad, 500); assert(receivedAt == 20);
  feed(bytes(drive(90, 100)), 1019); assert(queued.back().motorPwm == 255 && queued.back().servoAngle == 180);
  feed(bytes(BusProtocol::stop()), 1020); assert(!queued.back().enabled);
  size_t count = queued.size(); feed(bytes(drive(0,100)), 1021); assert(queued.size() == count);
  feed(bytes(drive(0,0)), 1022);
  feed(bytes(drive(0,100)), 1023);
  count = queued.size(); feed(bytes(drive(0,100)), 2023); assert(queued.size() == count); // Timeout disarms.
  feed(bytes(drive(0,0)), 2024); assert(queued.back().enabled);
  resetReceiver();
  feed(bytes(drive(0,0)), UINT32_MAX - 100);
  feed({}, 899); assert(!receiverArmed); // Exactly 1000 ms across rollover.
}
void resetActuators(uint32_t now = 0) {
  clockMs = now; pwmWrites.clear(); servoWrites.clear(); detachedAt.clear();
  attaches = 0; writeCounts.clear(); waits.clear(); arrivals.clear(); slowServo = false;
  feedbackFrames.clear(); Serial.output.clear();
}
void testActuators() {
  resetActuators();
  arrivals = {{0,{255,90,true,0},false}, {200,{255,90,true,200},false},
      {300,{255,180,true,300},false}, {400,{-255,180,true,400},false},
      {500,{0,90,false,500},false}, {600,{0,90,false,600},false},
      {700,{0,100,true,700},false}, {1800,{},true}};
  runActuators();
  assert((servoWrites == std::vector<int>{90,180,100}));
  assert(attaches == 2 && (detachedAt == std::vector<uint32_t>{500,1700}));
  assert(writeCounts[1] == writeCounts[2] && writeCounts[2] == writeCounts[3]);
  assert(writeCounts[5] == writeCounts[6]);
  resetActuators();
  arrivals = {{0,{255,90,true,0},false}, {1100,{},true}};
  runActuators(); assert((detachedAt == std::vector<uint32_t>{1000}));
  uint8_t stoppedPixels[8][12]; displayPixels(0,0,false,stoppedPixels);
  assert(memcmp(matrixPixels,stoppedPixels,sizeof(matrixPixels)) == 0);
  assert(feedbackFrames.front().command == BusProtocol::Command::Stopped);
  assert(feedbackFrames[1].command == BusProtocol::Command::Applied && feedbackFrames[1].speed == 100);
  assert(feedbackFrames.back().command == BusProtocol::Command::Stopped);
  sendStatus();
  assert(Serial.output == bytes({BusProtocol::Command::Stopped,0,0}));
  sendStatus(); assert(Serial.output.size() == BusProtocol::frameSize); // Rate limited.
  assert((pwmWrites[pwmWrites.size()-1] == std::pair<int,int>{10,0}));
  resetActuators(1500); arrivals = {{1500,{255,90,true,0},false}, {1600,{},true}};
  runActuators(); assert(attaches == 0 && pwmWrites.empty()); // Stale queued drive never applies.
  resetActuators(UINT32_MAX - 100);
  arrivals = {{UINT32_MAX-100,{255,90,true,UINT32_MAX-100},false}, {1000,{},true}};
  runActuators(); assert((detachedAt == std::vector<uint32_t>{899}));
  resetActuators(999); slowServo = true;
  arrivals = {{999,{255,90,true,0},false}, {1100,{},true}};
  runActuators(); assert(detachedAt.front() == 1001); // No unsigned wait underflow after slow output.
}
void testDisplay() {
  uint8_t pixels[8][12];
  displayPixels(0,0,true,pixels);
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 12; ++column) assert(pixels[row][column] == 0);
  assert(pixels[5][5] && pixels[5][6] && pixels[7][5] && pixels[7][6]);
  displayPixels(-100,90,true,pixels);
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 12; ++column) assert(pixels[row][column] == (column >= 6));
  assert(pixels[5][10] && pixels[5][11]);
  displayPixels(100,-90,true,pixels);
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 12; ++column) assert(pixels[row][column] == (column < 6));
  assert(pixels[5][0] && pixels[5][1]);
  displayPixels(30,0,true,pixels); assert(pixels[0][5] && pixels[0][4] && !pixels[0][3]);
  displayPixels(100,90,false,pixels);
  const char* stopped[] = {"..#......#..", "...#....#...", "....#..#....", ".....##.....",
                           ".....##.....", "....#..#....", "...#....#...", "..#......#.."};
  for (int row = 0; row < 8; ++row)
    for (int column = 0; column < 12; ++column)
      assert(pixels[row][column] == (stopped[row][column] == '#'));
  displayShow(50,10,true); int count = matrixUpdates;
  displayShow(50,10,true); assert(matrixUpdates == count);
}
XboxInput neutral(uint32_t now) {
  XboxInput input; input.connected = true; input.session = 1;
  input.updatedAt = input.reportAt = now; input.connectedAt = 1; return input;
}
void testControls() {
  Control::State state; XboxInput input = neutral(100);
  input.rightTrigger = 1023;
  assert(Control::update(state, input, 100).command == BusProtocol::Command::Stop);
  input.rightTrigger = 0; assert(Control::update(state,input,100).command == BusProtocol::Command::Drive);
  input.rightTrigger = 1023; assert(Control::update(state,input,100).speed == -100);
  input.leftTrigger = 1023; input.rightTrigger = 0; assert(Control::update(state,input,100).speed == 100);
  input.connected = false; assert(Control::update(state,input,100).command == BusProtocol::Command::Stop);
  input.connected = true; ++input.session;
  assert(Control::update(state,input,100).command == BusProtocol::Command::Stop);
  input.leftTrigger = 0; Control::update(state,input,100);
  assert(Control::update(state,input,350).command == BusProtocol::Command::Stop); // Worker stalled.
  input = neutral(1200); input.reportAt = 200;
  assert(Control::update(state,input,1200).command == BusProtocol::Command::Drive); // Unchanged controls remain valid.
  input = neutral(1200); input.reportAt = 0; assert(!Control::fresh(input,1200));
  input.reportAt = 1190; input.connectedAt = 1195; assert(!Control::fresh(input,1200));
  assert(Control::steering(32768) == 0 && Control::steering(0) == -90 && Control::steering(65535) == 90);
  assert(Control::angle(-90) == -71 && Control::angle(90) == 35);
  assert(Control::angle(45) == 9 && Control::angle(-45) == -18);
  assert(Control::angle(0) == 0);
  assert(Control::speed(0,20) == 0 && Control::speed(0,21) == 30 && Control::speed(0,529) == 48);
  assert(Control::speed(1000,500) == -Control::speed(500,1000) && Control::speed(500,500) == 0);
  state = Control::State{}; input = neutral(100); Control::update(state,input,100);
  input.steering = 65535; assert(Control::update(state,input,100).angle == 35);
  input.steering = 0; assert(Control::update(state,input,100).angle == -71);
  input.steering = 32768; assert(Control::update(state,input,100).angle == 0);
  state = Control::State{}; input = neutral(100); Control::update(state,input,100);
  input.rightTrigger = 1023; assert(Control::update(state,input,100).speed == -100);
  assert(Control::update(state,input,350).command == BusProtocol::Command::Stop);
  input.updatedAt = input.reportAt = 400;
  assert(Control::update(state,input,400).command == BusProtocol::Command::Stop); // Held throttle cannot rearm.
  input.rightTrigger = 0; assert(Control::update(state,input,400).command == BusProtocol::Command::Drive);
  input.leftTrigger = 1024; assert(Control::update(state,input,400).command == BusProtocol::Command::Stop);
  state = Control::State{}; input = neutral(100);
  input.updatedAt = UINT32_MAX - 5; input.reportAt = UINT32_MAX - 10; input.connectedAt = UINT32_MAX - 500;
  assert(Control::update(state,input,20).command == BusProtocol::Command::Drive);
  assert(Control::update(state,input,1010).command == BusProtocol::Command::Stop);
  state = Control::State{}; input = neutral(100); Control::update(state,input,100);
  input.steering = 65535; input.rightTrigger = 1023;
  for (uint32_t now : {1100u, 10000u, 60000u, 3600000u}) {
    input.updatedAt = now; // BLE worker remains live; no new HID report needed.
    auto held = Control::update(state,input,now);
    assert(held.command == BusProtocol::Command::Drive && held.angle == 35 && held.speed == -100);
  }
  input.connected = false;
  assert(Control::update(state,input,3600000).command == BusProtocol::Command::Stop);
  input.connected = true; ++input.session;
  input.updatedAt = input.reportAt = input.connectedAt = 3600001;
  assert(Control::update(state,input,3600001).command == BusProtocol::Command::Stop);
  input.steering = 32768; input.rightTrigger = 0;
  assert(Control::update(state,input,3600001).command == BusProtocol::Command::Drive);
}
#include "../firmware/s3/scan.h"
void testScanSchedule() {
  ScanSchedule scans;
  scans.reset(100);
  assert(scans.due(100) && scans.duration(100) == 4000);
  scans.begin(28100);
  assert(scans.due(29100) && scans.duration(29100) == 1000);
  scans.begin(29100);
  assert(!scans.due(30100));
  assert(!scans.due(34099));
  assert(scans.due(34100) && scans.duration(34100) == 1000);
  scans.begin(34100);
  assert(!scans.due(35100));
  scans.reset(35100); // A new disconnect gets a fresh fast-reconnect period.
  assert(scans.due(35100) && scans.duration(35100) == 4000);
  scans.reset(UINT32_MAX - 100);
  assert(scans.fast(100));
  scans.begin(30000);
  assert(!scans.fast(31000) && !scans.due(34999) && scans.due(35000));
}
int main() {
  try { setup(); } catch (const EndTask&) {}
  assert(digitalWrites.size() == 6);
  for (auto output : digitalWrites) assert(output.second == LOW);
  LightSwitch lights;
  assert(!lights.update(false, 1));
  assert(lights.update(true, 1));
  assert(lights.update(true, 1)); // Holding B cannot toggle repeatedly.
  assert(lights.update(false, 1));
  assert(!lights.update(true, 1));
  assert(!lights.update(true, 2)); // Reconnect while held.
  assert(!lights.update(false, 2));
  assert(lights.update(true, 2));
  resetReceiver();
  feed(bytes({BusProtocol::Command::Lights, 1, 0}), 100);
  assert(digitalWrites.back() == std::make_pair(7, 1));
  assert(!receiverArmed && !receiverActive && queued.empty());
  feed(bytes({BusProtocol::Command::Lights, 0, 0}), 200);
  assert(digitalWrites.back() == std::make_pair(7, 0));
  feed(bytes(drive(0, 0)), 300);
  assert(receiverArmed && receiverActive && receivedAt == 300);
  size_t targetCount = queued.size();
  feed(bytes({BusProtocol::Command::Lights, 1, 0}), 400);
  assert(receivedAt == 300 && queued.size() == targetCount);
  feed(bytes({BusProtocol::Command::Lights, 1, 0}), 1300);
  assert(!receiverArmed && !receiverActive && queued.size() == targetCount);
  assert(!BusProtocol::valid({BusProtocol::Command::Lights, 2, 0}));
  assert(!BusProtocol::valid({BusProtocol::Command::Lights, 1, 1}));
  assert(matrixBegins == 1);
  testFraming(); testReceiver(); testActuators(); testControls(); testDisplay(); testScanSchedule();
  std::cout << "Framing/CRC, arming, disconnect/stale stops, watchdog/rollover, actuator outputs, control curves and LED matrix passed\n";
}
