#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
namespace bt_audio_bridge {

int32_t BtAudioBridge::engine_audio_callback_(uint8_t *data, int32_t len) {
  if (global_bt_audio_bridge == nullptr) {
    std::memset(data, 0, static_cast<size_t>(len));
    return len;
  }
  return static_cast<int32_t>(global_bt_audio_bridge->audio_engine_.read(data, static_cast<size_t>(len)));
}

void BtAudioBridge::start_engine_test() {
  if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) {
    ESP_LOGW("bt_audio_bridge", "Audio engine test requested while Bluetooth speaker is not connected");
    this->publish_event_("ENGINE: speaker not connected");
    return;
  }

  if (this->engine_test_active_) {
    ESP_LOGW("bt_audio_bridge", "Audio engine test already running");
    return;
  }

  if (!this->audio_engine_.begin()) {
    ESP_LOGE("bt_audio_bridge", "Audio engine buffer allocation failed");
    this->publish_event_("ENGINE: buffer allocation FAILED");
    return;
  }

  this->audio_engine_.clear();
  this->engine_test_active_ = true;
  this->a2dp_source_.set_data_callback(&BtAudioBridge::engine_audio_callback_);
  this->publish_event_("ENGINE: PCM ring-buffer test started");

  BaseType_t result = xTaskCreate(
      &BtAudioBridge::engine_test_task_,
      "bt_audio_engine",
      4096,
      this,
      1,
      nullptr);

  if (result != pdPASS) {
    this->engine_test_active_ = false;
    this->a2dp_source_.set_data_callback(&BtAudioBridge::test_tone_callback_);
    ESP_LOGE("bt_audio_bridge", "Audio engine test task creation failed");
    this->publish_event_("ENGINE: test task FAILED");
  }
}

void BtAudioBridge::engine_test_task_(void *arg) {
  auto *self = static_cast<BtAudioBridge *>(arg);
  constexpr uint32_t test_duration_ms = 5000;
  constexpr uint32_t chunk_ms = 10;
  constexpr size_t bytes_per_second = 44100U * 2U * 2U;
  constexpr size_t chunk_bytes = bytes_per_second * chunk_ms / 1000U;
  constexpr double sample_rate = 44100.0;
  constexpr double frequency = 440.0;
  constexpr double two_pi = 6.28318530717958647692;

  uint8_t pcm[chunk_bytes];
  uint32_t phase = 0;
  const TickType_t start = xTaskGetTickCount();
  const TickType_t duration = pdMS_TO_TICKS(test_duration_ms);

  while (self->engine_test_active_ && (xTaskGetTickCount() - start) < duration) {
    auto *out = reinterpret_cast<int16_t *>(pcm);
    constexpr size_t samples = chunk_bytes / 4U;

    for (size_t i = 0; i < samples; i++) {
      const double sample = std::sin(two_pi * frequency * static_cast<double>(phase) / sample_rate) * 0.18;
      const int16_t value = static_cast<int16_t>(sample * 32767.0);
      out[i * 2U] = value;
      out[i * 2U + 1U] = value;
      phase++;
    }

    self->audio_engine_.write(pcm, sizeof(pcm));
    vTaskDelay(pdMS_TO_TICKS(chunk_ms));
  }

  self->engine_test_active_ = false;
  self->a2dp_source_.set_data_callback(&BtAudioBridge::test_tone_callback_);

  ESP_LOGI("bt_audio_bridge",
           "Audio engine test finished: buffer=%u%% underruns=%u overruns=%u written=%u read=%u",
           static_cast<unsigned>(self->audio_engine_.fill_percent()),
           static_cast<unsigned>(self->audio_engine_.underruns()),
           static_cast<unsigned>(self->audio_engine_.overruns()),
           static_cast<unsigned>(self->audio_engine_.bytes_written()),
           static_cast<unsigned>(self->audio_engine_.bytes_read()));
  self->publish_event_("ENGINE: PCM ring-buffer test finished");

  vTaskDelete(nullptr);
}

}  // namespace bt_audio_bridge
}  // namespace esphome
