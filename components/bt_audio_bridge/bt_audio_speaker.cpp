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
  this->media_enable_requested_ = false;

  // AUDIO SYNC FIX — 2026-09-21
  // Each HA playback gets a fresh A2DP callback counter. Without this reset,
  // a previous TTS could leave a2dp_callback_calls_ > 0 and the next TTS
  // would incorrectly skip the "wait for first callback" protection.
  // This is a per-stream diagnostic/guard reset; it does not reset the BT stack.
  //
  // ROLLBACK POINT:
  // Remove only these two counter resets if a future test proves that the
  // callback counter must remain cumulative across HA streams.
  this->a2dp_callback_calls_ = 0;
  this->a2dp_read_bytes_ = 0;

  // Reset per-stream diagnostic counters too, so the next AUDIO-DIAG line
  // starts from zero and can be compared directly with the new TTS stream.
  this->pcm_received_bytes_ = 0;
  this->pcm_queued_bytes_ = 0;
  this->last_diag_pcm_received_ = 0;
  this->last_diag_pcm_queued_ = 0;
  this->last_diag_a2dp_calls_ = 0;
  this->last_diag_a2dp_read_ = 0;
  this->last_audio_diag_ = millis();

  // AUDIO SYNC FIX — 2026-09-21
  // Arm A2DP immediately when HA starts the stream, but send the media-start
  // request exactly once. The previous experiment called set_media_enabled()
  // from every play() call while A2DP was still negotiating. The resulting
  // log contained a storm of "un-acked a2dp cmd: 2" messages and repeated
  // 4112-byte SBC allocation failures.
  //
  // ROLLBACK POINT:
  // Restore the old delayed media-enable sequence if a future test proves
  // that this library version requires it.
  this->a2dp_source_.set_media_enabled(true);
  this->media_enable_requested_ = true;

  this->engine_test_active_ = false;
  this->state_ = speaker::STATE_RUNNING;
  ESP_LOGI(SPEAKER_TAG, "HA audio stream START (A2DP armed once)");
}

void BtAudioBridge::stop() {
  this->finish_requested_ = false;
  this->speaker_started_ = false;
  this->a2dp_source_.set_media_enabled(false);
  this->media_enable_requested_ = false;
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
    this->media_enable_requested_ = false;
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
  // filling the 8 KiB bridge buffer while Bluetooth is still negotiating.
  // IMPORTANT: set_media_enabled() is NOT repeated here. start() already sent
  // the single media-start request for this stream.
  if (this->a2dp_callback_calls_ == 0) {
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
  // Wait for the first real A2DP data callback before accepting PCM. Unlike
  // the previous version, this path never sends another media-enable command.
  if (this->a2dp_callback_calls_ == 0) {
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
