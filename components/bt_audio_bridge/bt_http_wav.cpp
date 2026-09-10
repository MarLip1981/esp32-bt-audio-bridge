#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include <cstring>
#include <string>

namespace esphome {
namespace bt_audio_bridge {

static const char *const HTTP_TAG = "bt_http_wav";

namespace {

bool read_exact(Stream &stream, uint8_t *buffer, size_t length) {
  size_t received = 0;
  while (received < length) {
    const int count = stream.readBytes(buffer + received, length - received);
    if (count <= 0) return false;
    received += static_cast<size_t>(count);
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

bool skip_bytes(Stream &stream, uint32_t length) {
  uint8_t buffer[256];
  uint32_t remaining = length;
  while (remaining > 0) {
    const size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
    if (!read_exact(stream, buffer, wanted)) return false;
    remaining -= static_cast<uint32_t>(wanted);
  }
  if (length & 1U) {
    uint8_t padding = 0;
    if (!read_exact(stream, &padding, 1)) return false;
  }
  return true;
}

}  // namespace

void BtAudioBridge::set_audio_url(const char *url) {
  if (url == nullptr) {
    this->audio_url_[0] = '\0';
    return;
  }
  std::strncpy(this->audio_url_, url, sizeof(this->audio_url_) - 1);
  this->audio_url_[sizeof(this->audio_url_) - 1] = '\0';
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

  if (std::strncmp(this->audio_url_, "http://", 7) != 0) {
    ESP_LOGW(HTTP_TAG, "Only plain HTTP WAV URLs are supported in this stage");
    this->publish_event_("HTTP WAV: only http:// is supported");
    return;
  }

  if (!this->audio_engine_.begin()) {
    ESP_LOGE(HTTP_TAG, "Audio engine buffer allocation failed");
    this->publish_event_("HTTP WAV: audio buffer FAILED");
    return;
  }

  this->audio_engine_.clear();

  BaseType_t result = xTaskCreate(
      &BtAudioBridge::http_wav_task_,
      "bt_http_wav",
      6144,
      this,
      2,
      nullptr);

  if (result != pdPASS) {
    ESP_LOGE(HTTP_TAG, "HTTP WAV task creation failed");
    this->publish_event_("HTTP WAV: task FAILED");
    return;
  }

  this->publish_event_("HTTP WAV: download started");
}

void BtAudioBridge::http_wav_task_(void *arg) {
  auto *self = static_cast<BtAudioBridge *>(arg);
  std::string url(self->audio_url_);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);

  if (!http.begin(client, url.c_str())) {
    ESP_LOGE(HTTP_TAG, "HTTP begin failed");
    self->publish_event_("HTTP WAV: HTTP begin FAILED");
    self->engine_test_active_ = false;
    vTaskDelete(nullptr);
    return;
  }

  const int http_code = http.GET();
  if (http_code != HTTP_CODE_OK) {
    ESP_LOGE(HTTP_TAG, "HTTP GET failed, code=%d", http_code);
    self->publish_event_("HTTP WAV: HTTP GET FAILED");
    http.end();
    self->engine_test_active_ = false;
    vTaskDelete(nullptr);
    return;
  }

  Stream *stream = http.getStreamPtr();
  if (stream == nullptr) {
    ESP_LOGE(HTTP_TAG, "HTTP stream unavailable");
    self->publish_event_("HTTP WAV: stream FAILED");
    http.end();
    self->engine_test_active_ = false;
    vTaskDelete(nullptr);
    return;
  }

  stream->setTimeout(5000);

  uint8_t header[12];
  if (!read_exact(*stream, header, sizeof(header)) || !is_fourcc(header, "RIFF") || !is_fourcc(header + 8, "WAVE")) {
    ESP_LOGE(HTTP_TAG, "Invalid RIFF/WAVE header");
    self->publish_event_("HTTP WAV: invalid RIFF/WAVE");
    http.end();
    self->engine_test_active_ = false;
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
    if (!read_exact(*stream, chunk_header, sizeof(chunk_header))) {
      ESP_LOGE(HTTP_TAG, "Unexpected end of WAV while reading chunk header");
      self->publish_event_("HTTP WAV: truncated header");
      http.end();
      self->engine_test_active_ = false;
      vTaskDelete(nullptr);
      return;
    }

    const uint32_t chunk_size = le32(chunk_header + 4);

    if (is_fourcc(chunk_header, "fmt ")) {
      if (chunk_size < 16) {
        ESP_LOGE(HTTP_TAG, "Invalid fmt chunk size=%u", static_cast<unsigned>(chunk_size));
        self->publish_event_("HTTP WAV: invalid fmt chunk");
        http.end();
        self->engine_test_active_ = false;
        vTaskDelete(nullptr);
        return;
      }

      uint8_t fmt[16];
      if (!read_exact(*stream, fmt, sizeof(fmt))) {
        self->publish_event_("HTTP WAV: truncated fmt chunk");
        http.end();
        self->engine_test_active_ = false;
        vTaskDelete(nullptr);
        return;
      }

      audio_format = le16(fmt + 0);
      channels = le16(fmt + 2);
      sample_rate = le32(fmt + 4);
      bits_per_sample = le16(fmt + 14);
      fmt_found = true;

      if (chunk_size > 16 && !skip_bytes(stream, chunk_size - 16)) {
        self->publish_event_("HTTP WAV: bad fmt extension");
        http.end();
        self->engine_test_active_ = false;
        vTaskDelete(nullptr);
        return;
      }
      if (chunk_size == 16 && (chunk_size & 1U)) {
        uint8_t padding = 0;
        if (!read_exact(*stream, &padding, 1)) {
          self->publish_event_("HTTP WAV: bad fmt padding");
          http.end();
          self->engine_test_active_ = false;
          vTaskDelete(nullptr);
          return;
        }
      }
    } else if (is_fourcc(chunk_header, "data")) {
      data_size = chunk_size;
      data_found = true;
    } else {
      if (!skip_bytes(*stream, chunk_size)) {
        ESP_LOGE(HTTP_TAG, "Failed to skip WAV chunk");
        self->publish_event_("HTTP WAV: unsupported/truncated chunk");
        http.end();
        self->engine_test_active_ = false;
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
    http.end();
    self->engine_test_active_ = false;
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(HTTP_TAG, "WAV accepted: PCM 44.1 kHz / 16 bit / stereo, data=%u bytes", static_cast<unsigned>(data_size));
  self->publish_event_("HTTP WAV: WAV accepted, pre-filling PCM");

  uint8_t pcm[1024];
  uint32_t remaining = data_size;
  constexpr uint8_t PREFILL_PERCENT = 60;

  while (remaining > 0 && self->audio_engine_.fill_percent() < PREFILL_PERCENT) {
    const size_t wanted = remaining > sizeof(pcm) ? sizeof(pcm) : remaining;
    if (!read_exact(*stream, pcm, wanted)) {
      ESP_LOGE(HTTP_TAG, "WAV data ended during prefill");
      self->publish_event_("HTTP WAV: truncated PCM data");
      http.end();
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
    if (!read_exact(*stream, pcm, wanted)) {
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
  http.end();

  if (remaining == 0) {
    self->publish_event_("HTTP WAV: playback finished");
  } else {
    self->publish_event_("HTTP WAV: playback stopped");
  }

  vTaskDelete(nullptr);
}

}  // namespace bt_audio_bridge
}  // namespace esphome
