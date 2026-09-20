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

size_t BtAudioEngine::write(const uint8_t *data, size_t len) {
  if (this->buffer_ == nullptr || data == nullptr || len == 0) return 0;

  const size_t max_write = BUFFER_SIZE - 1;
  const size_t request = (len > max_write) ? max_write : len;

  const size_t written = xStreamBufferSend(this->buffer_, data, request, 0);
  this->bytes_written_ += static_cast<uint32_t>(written);

  if (written < len) {
    this->overruns_++;

    // ESPHome's AudioPipeline can immediately call Speaker::play() again
    // when only part of a decoder block was accepted. Our buffer is
    // intentionally non-blocking, so returning a partial write without
    // yielding can make the decoder task spin on a full buffer and starve
    // Core 1 long enough to trigger the Task WDT.
    //
    // One RTOS tick gives the A2DP consumer time to drain the buffer before
    // AudioPipeline retries the remaining PCM. This is especially important
    // when len > BUFFER_SIZE - 1, because the cap itself necessarily makes
    // the write partial even when the buffer was otherwise empty.
    vTaskDelay(1);
  }

  return written;
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
