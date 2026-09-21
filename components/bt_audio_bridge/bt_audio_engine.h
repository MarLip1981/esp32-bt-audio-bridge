#pragma once

#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

namespace esphome {
namespace bt_audio_bridge {

class BtAudioEngine {
 public:
  // DIAGNOSTIC EXPERIMENT — 2026-09-21
  // Increased PCM buffer from 2048 B to 8192 B.
  // Reason: the 2048 B buffer gave only ~11.6 ms of stereo 16-bit / 44.1 kHz
  // audio and the A2DP path showed repeated underflows in the previous test.
  // 8192 B provides ~46 ms of PCM and should give the A2DP task more scheduling margin.
  //
  // ROLLBACK POINT:
  // Previous value was 2048 B.
  // Keep this comment and commit history intact while diagnosing the audio path.
  static constexpr size_t BUFFER_SIZE = 8192;
  static constexpr size_t TRIGGER_LEVEL = 1;

  bool begin();
  void end();
  void clear();

  // Non-blocking producer/consumer operations.
  // The write path is deliberately capped so FreeRTOS never receives a
  // request larger than the stream buffer can hold.
  size_t write(const uint8_t *data, size_t len, TickType_t ticks_to_wait = 0);
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
