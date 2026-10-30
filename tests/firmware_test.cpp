constexpr int A1 = 15;
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <deque>
#include <vector>
#include <utility>
#include <iostream>

// Host stand-ins for Arduino/FreeRTOS. The application itself is included below.
struct EndTask {};
struct WirePacket { uint32_t at; std::vector<uint8_t> bytes; };
std::deque<WirePacket> packets;
uint32_t clockMs = 0;
int idleSleeps = 0;
uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
void vTaskDelay(uint32_t ticks) { assert(ticks == 5); ++idleSleeps; }
constexpr int LOW = 0, OUTPUT = 1, WL_CONNECTED = 3, pdPASS = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
using QueueHandle_t = void *;
std::vector<std::pair<int,int>> writes;
std::vector<int> servoWrites;
int attaches = 0, detaches = 0, wifiBegins = 0;
uint32_t millis() { return clockMs; }
void pinMode(int, int) {}
void digitalWrite(int, int) {}
void analogWrite(int pin, int value) { writes.emplace_back(pin, value); }
struct SerialMock {
  void begin(int) {}
  template<class T> void print(T) {}
  template<class T> void println(T) {}
} Serial;
struct WiFiMock {
  int begin(const char *, const char *) { ++wifiBegins; return WL_CONNECTED; }
  int localIP() { return 0; }
} WiFi;
class WiFiUDP {
  std::vector<uint8_t> packet;
public:
  int begin(int port) { assert(port == 5000); return 1; }
  int parsePacket() {
    if (packets.empty()) throw EndTask();
    packet = packets.front().bytes; clockMs = packets.front().at; packets.pop_front();
    return int(packet.size());
  }
  int read(uint8_t *data, size_t size) {
    size_t count = packet.size() < size ? packet.size() : size;
    memcpy(data, packet.data(), count); return int(count);
  }
  void flush() {}
};
class Servo {
  bool enabled = false;
public:
  bool attached() { return enabled; }
  void attach(int pin, int minimum, int maximum) {
    assert(pin == A1 && minimum == 500 && maximum == 2500); enabled = true; ++attaches;
  }
  void detach() { enabled = false; ++detaches; }
  void write(int value) { servoWrites.push_back(value); }
};
constexpr char WIFI_SSID[] = "test", WIFI_PASSWORD[] = "test";
QueueHandle_t xQueueCreate(int, size_t) { return reinterpret_cast<void *>(1); }
int xTaskCreate(void (*)(void *), const char *, int, void *, int, void *) { return pdPASS; }
void vTaskStartScheduler() { throw EndTask(); }
int xQueueReceive(QueueHandle_t, void *, uint32_t);
void xQueueOverwrite(QueueHandle_t, const void *);
#include "../.build/sketch.inc"

std::deque<Target> incoming;
std::vector<Target> queued;
std::vector<size_t> writeCounts;
int xQueueReceive(QueueHandle_t, void *destination, uint32_t wait) {
  assert(wait == portMAX_DELAY);
  writeCounts.push_back(writes.size());
  if (incoming.empty()) throw EndTask();
  *static_cast<Target *>(destination) = incoming.front(); incoming.pop_front(); return 1;
}
void xQueueOverwrite(QueueHandle_t, const void *source) { queued.push_back(*static_cast<const Target *>(source)); }
void runNetwork() { try { networkTask(nullptr); } catch (const EndTask &) {} }
int main() {
  try { setup(); } catch (const EndTask &) {}
  assert(wifiBegins == 1);
  // Repeated targets refresh receipt time; malformed/partial/oversized packets do not.
  packets = {{10,{1,166,156}}, {500,{1,166,156}}, {999,{1,90,100}},
             {1200,{1,0}}, {1400,{2,0,0}}, {1500,{1,91,0}}, {1600,{1,0,101}},
             {1700,{1,0,0,0}}, {1999,{}}, {2000,{}}, {2100,{}}, {2200,{1,0,0}}};
  runNetwork();
  assert(idleSleeps == 3); // Only the three empty polls sleep.
  assert(queued.size() == 5);
  assert(queued[0].motorPwm == -255 && queued[0].servoAngle == 0 && queued[0].enabled);
  assert(queued[1].motorPwm == -255 && queued[1].servoAngle == 0);
  assert(queued[2].motorPwm == 255 && queued[2].servoAngle == 180);
  assert(!queued[3].enabled && queued[3].motorPwm == 0);
  assert(queued[4].enabled && queued[4].motorPwm == 0 && queued[4].servoAngle == 90);
  // Apply stored values only when changed, including after a timeout/re-enable.
  incoming = {{255,90,true}, {255,90,true}, {255,180,true}, {-255,180,true},
              {0,90,false}, {0,90,false}, {0,90,true}};
  try { actuatorTask(nullptr); } catch (const EndTask &) {}
  assert((servoWrites == std::vector<int>{90,180,90}));
  assert(attaches == 2 && detaches == 1);
  assert(writeCounts[1] == writeCounts[2]); // identical targets: no PWM writes
  assert(writeCounts[2] == writeCounts[3]); // servo-only change: no PWM writes
  assert(writeCounts[5] == writeCounts[6]); // repeated stop: no PWM writes
  assert(wifiBegins == 1); // tasks never reconnect
  // Unsigned timeout arithmetic handles millis rollover.
  clockMs = UINT32_MAX - 100; queued.clear();
  packets = {{UINT32_MAX - 100,{1,0,100}}, {899,{}}, {900,{}}};
  runNetwork(); assert(queued.size() == 2 && !queued[1].enabled);
  std::cout << "UDP validation, unchanged targets, timeout/rollover, actuator writes and connect-once checks passed\n";
}
