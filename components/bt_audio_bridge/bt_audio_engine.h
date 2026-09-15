#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace bt_audio_bridge {

class BtAudioEngine {
 public:
  // Static ring buffer: no heap allocation during A2DP start/playback.
  // 8 KiB is ~46 ms of 44.1 kHz / 16-bit stereo PCM.
  static constexpr size_t BUFFER_SIZE = 8 * 1024;

  bool begin();
  void end();
  void clear();
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
  uint8_t buffer_[BUFFER_SIZE]{};
  volatile size_t read_pos_{0};
  volatile size_t write_pos_{0};
  volatile size_t used_{0};
  volatile uint32_t underruns_{0};
  volatile uint32_t overruns_{0};
  volatile uint32_t bytes_written_{0};
  volatile uint32_t bytes_read_{0};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
