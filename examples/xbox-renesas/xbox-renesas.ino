// Serial-only Renesas adaptation of asukiaaa's ESP32 connect example.
// See README.md and ../../upstream/xbox-esp32/LICENSE.
#include <ArduinoBLE.h>
#include <XboxControllerNotificationParser.h>

// Optional address filter; empty accepts a device named Xbox Wireless Controller.
const char controllerAddress[] = "";
BLEDevice controller;
XboxControllerNotificationParser inputs;
bool received = false;
uint32_t receivedAt = 0, printedAt = 0;

void startScan() {
  Serial.println("Scanning; put Xbox controller in pairing mode.");
  BLE.scan();
}

void reportReceived(BLEDevice, BLECharacteristic report) {
  // Xbox BLE input report 1 has exactly 16 bytes (GATT omits report ID).
  // Reject other report layouts rather than interpreting them as driving input.
  uint8_t data[XboxControllerNotificationParser::expectedDataLen];
  if (report.valueLength() != int(sizeof(data))) return;
  if (report.readValue(data, sizeof(data)) != int(sizeof(data))) return;
  if (inputs.update(data, sizeof(data)) != 0) return;
  received = true;
  receivedAt = millis();
  if (uint32_t(millis() - printedAt) >= 100) {
    Serial.println(inputs.toString());
    printedAt = millis();
  }
}

bool subscribeInputs() {
  Serial.println("Discovering attributes...");
  if (!controller.discoverAttributes()) return false;
  BLEService hid = controller.service("1812");
  if (!hid) return false;
  int subscribed = 0;
  for (int i = 0; i < hid.characteristicCount(); ++i) {
    BLECharacteristic report = hid.characteristic(i);
    if (String(report.uuid()) != "2a4d" || !report.canSubscribe()) continue;
    BLEDescriptor reference = report.descriptor("2908");
    uint8_t identity[2];
    // Report Reference: report ID 1, type 1 (input).
    if (!reference || reference.readValue(identity, sizeof(identity)) != 2 ||
        identity[0] != 1 || identity[1] != 1) continue;
    report.setEventHandler(BLEUpdated, reportReceived);
    if (report.subscribe()) ++subscribed;
  }
  Serial.print("Subscribed Xbox input reports: "); Serial.println(subscribed);
  return subscribed > 0;
}

void setup() {
  // Leave existing bus outputs off, including when replacing a running sketch.
  const uint8_t pins[] = {5, 6, 9, 10, 13};
  for (uint8_t pin : pins) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  Serial.begin(115200);
  uint32_t started = millis();
  while (!Serial && uint32_t(millis() - started) < 3000) {}
  if (!BLE.begin()) {
    Serial.println("BLE initialization failed; check UNO R4 connectivity firmware.");
    for (;;) delay(1000);
  }
  BLE.setPairable(YES);
  startScan();
}

void loop() {
  BLE.poll();
  if (controller && controller.connected()) {
    if (received && uint32_t(millis() - receivedAt) >= 1000) {
      Serial.println("No recent input; values are stale.");
      received = false;
    }
    delay(1);
    return;
  }
  if (controller) {
    Serial.println("Disconnected.");
    controller = BLEDevice();
    received = false;
    startScan();
  }
  BLEDevice candidate = BLE.available();
  if (!candidate) return;
  bool matches = controllerAddress[0]
    ? candidate.address().equalsIgnoreCase(controllerAddress)
    : candidate.localName() == "Xbox Wireless Controller";
  if (!matches) return;
  BLE.stopScan();
  Serial.print("Connecting: "); Serial.println(candidate.address());
  if (!candidate.connect()) {
    Serial.println("Connection failed.");
    delay(1000);
    startScan();
    return;
  }
  controller = candidate;
  if (!subscribeInputs()) {
    Serial.println("Input subscription failed; pairing/security or report layout may be unsupported.");
    controller.disconnect();
    controller = BLEDevice();
    delay(1000);
    startScan();
    return;
  }
  Serial.println("Waiting for input; move sticks or press buttons.");
}
