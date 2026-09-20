#include "bt_audio_engine.h"

#include <cstring>

#include <freertos/task.h>

namespace esphome {
namespace bt_audio_bridge {

bool BtAudioEngine::begin() {
  if (this->buffer_ != nullptr) return true;

  this->buffer_ = xStreamBufferCreateStatic(
      BUFFER_SIZE, TRIGGER_LEVEL, this->buffer_storage_, &this->static_buffer_);

  if (this->buffer_ == nullptr) return false;

  this->underruns_ = 0;
  this->overruns_ = 0;
  this->bytes_written_ = 0;
  this->bytes_read_ = 0;
  return true;
}

void BtAudioEngine::end() {
  // Static stream buffer: nothing to free.
  this->buffer_ = nullptr;
}

void BtAudioEngine::clear() {
  if (this->buffer_ != nullptr) {
    xStreamBufferReset(this->buffer_);
  }
}

size_t BtAudioEngine::write(const uint8_t *data, size_t len, TickType_t ticks_to_wait) {
  if (this->buffer_ == nullptr || data == nullptr || len == 0) return 0;

  size_t total_written = 0;

  while (total_written < len) {
    const size_t remaining = len - total_written;
    const size_t chunk = (remaining > (BUFFER_SIZE - 1)) ? (BUFFER_SIZE - 1) : remaining;

    // Let the A2DP callback drain the PCM buffer instead of returning a
    // partial write and making ESPHome's AudioPipeline immediately retry.
    // This is the contract exposed by Speaker::play(..., ticks_to_wait).
    // The PCM buffer is intentionally small (2048 B ~= 11.6 ms at
    // 44.1 kHz/stereo/16-bit). One RTOS tick is too short: the decoder then
    // gets a partial write on almost every call, so ESPHome keeps its large
    // transfer/decoder buffers alive and never reaches finish().
    //
    // 10 ms is still a hard upper bound, so this cannot turn backpressure
    // into the unbounded blocking that caused the earlier WDT. In normal
    // playback the A2DP callback drains the 2048 B buffer in about 12 ms and
    // the whole decoder chunk can be accepted in one call.
    const TickType_t wait = pdMS_TO_TICKS(10);

    const size_t written = xStreamBufferSend(
        this->buffer_, data + total_written, chunk, wait);

    total_written += written;
    this->bytes_written_ += static_cast<uint32_t>(written);

    if (written < chunk) {
      this->overruns_++;
      break;
    }
  }

  return total_written;
}

size_t BtAudioEngine::read(uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) return 0;

  if (this->buffer_ == nullptr) {
    std::memset(data, 0, len);
    this->underruns_++;
    return len;
  }

  const size_t received = xStreamBufferReceive(this->buffer_, data, len, 0);
  this->bytes_read_ += static_cast<uint32_t>(received);

  if (received < len) {
    std::memset(data + received, 0, len - received);
    this->underruns_++;
  }

  return len;
}

size_t BtAudioEngine::available() const {
  if (this->buffer_ == nullptr) return 0;
  return xStreamBufferBytesAvailable(this->buffer_);
}

uint8_t BtAudioEngine::fill_percent() const {
  const size_t used = this->available();
  return static_cast<uint8_t>((used * 100U) / BUFFER_SIZE);
}

}  // namespace bt_audio_bridge
}  // namespace esphome
