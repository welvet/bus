#include "xbox.h"
#include "control.h"
#include "scan.h"
#include "lights.h"

#include <Arduino.h>
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {
constexpr char controllerAddress[] = "40:8e:2c:32:c8:74";
enum class Event : uint8_t { Scanning, Connected, Disconnected };
enum class Stage : uint8_t { Starting, Initializing, Scanning, Polling, Discovering, Subscribing, Waiting, Ready, Failed };
enum class Failure : uint8_t { None, Task, Client, Hid, Report, Subscribe };
QueueHandle_t events = nullptr, inputs = nullptr;
std::atomic<bool> linked{false};
std::atomic<uint32_t> generation{0}, connectedAt{0}, workerAt{0};
std::atomic<Stage> stage{Stage::Starting};
std::atomic<Failure> failure{Failure::None};
std::atomic<unsigned> seen{0}, failedAttempts{0};
Control::State control;
LightSwitch lightSwitch;
std::atomic<bool> lightsOn{false};

void logEvent(Event event) { xQueueSend(events, &event, 0); }

struct Callbacks : XboxSeriesXControllerESP32_asukiaaa::ClientCallbacks {
  using Base = XboxSeriesXControllerESP32_asukiaaa::ClientCallbacks;
  explicit Callbacks(XboxSeriesXControllerESP32_asukiaaa::ConnectionState* state) : Base(state) {}
  void onConnect(NimBLEClient* client) override {
    connectedAt.store(millis());
    generation.fetch_add(1);
    Base::onConnect(client);
    linked.store(true);
    stage.store(Stage::Discovering);
    logEvent(Event::Connected);
  }
  void onDisconnect(NimBLEClient* client, int reason) override {
    linked.store(false); // Main loop can stop even while the BLE worker blocks.
    generation.fetch_add(1);
    Base::onDisconnect(client, reason);
    stage.store(Stage::Scanning);
    logEvent(Event::Disconnected);
    logEvent(Event::Scanning);
  }
};

void inputReceived(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  uint32_t session = generation.load();
  uint32_t since = connectedAt.load();
  if (!linked.load() || length != XboxControllerNotificationParser::expectedDataLen) return;
  XboxControllerNotificationParser parsed;
  if (parsed.update(data, length) != 0) return;
  if (!linked.load() || session != generation.load()) return;
  lightsOn.store(lightSwitch.update(parsed.btnB, session));
  XboxInput input;
  input.connected = true;
  input.reportAt = millis();
  input.connectedAt = since;
  input.session = session;
  input.steering = parsed.joyLHori;
  input.leftTrigger = parsed.trigLT;
  input.rightTrigger = parsed.trigRT;
  input.automatic = parsed.btnA;
  xQueueOverwrite(inputs, &input);
}

bool subscribeInput() {
  auto* client = NimBLEDevice::getClientByPeerAddress(NimBLEAddress(controllerAddress, 0));
  if (!client || !client->isConnected()) { failure.store(Failure::Client); return false; }
  auto* hid = client->getService(NimBLEUUID("1812"));
  if (!hid) { failure.store(Failure::Hid); return false; }
  for (auto* report : hid->getCharacteristics()) {
    if (report->getUUID() != NimBLEUUID("2a4d") || !report->canNotify()) continue;
    auto* reference = report->getDescriptor(NimBLEUUID("2908"));
    if (!reference) continue;
    auto identity = reference->readValue();
    if (identity.size() != 2 || identity[0] != 1 || identity[1] != 1) continue;
    // Replace the library's input handler with a validated, queue-only callback.
    bool ok = report->subscribe(true, inputReceived);
    failure.store(ok ? Failure::None : Failure::Subscribe);
    return ok;
  }
  failure.store(Failure::Report);
  return false;
}

void xboxTask(void*) {
  stage.store(Stage::Initializing);
  XboxSeriesXControllerESP32_asukiaaa::Core controller(controllerAddress);
  auto* original = controller.clientCBs;
  controller.clientCBs = new Callbacks(original->pConnectionState);
  delete original;
  controller.begin();
  uint32_t subscribedSession = UINT32_MAX;
  uint32_t checkedAt = 0;
  ScanSchedule scans;
  scans.reset(millis());
  uint32_t scanSession = generation.load();
  for (;;) {
    uint32_t now = millis();
    uint32_t currentSession = generation.load();
    if (currentSession != scanSession) {
      scans.reset(now);
      scanSession = currentSession;
    }
    stage.store(linked.load() ? Stage::Waiting :
                NimBLEDevice::getScan()->isScanning() ? Stage::Scanning : Stage::Polling);
    // Only delay starting a new scan. Always process a discovered controller
    // immediately, even if its advertisement arrived at the end of a scan.
    bool startingScan = !controller.isConnected() &&
                        !NimBLEDevice::getScan()->isScanning() &&
                        XboxSeriesXControllerESP32_asukiaaa::advDevice == nullptr;
    if (!startingScan || scans.due(now)) {
      if (startingScan) {
        // Mirror the library's scan configuration, with our own duration.
        auto* scan = NimBLEDevice::getScan();
        scan->setDuplicateFilter(false);
        scan->setScanCallbacks(controller.scanCBs);
        scan->setInterval(97);
        scan->setWindow(97);
        scan->start(scans.duration(now));
        scans.begin(now);
      } else if (controller.isConnected() ||
                 XboxSeriesXControllerESP32_asukiaaa::advDevice != nullptr) {
        controller.onLoop(); // Discovery cannot block the UART main loop.
      }
    }
    failedAttempts.store(controller.getCountFailedConnection());
    uint32_t session = generation.load();
    if (linked.load() && session != subscribedSession) {
      stage.store(Stage::Subscribing);
      if (subscribeInput()) {
        subscribedSession = session;
      } else {
        auto* client = NimBLEDevice::getClientByPeerAddress(NimBLEAddress(controllerAddress, 0));
        if (client) client->disconnect();
      }
    }
    if (uint32_t(millis() - checkedAt) >= 1000) {
      seen.store(NimBLEDevice::getScan()->getResults().getCount());
      checkedAt = millis();
    }
    if (linked.load()) {
      XboxInput latest;
      stage.store(xQueuePeek(inputs, &latest, 0) == pdTRUE && latest.session == session
                    ? Stage::Ready : Stage::Waiting);
    } else {
      stage.store(Stage::Scanning);
    }
    workerAt.store(millis());
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
} // namespace

void xboxBegin() {
  if (events) return;
  events = xQueueCreate(8, sizeof(Event));
  inputs = xQueueCreate(1, sizeof(XboxInput));
  if (!events || !inputs) {
    if (events) vQueueDelete(events);
    if (inputs) vQueueDelete(inputs);
    events = inputs = nullptr; // No input means Stop forever.
    stage.store(Stage::Failed);
    failure.store(Failure::Task);
    return;
  }
  // Queue this before BLE initialization, which may take time in its task.
  logEvent(Event::Scanning);
  if (xTaskCreate(xboxTask, "xbox", 8192, nullptr, 1, nullptr) != pdPASS) {
    stage.store(Stage::Failed);
    failure.store(Failure::Task);
    logEvent(Event::Disconnected); // Inputs remain empty, so control stays stopped.
  }
}

void xboxPrintStatus() {
  static const char* stages[] = {"starting", "initializing", "scanning", "scan/connect",
                               "discovering", "subscribing", "waiting-input", "input", "failed"};
  static const char* failures[] = {"none", "task", "client", "hid-service", "input-report", "subscribe"};
  XboxInput latest;
  bool haveInput = inputs && xQueuePeek(inputs, &latest, 0) == pdTRUE;
  Serial.printf("Bus S3: ping ms=%lu BLE=%s seen=%u failures=%u err=%s age=%lu rx_ms=%ld armed=%u\n",
                static_cast<unsigned long>(millis()), stages[unsigned(stage.load())],
                seen.load(), failedAttempts.load(), failures[unsigned(failure.load())],
                static_cast<unsigned long>(uint32_t(millis() - workerAt.load())),
                haveInput ? static_cast<long>(uint32_t(millis() - latest.reportAt)) : -1L,
                unsigned(control.armed));
}

void xboxPoll() {
  if (!events) return;
  bool requested = false, printed = false;
  while (Serial.available() > 0) { Serial.read(); requested = true; }
  // Do not gate writes on Serial's bool: hardware CDC can need a first TX to
  // establish its connected state. The driver buffers startup messages.
  Event event;
  while (xQueueReceive(events, &event, 0) == pdTRUE) {
    if (event == Event::Scanning) Serial.println("Xbox: scanning");
    else if (event == Event::Connected) Serial.println("Xbox: connected");
    else Serial.println("Xbox: disconnected");
    printed = true;
  }
  if (requested && !printed)
    Serial.println(linked.load() ? "Xbox: connected" : "Xbox: scanning");
}

BusProtocol::Frame xboxCommand() {
  XboxInput input;
  if (!inputs || xQueuePeek(inputs, &input, 0) != pdTRUE || !linked.load() ||
      input.session != generation.load()) input.connected = false;
  input.updatedAt = workerAt.load();
  return Control::update(control, input, millis());
}

bool xboxLights() { return lightsOn.load(); }
