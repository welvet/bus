#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <cJSON.h>
#include <math.h>
#include <atomic>
#include "config.h"
#include "motion.h"
#include "rtsp.h"

constexpr uint16_t udpPort = 4210;
HardwareSerial renesas(1);
WiFiUDP udp;
CameraMotion::State motion;
SemaphoreHandle_t stateLock;
BusProtocol::Parser replies;
BusProtocol::Frame applied = {BusProtocol::Command::Stopped,0,0};
uint32_t appliedAt = 0, lightsAt = 0, sentAt = 0;
uint32_t rxBytes = 0, rxFrames = 0, rejectedFrames = 0, txFrames = 0;
bool haveApplied = false, lightsApplied = false, haveLights = false;
bool cameraReady = false, serversStarted = false, wasWifi = false;
uint16_t sensorId = 0;
std::atomic<uint32_t> uartErrors{0}, snapshotFrames{0}, snapshotFailures{0};

uint32_t sessionToken() { uint32_t value = esp_random(); return value ? value : 1; }
void sendFrame(const BusProtocol::Frame& frame) {
  uint8_t bytes[BusProtocol::frameSize];
  if (BusProtocol::encode(frame, bytes) && renesas.write(bytes, sizeof(bytes)) == sizeof(bytes)) {
    xSemaphoreTake(stateLock, portMAX_DELAY);
    ++txFrames;
    xSemaphoreGive(stateLock);
  }
}
bool startCamera() {
  // GOOUUU ESP32-S3-CAM v1.5: Espressif BOARD_ESP32S3_GOOUUU / S3-EYE map.
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 11; config.pin_d1 = 9; config.pin_d2 = 8; config.pin_d3 = 10;
  config.pin_d4 = 12; config.pin_d5 = 18; config.pin_d6 = 17; config.pin_d7 = 16;
  config.pin_xclk = 15; config.pin_pclk = 13; config.pin_vsync = 6; config.pin_href = 7;
  config.pin_sccb_sda = 4; config.pin_sccb_scl = 5;
  config.pin_pwdn = -1; config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  if (!psramFound()) { Serial.println("Camera: missing PSRAM"); return false; }
  esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) { Serial.printf("Camera: init failed 0x%x\n", error); return false; }
  auto* sensor = esp_camera_sensor_get();
  sensorId = sensor ? sensor->id.PID : 0;
  camera_fb_t* frame = esp_camera_fb_get();
  if (!frame) { Serial.println("Camera: probe capture failed"); return false; }
  Serial.printf("Camera: PID=0x%04x JPEG=%ux%u bytes=%u PSRAM=%u\n", sensorId,
                unsigned(frame->width), unsigned(frame->height), unsigned(frame->len), unsigned(ESP.getPsramSize()));
  esp_camera_fb_return(frame);
  return true;
}

