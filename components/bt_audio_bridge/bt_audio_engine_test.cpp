#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
namespace bt_audio_bridge {

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
      3072,
      this,
      3,
      nullptr);

  if (result != pdPASS) {
    this->engine_test_active_ = false;
    this->a2dp_source_.set_data_callback(&BtAudioBridge::test_tone_callback_);
    this->audio_engine_.end();
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
  constexpr float sample_rate = 44100.0f;
  constexpr float frequency = 440.0f;
  constexpr float two_pi = 6.28318530717958647692f;
  constexpr float amplitude = 0.18f;

  uint8_t pcm[chunk_bytes];
  float phase = 0.0f;
  const float phase_step = two_pi * frequency / sample_rate;
  const TickType_t start = xTaskGetTickCount();
  const TickType_t duration = pdMS_TO_TICKS(test_duration_ms);

  auto fill_pcm = [&]() {
    auto *out = reinterpret_cast<int16_t *>(pcm);
    constexpr size_t samples = chunk_bytes / 4U;
    for (size_t i = 0; i < samples; i++) {
      const float sample = sinf(phase) * amplitude;
      const int16_t value = static_cast<int16_t>(sample * 32767.0f);
      out[i * 2U] = value;
      out[i * 2U + 1U] = value;
      phase += phase_step;
      if (phase >= two_pi) phase -= two_pi;
    }
  };

  // Keep the diagnostic buffer private to this test. Two chunks provide
  // startup headroom, while the blocking write below prevents dropping PCM
  // when the 4 KiB buffer is temporarily full.
  constexpr size_t prefill_chunks = 2;
  for (size_t chunk = 0; chunk < prefill_chunks && self->engine_test_active_; chunk++) {
    fill_pcm();
    size_t written = 0;
    while (written < sizeof(pcm) && self->engine_test_active_) {
      const size_t n = self->audio_engine_.write(pcm + written, sizeof(pcm) - written);
      written += n;
      if (n == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  TickType_t next_write = xTaskGetTickCount();

  while (self->engine_test_active_ && (xTaskGetTickCount() - start) < duration) {
    fill_pcm();

    // Do not silently throw away PCM when the ring buffer is full. The A2DP
    // consumer owns the timing; the producer waits briefly for room instead.
    size_t written = 0;
    while (written < sizeof(pcm) && self->engine_test_active_) {
      const size_t n = self->audio_engine_.write(pcm + written, sizeof(pcm) - written);
      written += n;
      if (n == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }

    next_write += pdMS_TO_TICKS(chunk_ms);
    const TickType_t now = xTaskGetTickCount();
    if (static_cast<int32_t>(next_write - now) > 0)
      vTaskDelay(next_write - now);
    else
      taskYIELD();
  }

  self->engine_test_active_ = false;
  self->a2dp_source_.set_data_callback(&BtAudioBridge::test_tone_callback_);

  const unsigned buffer = static_cast<unsigned>(self->audio_engine_.fill_percent());
  const unsigned underruns = static_cast<unsigned>(self->audio_engine_.underruns());
  const unsigned overruns = static_cast<unsigned>(self->audio_engine_.overruns());
  const unsigned written = static_cast<unsigned>(self->audio_engine_.bytes_written());
  const unsigned read = static_cast<unsigned>(self->audio_engine_.bytes_read());

  ESP_LOGI("bt_audio_bridge",
           "Audio engine test finished: buffer=%u%% underruns=%u overruns=%u written=%u read=%u",
           buffer, underruns, overruns, written, read);
  self->publish_event_("ENGINE: PCM ring-buffer test finished");

  // The engine buffer is a diagnostic resource, not a permanent A2DP buffer.
  // Release it after every test so repeated tests cannot consume heap.
  self->audio_engine_.clear();
  self->audio_engine_.end();

  vTaskDelete(nullptr);
}

}  // namespace bt_audio_bridge
}  // namespace esphome
