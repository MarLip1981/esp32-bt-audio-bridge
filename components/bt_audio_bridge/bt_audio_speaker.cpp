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

  // AUDIO SYNC FIX — 2026-09-21
  // The previous sequence waited for the first PCM bytes before enabling
  // A2DP media. On the original ESP32 the A2DP profile could then become
  // active ~29 s after HA reported PLAYING. During that delay ESPHome kept
  // feeding PCM into our 8 KiB buffer, which immediately saturated and the
  // eventual playback contained only the tail of the TTS.
  //
  // Arm the A2DP media path immediately when HA starts the stream. The play()
  // path below deliberately waits for the first A2DP callback before handing
  // PCM to the bridge. This makes the order deterministic:
  //   HA START -> A2DP media enabled -> A2DP callback -> PCM feed.
  //
  // ROLLBACK POINT:
  // Restore set_media_enabled(false) here and the old "enable after write"
  // logic in play() if this experiment proves incompatible with ESPHome's
  // Speaker pipeline.
  this->a2dp_source_.set_media_enabled(true);

  this->engine_test_active_ = false;
  this->state_ = speaker::STATE_RUNNING;
  ESP_LOGI(SPEAKER_TAG, "HA audio stream START (A2DP armed)");
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

  // Do not accept HA PCM before the A2DP source has actually requested data.
  // Returning 0 applies back-pressure to the Speaker pipeline instead of
  // filling the 8 KiB bridge buffer for seconds while Bluetooth is idle.
  if (this->a2dp_callback_calls_ == 0) {
    this->a2dp_source_.set_media_enabled(true);
    return 0;
  }

  const size_t written = this->audio_engine_.write(data, length, ticks_to_wait);
  this->pcm_received_bytes_ += static_cast<uint32_t>(length);
  this->pcm_queued_bytes_ += static_cast<uint32_t>(written);
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

  // AUDIO SYNC FIX — see start().
  // The previous implementation accepted PCM before the A2DP callback
  // existed. With the observed ~29 s media-start delay that consumed the
  // entire bridge buffer and left only the final fragment audible.
  if (this->a2dp_callback_calls_ == 0) {
    this->a2dp_source_.set_media_enabled(true);
    return 0;
  }

  const size_t written = this->audio_engine_.write(data, length);
  this->pcm_received_bytes_ += static_cast<uint32_t>(length);
  this->pcm_queued_bytes_ += static_cast<uint32_t>(written);
  return written;
}

bool BtAudioBridge::has_buffered_data() const {
  return this->audio_engine_.available() != 0;
}

}  // namespace bt_audio_bridge
}  // namespace esphome
