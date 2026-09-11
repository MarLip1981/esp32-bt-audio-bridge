#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

#include <esp_http_client.h>
#include <esp_err.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <string>

namespace esphome {
namespace bt_audio_bridge {

static const char *const HTTP_TAG = "bt_http_wav";

namespace {

// Keep the HTTP task stack out of the heap. Bluetooth A2DP needs contiguous
// heap blocks for SBC/media buffers, so creating this task dynamically can
// make an otherwise healthy heap temporarily unavailable to BT.
static StaticTask_t http_wav_task_tcb;
static StackType_t http_wav_task_stack[4096];

void log_heap(const char *stage) {
  ESP_LOGI(HTTP_TAG, "Heap %s: free=%u largest=%u", stage,
           static_cast<unsigned>(esp_get_free_heap_size()),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

bool read_exact(esp_http_client_handle_t client, uint8_t *buffer, size_t length) {
  size_t received = 0;
  while (received < length) {
    const int count = esp_http_client_read(client, reinterpret_cast<char *>(buffer + received),
                                           static_cast<int>(length - received));
    if (count > 0) {
      received += static_cast<size_t>(count);
      continue;
    }
    if (count == 0 || count == -ESP_ERR_HTTP_EAGAIN) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    return false;
  }
  return true;
}

uint32_t le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t le16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8);
}

bool is_fourcc(const uint8_t *p, const char *text) {
  return p[0] == static_cast<uint8_t>(text[0]) &&
         p[1] == static_cast<uint8_t>(text[1]) &&
         p[2] == static_cast<uint8_t>(text[2]) &&
         p[3] == static_cast<uint8_t>(text[3]);
}

bool skip_bytes(esp_http_client_handle_t client, uint32_t length) {
  uint8_t buffer[256];
  uint32_t remaining = length;
  while (remaining > 0) {
    const size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
    if (!read_exact(client, buffer, wanted)) return false;
    remaining -= static_cast<uint32_t>(wanted);
  }
  if (length & 1U) {
    uint8_t padding = 0;
    if (!read_exact(client, &padding, 1)) return false;
  }
  return true;
}

}  // namespace

void BtAudioBridge::set_audio_url(const char *url) {
  if (url == nullptr) {
    this->audio_url_[0] = '\0';
    ESP_LOGI(HTTP_TAG, "Audio URL cleared");
    return;
  }
  std::strncpy(this->audio_url_, url, sizeof(this->audio_url_) - 1);
  this->audio_url_[sizeof(this->audio_url_) - 1] = '\0';
  ESP_LOGI(HTTP_TAG, "Audio URL updated: %s", this->audio_url_);
}

void BtAudioBridge::play_http_wav() {
  if (this->engine_test_active_) {
    ESP_LOGW(HTTP_TAG, "HTTP WAV playback already active");
    this->publish_event_("HTTP WAV: already playing");
    return;
  }
  if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) {
    ESP_LOGW(HTTP_TAG, "HTTP WAV requested while Bluetooth speaker is not connected");
    this->publish_event_("HTTP WAV: speaker not connected");
    return;
  }
  if (this->audio_url_[0] == '\0') {
    ESP_LOGW(HTTP_TAG, "HTTP WAV URL is empty");
    this->publish_event_("HTTP WAV: URL empty");
    return;
  }
  const bool http_url = std::strncmp(this->audio_url_, "http://", 7) == 0;
  const bool https_url = std::strncmp(this->audio_url_, "https://", 8) == 0;
  if (!http_url && !https_url) {
    ESP_LOGW(HTTP_TAG, "Only http:// and https:// WAV URLs are supported");
    this->publish_event_("HTTP WAV: URL must use http:// or https://");
    return;
  }
  log_heap("before HTTP task");
  TaskHandle_t task = xTaskCreateStatic(&BtAudioBridge::http_wav_task_, "bt_http_wav",
                                        sizeof(http_wav_task_stack) / sizeof(http_wav_task_stack[0]),
                                        this, 2, http_wav_task_stack, &http_wav_task_tcb);
  if (task == nullptr) {
    ESP_LOGE(HTTP_TAG, "HTTP WAV static task creation failed");
    this->publish_event_("HTTP WAV: task FAILED");
    return;
  }
  ESP_LOGI(HTTP_TAG, "HTTP WAV task created statically");
  this->publish_event_("HTTP WAV: download started");
}

void BtAudioBridge::http_wav_task_(void *arg) {
  auto *self = static_cast<BtAudioBridge *>(arg);
  ESP_LOGI(HTTP_TAG, "HTTP task entered");
  log_heap("at HTTP task entry");

  char url[256];
  std::strncpy(url, self->audio_url_, sizeof(url) - 1);
  url[sizeof(url) - 1] = '\0';
  const bool https_url = std::strncmp(url, "https://", 8) == 0;
  ESP_LOGI(HTTP_TAG, "HTTP task started: stack free words=%u URL=%s",
           static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), url);
  log_heap("before HTTP init");
  esp_http_client_config_t config{};
  config.url = url;
  config.timeout_ms = 5000;
  config.buffer_size = 2048;
  config.buffer_size_tx = 512;
  config.crt_bundle_attach = https_url ? esp_crt_bundle_attach : nullptr;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGE(HTTP_TAG, "HTTP client init failed");
    self->publish_event_("HTTP WAV: HTTP init FAILED");
    self->engine_test_active_ = false;
    log_heap("after HTTP init FAILED");
    vTaskDelete(nullptr);
    return;
  }
  log_heap("after HTTP init");
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(HTTP_TAG, "HTTP open failed: %s", esp_err_to_name(err));
    self->publish_event_("HTTP WAV: HTTP open FAILED");
    esp_http_client_cleanup(client);
    self->engine_test_active_ = false;
    log_heap("after HTTP open FAILED");
    vTaskDelete(nullptr);
    return;
  }
  log_heap("after HTTP open");
  const int64_t content_length = esp_http_client_fetch_headers(client);
  const int http_code = esp_http_client_get_status_code(client);
  if (http_code != 200) {
    ESP_LOGE(HTTP_TAG, "HTTP GET failed, code=%d", http_code);
    self->publish_event_("HTTP WAV: HTTP GET FAILED");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    self->engine_test_active_ = false;
    vTaskDelete(nullptr);
    return;
  }
  if (content_length < 0) {
    ESP_LOGE(HTTP_TAG, "HTTP response headers failed: %lld", static_cast<long long>(content_length));
    self->publish_event_("HTTP WAV: HTTP headers FAILED");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    self->engine_test_active_ = false;
    vTaskDelete(nullptr);
    return;
  }
  ESP_LOGI(HTTP_TAG, "HTTP connected, content-length=%lld, chunked=%s",
           static_cast<long long>(content_length),
           esp_http_client_is_chunked_response(client) ? "yes" : "no");
  if (!self->audio_engine_.begin()) {
    ESP_LOGE(HTTP_TAG, "Audio engine buffer allocation failed after HTTP connect");
    self->publish_event_("HTTP WAV: audio buffer FAILED");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    self->engine_test_active_ = false;
    log_heap("after audio buffer FAILED");
    vTaskDelete(nullptr);
    return;
  }
  self->audio_engine_.clear();
  log_heap("after audio buffer allocation");
  uint8_t header[12];
  if (!read_exact(client, header, sizeof(header)) || !is_fourcc(header, "RIFF") || !is_fourcc(header + 8, "WAVE")) {
    ESP_LOGE(HTTP_TAG, "Invalid RIFF/WAVE header");
    self->publish_event_("HTTP WAV: invalid RIFF/WAVE");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    self->engine_test_active_ = false;
    self->audio_engine_.clear();
    vTaskDelete(nullptr);
    return;
  }
  bool fmt_found = false;
  bool data_found = false;
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;
  uint32_t data_size = 0;
  while (!data_found) {
    uint8_t chunk_header[8];
    if (!read_exact(client, chunk_header, sizeof(chunk_header))) {
      ESP_LOGE(HTTP_TAG, "Unexpected end of WAV while reading chunk header");
      self->publish_event_("HTTP WAV: truncated header");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      self->engine_test_active_ = false;
      self->audio_engine_.clear();
      vTaskDelete(nullptr);
      return;
    }
    const uint32_t chunk_size = le32(chunk_header + 4);
    if (is_fourcc(chunk_header, "fmt ")) {
      if (chunk_size < 16) {
        ESP_LOGE(HTTP_TAG, "Invalid fmt chunk size=%u", static_cast<unsigned>(chunk_size));
        self->publish_event_("HTTP WAV: invalid fmt chunk");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        self->engine_test_active_ = false;
        self->audio_engine_.clear();
        vTaskDelete(nullptr);
        return;
      }
      uint8_t fmt[16];
      if (!read_exact(client, fmt, sizeof(fmt))) {
        self->publish_event_("HTTP WAV: truncated fmt chunk");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        self->engine_test_active_ = false;
        self->audio_engine_.clear();
        vTaskDelete(nullptr);
        return;
      }
      audio_format = le16(fmt + 0);
      channels = le16(fmt + 2);
      sample_rate = le32(fmt + 4);
      bits_per_sample = le16(fmt + 14);
      fmt_found = true;
      if (chunk_size > 16 && !skip_bytes(client, chunk_size - 16)) {
        self->publish_event_("HTTP WAV: bad fmt extension");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        self->engine_test_active_ = false;
        self->audio_engine_.clear();
        vTaskDelete(nullptr);
        return;
      }
    } else if (is_fourcc(chunk_header, "data")) {
      data_size = chunk_size;
      data_found = true;
    } else {
      if (!skip_bytes(client, chunk_size)) {
        ESP_LOGE(HTTP_TAG, "Failed to skip WAV chunk");
        self->publish_event_("HTTP WAV: unsupported/truncated chunk");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        self->engine_test_active_ = false;
        self->audio_engine_.clear();
        vTaskDelete(nullptr);
        return;
      }
    }
  }
  if (!fmt_found || audio_format != 1 || channels != 2 || sample_rate != 44100 || bits_per_sample != 16) {
    ESP_LOGE(HTTP_TAG, "Unsupported WAV: format=%u channels=%u rate=%u bits=%u",
             static_cast<unsigned>(audio_format), static_cast<unsigned>(channels),
             static_cast<unsigned>(sample_rate), static_cast<unsigned>(bits_per_sample));
    self->publish_event_("HTTP WAV: need PCM 44.1k/16bit/stereo");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    self->engine_test_active_ = false;
    self->audio_engine_.clear();
    vTaskDelete(nullptr);
    return;
  }
  ESP_LOGI(HTTP_TAG, "WAV accepted: PCM 44.1 kHz / 16 bit / stereo, data=%u bytes", static_cast<unsigned>(data_size));
  self->publish_event_("HTTP WAV: WAV accepted, pre-filling PCM");
  uint8_t pcm[1024];
  uint32_t remaining = data_size;
  constexpr uint8_t PREFILL_PERCENT = 70;
  while (remaining > 0 && self->audio_engine_.fill_percent() < PREFILL_PERCENT) {
    const size_t wanted = remaining > sizeof(pcm) ? sizeof(pcm) : remaining;
    if (!read_exact(client, pcm, wanted)) {
      ESP_LOGE(HTTP_TAG, "WAV data ended during prefill");
      self->publish_event_("HTTP WAV: truncated PCM data");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      self->engine_test_active_ = false;
      self->audio_engine_.clear();
      vTaskDelete(nullptr);
      return;
    }
    size_t offset = 0;
    while (offset < wanted) {
      const size_t written = self->audio_engine_.write(pcm + offset, wanted - offset);
      offset += written;
      if (written == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    remaining -= static_cast<uint32_t>(wanted);
  }
  self->engine_test_active_ = true;
  self->publish_event_("HTTP WAV: PCM playback started");
  while (remaining > 0 && self->engine_test_active_) {
    if (!self->a2dp_source_.is_active()) {
      ESP_LOGW(HTTP_TAG, "Bluetooth speaker disconnected during HTTP WAV playback");
      self->publish_event_("HTTP WAV: speaker disconnected");
      break;
    }
    const size_t wanted = remaining > sizeof(pcm) ? sizeof(pcm) : remaining;
    if (!read_exact(client, pcm, wanted)) {
      ESP_LOGE(HTTP_TAG, "WAV data ended unexpectedly");
      self->publish_event_("HTTP WAV: network/data error");
      break;
    }
    size_t offset = 0;
    while (offset < wanted && self->engine_test_active_) {
      const size_t written = self->audio_engine_.write(pcm + offset, wanted - offset);
      offset += written;
      if (written == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    remaining -= static_cast<uint32_t>(wanted);
  }
  const TickType_t drain_start = xTaskGetTickCount();
  while (self->audio_engine_.available() > 0 &&
         (xTaskGetTickCount() - drain_start) < pdMS_TO_TICKS(1000) &&
         self->a2dp_source_.is_active()) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  self->engine_test_active_ = false;
  self->audio_engine_.clear();
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (remaining == 0) {
    self->publish_event_("HTTP WAV: playback finished");
  } else {
    self->publish_event_("HTTP WAV: playback stopped");
  }
  vTaskDelete(nullptr);
}

}  // namespace bt_audio_bridge
}  // namespace esphome
