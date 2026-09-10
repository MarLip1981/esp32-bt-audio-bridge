#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace bt_audio_bridge {

// Produces PCM in the exact format expected by the A2DP source:
// 44.1 kHz, 16-bit, stereo, interleaved little-endian samples.
// The class owns no audio buffer; the caller supplies the destination.
class BtAudioPcmSource {
 public:
  static constexpr uint32_t SAMPLE_RATE = 44100;
  static constexpr uint32_t CHANNELS = 2;
  static constexpr uint32_t BYTES_PER_SAMPLE = 2;
  static constexpr uint32_t BYTES_PER_FRAME = CHANNELS * BYTES_PER_SAMPLE;

  void reset(float frequency = 440.0f, float amplitude = 0.10f);
  size_t generate(uint8_t *buffer, size_t bytes);

 private:
  float frequency_{440.0f};
  float amplitude_{0.10f};
  float phase_{0.0f};
};

}  // namespace bt_audio_bridge
}  // namespace esphome
