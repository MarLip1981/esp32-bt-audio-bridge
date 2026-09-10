#pragma once

#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

namespace esphome {
namespace bt_audio_bridge {

class BtAudioEngine {
 public:
  // 24 KiB of raw PCM = about 136 ms at 44.1 kHz / 16-bit / stereo.
  static constexpr size_t BUFFER_SIZE = 24 * 1024;
  static constexpr size_t TRIGGER_LEVEL = 1;

  bool begin();
  void end();
  void clear();

  // Non-blocking producer/consumer operations.
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
  volatile uint32_t underruns_{0};
  volatile uint32_t overruns_{0};
  volatile uint32_t bytes_written_{0};
  volatile uint32_t bytes_read_{0};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