esp_err_t jsonReply(httpd_req_t* request, const char* json) {
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t statusHandler(httpd_req_t* request) {
  xSemaphoreTake(stateLock, portMAX_DELAY);
  auto state = motion;
  auto output = applied;
  bool reply = haveApplied, light = lightsApplied, lightKnown = haveLights;
  uint32_t replyTime = appliedAt, lightTime = lightsAt;
  uint32_t receivedBytes = rxBytes, receivedFrames = rxFrames, rejected = rejectedFrames, sent = txFrames;
  xSemaphoreGive(stateLock);
  auto video = CameraRTSP::stats();
  uint32_t now = millis();
  uint32_t remaining = state.active && uint32_t(now - state.startedAt) < state.duration
      ? state.duration - uint32_t(now - state.startedAt) : 0;
  wifi_ps_type_t powerSave = WIFI_PS_NONE;
  esp_wifi_get_ps(&powerSave);
  char json[1536];
  snprintf(json,sizeof(json),
    "{\"firmware\":\"bus-camera-rtp-1\",\"uptime_ms\":%lu,\"camera\":%s,\"sensor_pid\":%u,"
    "\"frames\":%lu,\"capture_failures\":%lu,\"session\":%lu,\"counter\":%lu,\"have_counter\":%s,"
    "\"epoch\":%lu,\"mode\":\"%s\",\"remaining_ms\":%lu,\"angle\":%d,\"speed\":%d,"
    "\"renesas_age_ms\":%ld,\"renesas_enabled\":%s,\"renesas_angle\":%d,\"renesas_speed\":%d,"
    "\"udp_port\":%u,\"lights_api\":1,\"lights_on\":%s,\"lights_age_ms\":%ld,"
    "\"video_transport\":\"rtsp-rtp\",\"rtsp_port\":8554,\"rtsp_path\":\"/mjpeg/1\","
    "\"rtsp_ready\":%s,\"rtsp_clients\":%lu,\"fps_target\":10,\"frame_source_age_ms\":%lu,"
    "\"wifi_rssi_dbm\":%d,\"wifi_power_save\":%d,\"heap_free\":%u,\"psram_free\":%u,"
    "\"uart_rx_bytes\":%lu,\"uart_rx_frames\":%lu,\"uart_rejected_frames\":%lu,"
    "\"uart_errors\":%lu,\"uart_tx_frames\":%lu}",
    (unsigned long)now,cameraReady?"true":"false",sensorId,
    (unsigned long)(video.frames+snapshotFrames.load()),(unsigned long)(video.failures+snapshotFailures.load()),
    (unsigned long)state.session,(unsigned long)state.counter,state.haveCounter?"true":"false",
    (unsigned long)state.epoch,!state.online(now)?"unknown":state.automatic?"auto":"manual",
    (unsigned long)remaining,state.angle,state.speed,reply?(long)uint32_t(now-replyTime):-1L,
    reply&&uint32_t(now-replyTime)<BusProtocol::timeoutMs&&output.command==BusProtocol::Command::Applied?"true":"false",
    output.angle,output.speed,udpPort,lightKnown?(light?"true":"false"):"null",
    lightKnown?(long)uint32_t(now-lightTime):-1L,video.ready?"true":"false",(unsigned long)video.clients,
    (unsigned long)video.sourceAgeMs,WiFi.RSSI(),int(powerSave),unsigned(ESP.getFreeHeap()),unsigned(ESP.getFreePsram()),
    (unsigned long)receivedBytes,(unsigned long)receivedFrames,(unsigned long)rejected,
    (unsigned long)uartErrors.load(),(unsigned long)sent);
  return jsonReply(request,json);
}

esp_err_t sessionHandler(httpd_req_t* request) {
  uint32_t token = sessionToken();
  xSemaphoreTake(stateLock, portMAX_DELAY);
  motion.reset(token);
  xSemaphoreGive(stateLock);
  char json[80];
  snprintf(json,sizeof(json),"{\"session\":%lu}",(unsigned long)token);
  return jsonReply(request,json);
}

esp_err_t unavailable(httpd_req_t* request, const char* reason) {
  httpd_resp_set_status(request,"503 Service Unavailable");
  return httpd_resp_send(request,reason,HTTPD_RESP_USE_STRLEN);
}

esp_err_t snapshotHandler(httpd_req_t* request) {
  if (!cameraReady) return unavailable(request,"camera unavailable");
  if (CameraRTSP::stats().clients) return unavailable(request,"RTSP active; use the backend's video feed");
  auto* frame = esp_camera_fb_get();
  if (!frame) { ++snapshotFailures; return unavailable(request,"capture failed"); }
  ++snapshotFrames;
  httpd_resp_set_type(request,"image/jpeg");
  httpd_resp_set_hdr(request,"Cache-Control","no-store");
  esp_err_t error = httpd_resp_send(request,(const char*)frame->buf,frame->len);
  esp_camera_fb_return(frame);
  return error;
}

esp_err_t rtspHandler(httpd_req_t* request) {
  auto video = CameraRTSP::stats();
  char json[352];
  snprintf(json,sizeof(json),
    "{\"ready\":%s,\"port\":8554,\"path\":\"/mjpeg/1\",\"clients\":%lu,\"frames_provided\":%lu,"
    "\"capture_failures\":%lu,\"capture_ms\":%lu,\"last_frame_age_ms\":%ld,\"frame_source_age_ms\":%lu,\"fps_target\":10}",
    video.ready?"true":"false",(unsigned long)video.clients,(unsigned long)video.frames,
    (unsigned long)video.failures,(unsigned long)video.captureMs,
    video.frames?(long)uint32_t(millis()-video.lastAt):-1L,(unsigned long)video.sourceAgeMs);
  return jsonReply(request,json);
}

void startServers() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 6144;
  config.send_wait_timeout = 2; config.recv_wait_timeout = 2;
  config.open_fn = [](httpd_handle_t,int socket) -> esp_err_t {
    int enabled = 1;
    return setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,&enabled,sizeof(enabled))==0?ESP_OK:ESP_FAIL;
  };
  httpd_handle_t control = nullptr;
  ESP_ERROR_CHECK(httpd_start(&control,&config));
  httpd_uri_t status = {}; status.uri="/status"; status.method=HTTP_GET; status.handler=statusHandler;
  httpd_uri_t session = {}; session.uri="/session"; session.method=HTTP_POST; session.handler=sessionHandler;
  httpd_uri_t snapshot = {}; snapshot.uri="/snapshot"; snapshot.method=HTTP_GET; snapshot.handler=snapshotHandler;
  httpd_uri_t rtsp = {}; rtsp.uri="/rtsp"; rtsp.method=HTTP_GET; rtsp.handler=rtspHandler;
  ESP_ERROR_CHECK(httpd_register_uri_handler(control,&status));
  ESP_ERROR_CHECK(httpd_register_uri_handler(control,&session));
  ESP_ERROR_CHECK(httpd_register_uri_handler(control,&snapshot));
  ESP_ERROR_CHECK(httpd_register_uri_handler(control,&rtsp));
  if (MDNS.begin("bus-camera")) MDNS.addService("http","tcp",80);
  if (cameraReady && CameraRTSP::begin()) MDNS.addService("rtsp","tcp",8554);
}
bool integer(cJSON* root, const char* key, double low, double high, double& value) {
  auto* item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(item)) return false;
  value = item->valuedouble;
  return isfinite(value) && value >= low && value <= high && floor(value) == value;
}

