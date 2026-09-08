#include "bt_audio_bridge.h"

#include <cstdint>

#include "esphome/core/log.h"

namespace esphome {
namespace bt_audio_bridge {

static const char *const TEST_AUDIO_TAG = "bt_audio_bridge";

BtAudioBridge::BtAudioBridge() {
  // The A2DP library calls this callback whenever the Bluetooth speaker
  // asks for PCM samples. Normally we return 0 (no audio). During the
  // test tone we generate a short 440 Hz square wave.
  this->a2dp_source_.set_data_callback(&BtAudioBridge::test_tone_callback_);
}

void BtAudioBridge::start_test_tone() {
  if (!this->a2dp_started_ || !this->connected_) {
    ESP_LOGW(TEST_AUDIO_TAG, "Test audio requested but Bluetooth speaker is not connected");
    this->publish_event_("AUDIO TEST: speaker not connected");
    return;
  }

  this->test_tone_phase_ = 0;
  this->test_tone_active_ = true;
  this->test_tone_until_ = millis() + 3000;
  this->publish_event_("AUDIO TEST: 440 Hz tone for 3 seconds");
  ESP_LOGI(TEST_AUDIO_TAG, "Starting 440 Hz test tone for 3 seconds");
}

void BtAudioBridge::stop_test_tone() {
  if (!this->test_tone_active_) return;
  this->test_tone_active_ = false;
  this->publish_event_("AUDIO TEST: stopped");
  ESP_LOGI(TEST_AUDIO_TAG, "Test tone stopped");
}

int32_t BtAudioBridge::test_tone_callback_(uint8_t *data, int32_t len) {
  if (global_bt_audio_bridge == nullptr) return 0;
  return global_bt_audio_bridge->generate_test_tone_(data, len);
}

int32_t BtAudioBridge::generate_test_tone_(uint8_t *data, int32_t len) {
  if (data == nullptr || len <= 0 || !this->test_tone_active_) return 0;

  // Stop automatically after 3 seconds. millis() subtraction also works
  // correctly across the 32-bit millis() rollover.
  if (static_cast<int32_t>(millis() - this->test_tone_until_) >= 0) {
    this->test_tone_active_ = false;
    return 0;
  }

  // ESP32-A2DP source audio is 44.1 kHz, 16-bit, stereo PCM here.
  // A 100-sample period gives an easy-to-hear ~441 Hz test tone.
  constexpr int16_t AMPLITUDE = 12000;
  constexpr uint32_t PERIOD_SAMPLES = 100;
  int16_t *samples = reinterpret_cast<int16_t *>(data);
  const int32_t sample_count = len / static_cast<int32_t>(sizeof(int16_t));

  for (int32_t i = 0; i + 1 < sample_count; i += 2) {
    const int16_t sample = (this->test_tone_phase_ < PERIOD_SAMPLES / 2) ? AMPLITUDE : -AMPLITUDE;
    samples[i] = sample;
    samples[i + 1] = sample;
    this->test_tone_phase_++;
    if (this->test_tone_phase_ >= PERIOD_SAMPLES) this->test_tone_phase_ = 0;
  }

  return sample_count * static_cast<int32_t>(sizeof(int16_t));
}

}  // namespace bt_audio_bridge
}  // namespace esphome
