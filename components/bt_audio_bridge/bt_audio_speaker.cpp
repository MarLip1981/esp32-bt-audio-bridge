#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

namespace esphome {
namespace bt_audio_bridge {

static const char *const SPEAKER_TAG = "bt_audio_bridge.speaker";

void BtAudioBridge::start() {
  if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) {
    ESP_LOGW(SPEAKER_TAG, "Cannot start HA audio: Bluetooth speaker is not active");
    this->state_ = speaker::STATE_STOPPED;
    return;
  }

  if (!this->audio_engine_.begin()) {
    ESP_LOGE(SPEAKER_TAG, "Failed to allocate PCM audio buffer");
    this->state_ = speaker::STATE_STOPPED;
    return;
  }

  this->audio_engine_.clear();
  this->finish_requested_ = false;
  this->speaker_started_ = true;
  this->a2dp_source_.set_media_enabled(true);
  this->engine_test_active_ = false;
  this->state_ = speaker::STATE_RUNNING;
  ESP_LOGI(SPEAKER_TAG, "HA audio stream START");
}

void BtAudioBridge::stop() {
  this->finish_requested_ = false;
  this->speaker_started_ = false;
  this->a2dp_source_.set_media_enabled(false);
  this->engine_test_active_ = false;
  this->audio_engine_.clear();
  this->state_ = speaker::STATE_STOPPED;
  ESP_LOGI(SPEAKER_TAG, "HA audio stream STOP");
}

void BtAudioBridge::finish() {
  this->finish_requested_ = true;

  if (this->audio_engine_.available() == 0) {
    this->speaker_started_ = false;
    this->a2dp_source_.set_media_enabled(false);
    this->finish_requested_ = false;
    this->state_ = speaker::STATE_STOPPED;
    ESP_LOGI(SPEAKER_TAG, "HA audio stream FINISHED");
  } else {
    this->state_ = speaker::STATE_STOPPING;
  }
}

size_t BtAudioBridge::play(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0) return 0;

  // AudioPipeline may still deliver buffered decoder data after Bluetooth
  // disconnects. Do not repeatedly call start() in that state: start() logs
  // on every rejected buffer and can flood the UART/logger task badly enough
  // to trigger the Task WDT.
  if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) {
    this->speaker_started_ = false;
    this->state_ = speaker::STATE_STOPPED;
    return 0;
  }

  if (!this->speaker_started_ || this->state_ == speaker::STATE_STOPPED) {
    this->start();
  }

  if (!this->speaker_started_) return 0;

  return this->audio_engine_.write(data, length);
}

bool BtAudioBridge::has_buffered_data() const {
  return this->audio_engine_.available() != 0;
}

}  // namespace bt_audio_bridge
}  // namespace esphome
