#pragma once

#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

namespace esphome {
namespace bt_audio_bridge {

class BtAudioEngine {
 public:
  // Static PCM buffer: avoids a ~4 KiB heap allocation and leaves the
  // Classic BT stack enough contiguous heap for A2DP/SBC TX buffers.
  // 2048 bytes is ~11.6 ms at 44.1 kHz stereo / 16-bit.
  static constexpr size_t BUFFER_SIZE = 2048;
  static constexpr size_t TRIGGER_LEVEL = 1;

  bool begin();
  void end();
  void clear();

  // Non-blocking producer/consumer operations.
  // The write path is deliberately capped so FreeRTOS never receives a
  // request larger than the stream buffer can hold.
  size_t write(const uint8_t *data, size_t len);
  size_t read(uint8_t *data, size_t len);

  size_t available() const;
  size_t capacity() const { return BUFFER_SIZE; }
  uint8_t fill_percent() const;

  uint32_t underruns() const { return this->underruns_; }
  uint32_t overruns() const { return this->overruns_; }
  uint32_t bytes_written() const { return this->bytes_written_; }
  uint32_t bytes_read() const { return this->bytes_read_; }

 private:
  StreamBufferHandle_t buffer_{nullptr};
  StaticStreamBuffer_t static_buffer_{};
  uint8_t buffer_storage_[BUFFER_SIZE + 1]{};

  volatile uint32_t underruns_{0};
  volatile uint32_t overruns_{0};
  volatile uint32_t bytes_written_{0};
  volatile uint32_t bytes_read_{0};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
