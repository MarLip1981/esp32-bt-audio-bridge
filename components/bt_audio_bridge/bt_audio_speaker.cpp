#include "bt_audio_bridge.h"

#include "esphome/core/log.h"

namespace esphome {
namespace bt_audio_bridge {

static const char *const SPEAKER_TAG = "bt_audio_bridge.speaker";

void BtAudioBridge::start() {
  if (!this->a2dp_started_ || !this->a2dp_source_.is_connected()) {
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
  // Do not enable A2DP media until the first PCM bytes are actually queued.
  // This prevents the BT stack from repeatedly allocating SBC TX buffers
  // while HA is still preparing the decoder/TTS output.
  this->a2dp_source_.set_media_enabled(false);
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

#ifdef USE_ESP32
size_t BtAudioBridge::play(const uint8_t *data, size_t length, TickType_t ticks_to_wait) {
  if (data == nullptr || length == 0) return 0;

  if (!this->a2dp_started_ || !this->a2dp_source_.is_connected()) {
    this->speaker_started_ = false;
    this->state_ = speaker::STATE_STOPPED;
    return 0;
  }

  if (!this->speaker_started_ || this->state_ == speaker::STATE_STOPPED) {
    this->start();
  }

  if (!this->speaker_started_) return 0;

  const size_t written = this->audio_engine_.write(data, length, ticks_to_wait);
  this->pcm_received_bytes_ += static_cast<uint32_t>(length);
  this->pcm_queued_bytes_ += static_cast<uint32_t>(written);
  if (written > 0) {
    // Start the A2DP media path only after PCM is waiting in the buffer.
    // The callback can then immediately provide real audio instead of
    // repeatedly returning 0 while the SBC/TX path is being initialized.
    this->a2dp_source_.set_media_enabled(true);
  }
  return written;
}
#endif

size_t BtAudioBridge::play(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0) return 0;

  // AudioPipeline may still deliver buffered decoder data after Bluetooth
  // disconnects. Do not repeatedly call start() in that state: start() logs
  // on every rejected buffer and can flood the UART/logger task badly enough
  // to trigger the Task WDT.
  if (!this->a2dp_started_ || !this->a2dp_source_.is_connected()) {
    this->speaker_started_ = false;
    this->state_ = speaker::STATE_STOPPED;
    return 0;
  }

  if (!this->speaker_started_ || this->state_ == speaker::STATE_STOPPED) {
    this->start();
  }

  if (!this->speaker_started_) return 0;

  const size_t written = this->audio_engine_.write(data, length);
  this->pcm_received_bytes_ += static_cast<uint32_t>(length);
  this->pcm_queued_bytes_ += static_cast<uint32_t>(written);

  if (written > 0) {
    // CRITICAL DIAGNOSTIC FIX — 2026-09-21
    // ESPHome can feed this 2-argument Speaker::play() overload for the
    // normal media pipeline. The previous code enabled A2DP media only in
    // the TickType_t overload. As a result PCM was queued successfully, but
    // the A2DP source never entered its media-start path: A2DP_cb stayed 0,
    // the PCM buffer remained full, and finish() waited forever until the
    // user pressed STOP in Home Assistant.
    //
    // The Bluetooth connection itself can remain CONNECTED while A2DP media
    // is not started. Enable media as soon as real PCM is queued, matching
    // the working behavior of the other play() overload.
    this->a2dp_source_.set_media_enabled(true);
  }

  return written;
}

bool BtAudioBridge::has_buffered_data() const {
  return this->audio_engine_.available() != 0;
}

}  // namespace bt_audio_bridge
}  // namespace esphome
