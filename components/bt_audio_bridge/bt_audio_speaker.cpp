#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

namespace esphome {
namespace bt_audio_bridge {

static const char *const SPEAKER_TAG = "bt_audio_bridge.speaker";

void BtAudioBridge::start() {
  if (!this->a2dp_started_ || !this->a2dp_source_.is_active()) {
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
  this->engine_test_active_ = true;
  this->speaker_started_ = true;
  this->state_ = speaker::STATE_RUNNING;
}

void BtAudioBridge::stop() {
  this->engine_test_active_ = false;
  this->finish_requested_ = false;
  this->speaker_started_ = false;
  this->audio_engine_.clear();
  this->audio_engine_.end();
  this->state_ = speaker::STATE_STOPPED;
}

void BtAudioBridge::finish() {
  if (!this->speaker_started_) {
    this->state_ = speaker::STATE_STOPPED;
    this->audio_engine_.end();
    return;
  }

  this->finish_requested_ = true;
  this->state_ = speaker::STATE_STOPPING;
}

size_t BtAudioBridge::play(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0) return 0;

  if (!this->speaker_started_ || this->state_ == speaker::STATE_STOPPED) {
    this->start();
  }

  if (!this->speaker_started_) return 0;

  return this->audio_engine_.write(data, length);
}

bool BtAudioBridge::has_buffered_data() const {
  const bool buffered = this->audio_engine_.available() != 0;

  if (!buffered && this->finish_requested_) {
    auto *self = const_cast<BtAudioBridge *>(this);
    self->engine_test_active_ = false;
    self->speaker_started_ = false;
    self->finish_requested_ = false;
    self->audio_engine_.clear();
    self->audio_engine_.end();
    self->state_ = speaker::STATE_STOPPED;
  }

  return buffered;
}

}  // namespace bt_audio_bridge
}  // namespace esphome