void receiveUdp() {
  // Bound packet work so flooding cannot starve the independent UART schedule.
  for (int count = 0; count < 4; ++count) {
    int size = udp.parsePacket();
    if (!size) return;
    auto address = udp.remoteIP(); uint16_t port = udp.remotePort();
    char data[513];
    int length = udp.read((uint8_t*)data, sizeof(data) - 1);
    udp.flush();
    const char* reason = "json";
    uint32_t sequence = 0;
    if (size <= 512 && length == size) {
      data[length] = 0;
      const char* end = nullptr;
      cJSON* root = cJSON_ParseWithOpts(data, &end, true);
      double token, counter, epoch = 0, angle = 0, speed = 0, duration = 0;
      auto* operation = root ? cJSON_GetObjectItemCaseSensitive(root, "op") : nullptr;
      auto* lightValue = root ? cJSON_GetObjectItemCaseSensitive(root, "on") : nullptr;
      bool lightsCommand = cJSON_IsString(operation) && strcmp(operation->valuestring, "lights") == 0;
      bool drive = cJSON_IsString(operation) && strcmp(operation->valuestring, "drive") == 0;
      bool valid = cJSON_IsObject(root) && cJSON_IsString(operation) &&
        (!lightsCommand || cJSON_IsBool(lightValue)) &&
        integer(root,"session",1,UINT32_MAX,token) && integer(root,"counter",0,UINT32_MAX,counter) &&
        (!drive || (integer(root,"epoch",0,UINT32_MAX,epoch) && integer(root,"angle",-45,45,angle) &&
          integer(root,"speed",-100,100,speed) && integer(root,"duration_ms",1,CameraMotion::maxDurationMs,duration)));
      if (valid) {
        sequence = uint32_t(counter);
        xSemaphoreTake(stateLock, portMAX_DELAY);
        reason = motion.accept(uint32_t(token), sequence, uint32_t(epoch), operation->valuestring,
                               int(angle), int(speed), uint32_t(duration), millis(), cJSON_IsTrue(lightValue));
        xSemaphoreGive(stateLock);
      }
      cJSON_Delete(root);
    }
    char reply[160];
    snprintf(reply, sizeof(reply), "{\"accepted\":%s,\"counter\":%lu,\"reason\":\"%s\"}",
             reason ? "false" : "true", (unsigned long)sequence, reason ? reason : "ok");
    udp.beginPacket(address, port); udp.write((const uint8_t*)reply, strlen(reply)); udp.endPacket();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Bus camera: initializing RTP/UDP firmware");
  stateLock = xSemaphoreCreateMutex();
  if (!stateLock) for (;;) delay(1000);
  motion.reset(sessionToken());
  renesas.begin(BusProtocol::baud,SERIAL_8N1,BUS_CAMERA_RX,BUS_CAMERA_TX);
  renesas.onReceiveError([](hardwareSerial_error_t) { ++uartErrors; });
  sendFrame(BusProtocol::stop());
  cameraReady = startCamera();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(BUS_WIFI_SSID,BUS_WIFI_PASSWORD);
  Serial.printf("UART: TX=%d RX=%d; waiting for Wi-Fi\n",BUS_CAMERA_TX,BUS_CAMERA_RX);
}

void loop() {
  bool wifi = WiFi.status()==WL_CONNECTED;
  if (wifi && !wasWifi) {
    WiFi.setSleep(false);
    udp.stop(); udp.begin(udpPort);
    xSemaphoreTake(stateLock,portMAX_DELAY);
    motion.reset(sessionToken());
    xSemaphoreGive(stateLock);
    Serial.printf("Wi-Fi: %s RTSP: rtsp://%s:8554/mjpeg/1 UDP=%u\n",
      WiFi.localIP().toString().c_str(),WiFi.localIP().toString().c_str(),udpPort);
  }
  if (!wifi && wasWifi) { udp.stop(); Serial.println("Wi-Fi: disconnected"); }
  wasWifi = wifi;
  CameraRTSP::pause(!wifi);
  if (wifi && !serversStarted) { startServers(); serversStarted=true; }
  for (int i=0;i<64 && renesas.available();++i) {
    uint8_t value=uint8_t(renesas.read());
    uint32_t now=millis();
    BusProtocol::Frame reply;
    xSemaphoreTake(stateLock,portMAX_DELAY);
    ++rxBytes;
    bool candidate=replies.used==BusProtocol::frameSize-1 && uint32_t(now-replies.lastByteAt)<50;
    bool valid=replies.push(value,now,reply);
    if (candidate && !valid) ++rejectedFrames;
    if (!valid) { xSemaphoreGive(stateLock); continue; }
    ++rxFrames;
    if (reply.command==BusProtocol::Command::LightsApplied) {
      lightsApplied=reply.angle!=0; haveLights=true; lightsAt=now;
    }
    if (reply.command==BusProtocol::Command::Mode) motion.mode(reply.angle!=0,now);
    if (reply.command==BusProtocol::Command::Applied || reply.command==BusProtocol::Command::Stopped) {
      applied=reply; appliedAt=now; haveApplied=true;
    }
    xSemaphoreGive(stateLock);
  }
  if (uint32_t(millis()-sentAt)>=BusProtocol::sendIntervalMs) {
    xSemaphoreTake(stateLock,portMAX_DELAY);
    auto frame=motion.tick(millis(),wifi);
    xSemaphoreGive(stateLock);
    sendFrame(frame); sentAt=millis();
  }
  if (wifi) receiveUdp();
  bool lightValue=false;
  xSemaphoreTake(stateLock,portMAX_DELAY);
  bool sendLight=motion.takeLights(lightValue);
  xSemaphoreGive(stateLock);
  if (sendLight) sendFrame({BusProtocol::Command::Lights,lightValue?1:0,0});
  delay(1);
}