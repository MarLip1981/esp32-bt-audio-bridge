#include "bt_audio_engine.h"

#include <cstring>

namespace esphome {
namespace bt_audio_bridge {

bool BtAudioEngine::begin() {
  if (this->buffer_ != nullptr) return true;

  this->buffer_ = xStreamBufferCreate(BUFFER_SIZE, TRIGGER_LEVEL);
  if (this->buffer_ == nullptr) return false;

  this->underruns_ = 0;
  this->overruns_ = 0;
  this->bytes_written_ = 0;
  this->bytes_read_ = 0;
  return true;
}

void BtAudioEngine::end() {
  if (this->buffer_ != nullptr) {
    vStreamBufferDelete(this->buffer_);
    this->buffer_ = nullptr;
  }
}

void BtAudioEngine::clear() {
  if (this->buffer_ != nullptr) {
    xStreamBufferReset(this->buffer_);
  }
}

size_t BtAudioEngine::write(const uint8_t *data, size_t len) {
  if (this->buffer_ == nullptr || data == nullptr || len == 0) return 0;

  const size_t written = xStreamBufferSend(this->buffer_, data, len, 0);
  this->bytes_written_ += static_cast<uint32_t>(written);

  if (written < len) this->overruns_++;
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
