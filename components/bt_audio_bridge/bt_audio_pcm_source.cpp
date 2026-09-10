#include "bt_audio_pcm_source.h"

#include <cmath>
#include <cstring>

namespace esphome {
namespace bt_audio_bridge {

void BtAudioPcmSource::reset(float frequency, float amplitude) {
  this->frequency_ = frequency;
  this->amplitude_ = amplitude;
  this->phase_ = 0.0f;
}

size_t BtAudioPcmSource::generate(uint8_t *buffer, size_t bytes) {
  if (buffer == nullptr || bytes < BYTES_PER_FRAME) return 0;

  const size_t usable = bytes - (bytes % BYTES_PER_FRAME);
  auto *out = reinterpret_cast<int16_t *>(buffer);
  const size_t frames = usable / BYTES_PER_FRAME;
  constexpr float two_pi = 6.28318530717958647692f;
  const float phase_step = two_pi * this->frequency_ / static_cast<float>(SAMPLE_RATE);

  for (size_t i = 0; i < frames; i++) {
    const int16_t value = static_cast<int16_t>(std::sin(this->phase_) * 32767.0f * this->amplitude_);
    out[i * 2U] = value;
    out[i * 2U + 1U] = value;

    this->phase_ += phase_step;
    if (this->phase_ >= two_pi) this->phase_ -= two_pi;
  }

  return usable;
}

}  // namespace bt_audio_bridge
}  // namespace esphome
