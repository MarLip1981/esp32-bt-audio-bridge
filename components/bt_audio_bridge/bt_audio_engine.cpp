#include "bt_audio_engine.h"

#include <cstring>

namespace esphome {
namespace bt_audio_bridge {

bool BtAudioEngine::begin() {
  // The ring buffer lives in the object itself. begin() must never allocate.
  this->clear();
  this->underruns_ = 0;
  this->overruns_ = 0;
  this->bytes_written_ = 0;
  this->bytes_read_ = 0;
  return true;
}

void BtAudioEngine::end() {
  // Intentionally no free/delete: the PCM ring buffer is static storage.
  this->clear();
}

void BtAudioEngine::clear() {
  this->read_pos_ = 0;
  this->write_pos_ = 0;
  this->used_ = 0;
}

size_t BtAudioEngine::write(const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) return 0;

  size_t free_bytes = BUFFER_SIZE - this->used_;
  if (free_bytes == 0) {
    this->overruns_++;
    return 0;
  }

  const size_t to_write = len < free_bytes ? len : free_bytes;
  size_t first = BUFFER_SIZE - this->write_pos_;
  if (first > to_write) first = to_write;
  std::memcpy(this->buffer_ + this->write_pos_, data, first);

  const size_t second = to_write - first;
  if (second != 0) std::memcpy(this->buffer_, data + first, second);

  this->write_pos_ = (this->write_pos_ + to_write) % BUFFER_SIZE;
  this->used_ += to_write;
  this->bytes_written_ += static_cast<uint32_t>(to_write);

  if (to_write < len) this->overruns_++;
  return to_write;
}

size_t BtAudioEngine::read(uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) return 0;

  const size_t available_bytes = this->used_;
  const size_t to_read = len < available_bytes ? len : available_bytes;

  if (to_read != 0) {
    size_t first = BUFFER_SIZE - this->read_pos_;
    if (first > to_read) first = to_read;
    std::memcpy(data, this->buffer_ + this->read_pos_, first);

    const size_t second = to_read - first;
    if (second != 0) std::memcpy(data + first, this->buffer_, second);

    this->read_pos_ = (this->read_pos_ + to_read) % BUFFER_SIZE;
    this->used_ -= to_read;
    this->bytes_read_ += static_cast<uint32_t>(to_read);
  }

  if (to_read < len) {
    std::memset(data + to_read, 0, len - to_read);
    this->underruns_++;
  }

  // Keep the A2DP callback contract: always return the requested PCM length.
  return len;
}

size_t BtAudioEngine::available() const {
  return this->used_;
}

uint8_t BtAudioEngine::fill_percent() const {
  const size_t used = this->available();
  return static_cast<uint8_t>((used * 100U) / BUFFER_SIZE);
}

}  // namespace bt_audio_bridge
}  // namespace esphome
