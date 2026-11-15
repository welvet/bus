#include "rtsp.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <micro_rtsp_server.h>
#include <micro_rtsp_source_video.h>
#include <atomic>

namespace CameraRTSP {
namespace {
std::atomic<bool> ready{false}, paused{false};
std::atomic<uint32_t> clients{0}, frames{0}, failures{0}, captureMs{0}, lastAt{0}, sourceAgeMs{0};

// Adapt the already initialized camera. Keep an owned PSRAM copy while the
// library paces RTP fragments; never hold a camera framebuffer during sending.
class Source : public micro_rtsp_source_video {
  uint8_t* buffer = nullptr;
  size_t capacity = 0, length = 0, w = 0, h = 0;
public:
  bool available() const override { return ready.load(); }
  void update() override {
    length = 0;
    if (paused.load()) return;
    uint32_t started = millis();
    auto* frame = esp_camera_fb_get();
    captureMs.store(uint32_t(millis() - started));
    if (!frame) { ++failures; return; }
    if (frame->len > capacity) {
      auto* replacement = static_cast<uint8_t*>(heap_caps_realloc(buffer, frame->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (replacement) { buffer = replacement; capacity = frame->len; }
    }
    if (buffer && capacity >= frame->len && frame->format == PIXFORMAT_JPEG) {
      memcpy(buffer, frame->buf, frame->len);
      length = frame->len; w = frame->width; h = frame->height;
      int64_t timestamp = int64_t(frame->timestamp.tv_sec) * 1000000 + frame->timestamp.tv_usec;
      sourceAgeMs.store(uint32_t((esp_timer_get_time() - timestamp) / 1000));
      lastAt.store(millis()); ++frames;
    } else ++failures;
    esp_camera_fb_return(frame);
  }
  uint8_t* data() const override { return length ? buffer : nullptr; }
  size_t width() const override { return w; }
  size_t height() const override { return h; }
  size_t size() const override { return length; }
};

Source source;
micro_rtsp_server server("bus-camera", &source);
void task(void*) {
  server.set_rtsp_port(8554);
  server.set_frame_interval(100); // 10 fps target; paced RTP/UDP packets.
  server.begin();
  ready.store(true);
  for (;;) {
    server.loop();
    clients.store(server.clients());
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
} // namespace

bool begin() { return xTaskCreate(task, "rtsp", 8192, nullptr, 1, nullptr) == pdPASS; }
void pause(bool value) { paused.store(value); }
Stats stats() { return {ready.load(),clients.load(),frames.load(),failures.load(),captureMs.load(),lastAt.load(),sourceAgeMs.load()}; }
} // namespace CameraRTSP
