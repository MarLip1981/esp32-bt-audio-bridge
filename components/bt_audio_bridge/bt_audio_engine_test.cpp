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
  this->engine_test_active_ = false;
  this->publish_event_("ENGINE: pre-filling PCM ring buffer");

  BaseType_t result = xTaskCreate(
      &BtAudioBridge::engine_test_task_,
      "bt_audio_engine",
      4096,
      this,
      2,
      nullptr);

  if (result != pdPASS) {
    ESP_LOGE("bt_audio_bridge", "Audio engine test task creation failed");
    this->publish_event_("ENGINE: test task FAILED");
  }
}

void BtAudioBridge::engine_test_task_(void *arg) {
  auto *self = static_cast<BtAudioBridge *>(arg);
  constexpr uint32_t test_duration_ms = 5000;
  constexpr uint32_t chunk_ms = 10;
  constexpr uint32_t prefill_ms = 150;
  constexpr size_t bytes_per_second = 44100U * 2U * 2U;
  constexpr size_t chunk_bytes = bytes_per_second * chunk_ms / 1000U;
  constexpr float sample_rate = 44100.0f;
  constexpr float frequency = 440.0f;
  constexpr float two_pi = 6.28318530717958647692f;
  constexpr float amplitude = 0.10f;
  constexpr uint8_t target_fill_percent = 70;

  uint8_t pcm[chunk_bytes];
  float phase = 0.0f;
  const float phase_step = two_pi * frequency / sample_rate;

  auto generate_chunk = [&]() {
    auto *out = reinterpret_cast<int16_t *>(pcm);
    constexpr size_t samples = chunk_bytes / 4U;
    for (size_t i = 0; i < samples; i++) {
      const int16_t value = static_cast<int16_t>(std::sin(phase) * 32767.0f * amplitude);
      out[i * 2U] = value;
      out[i * 2U + 1U] = value;
      phase += phase_step;
      if (phase >= two_pi) phase -= two_pi;
    }
  };

  // Fill the ring buffer before allowing the A2DP callback to consume it.
  const int prefill_chunks = prefill_ms / chunk_ms;
  for (int chunk = 0; chunk < prefill_chunks; chunk++) {
    generate_chunk();
    self->audio_engine_.write(pcm, sizeof(pcm));
  }

  self->engine_test_active_ = true;
  self->publish_event_("ENGINE: PCM ring-buffer playback started");

  const TickType_t start = xTaskGetTickCount();
  const TickType_t duration = pdMS_TO_TICKS(test_duration_ms);
  while (self->engine_test_active_ && (xTaskGetTickCount() - start) < duration) {
    // Do not rely on exact task timing. Keep a healthy amount of PCM queued,
    // but leave headroom so the producer cannot overrun the ring buffer.
    if (self->audio_engine_.fill_percent() < target_fill_percent) {
      generate_chunk();
      self->audio_engine_.write(pcm, sizeof(pcm));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  self->engine_test_active_ = false;
  const unsigned buffer = static_cast<unsigned>(self->audio_engine_.fill_percent());
  const unsigned underruns = static_cast<unsigned>(self->audio_engine_.underruns());
  const unsigned overruns = static_cast<unsigned>(self->audio_engine_.overruns());
  const unsigned written = static_cast<unsigned>(self->audio_engine_.bytes_written());
  const unsigned read = static_cast<unsigned>(self->audio_engine_.bytes_read());
  self->audio_engine_.clear();

  ESP_LOGI("bt_audio_bridge",
           "Audio engine test finished: buffer=%u%% underruns=%u overruns=%u written=%u read=%u",
           buffer, underruns, overruns, written, read);
  self->publish_event_(underruns == 0 && overruns == 0 ? "ENGINE: test OK - no underrun/overrun" : "ENGINE: test finished - check counters");
  vTaskDelete(nullptr);
}

}  // namespace bt_audio_bridge
}  // namespace esphome
