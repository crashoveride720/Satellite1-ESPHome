#include "mic_rms_sensor.h"

#include <cmath>

#include "esphome/components/audio/audio.h"
#include "esphome/core/log.h"

namespace esphome {
namespace satellite1 {

static const char *const TAG = "satellite1.mic_rms";
static constexpr float Q31_SCALE = 2147483648.0f;
static constexpr float DBFS_FLOOR = -120.0f;

uint32_t Satellite1MicRmsSensor::isqrt_u64_(uint64_t x) {
  uint64_t op = x;
  uint64_t res = 0;
  uint64_t one = static_cast<uint64_t>(1) << 62;

  while (one > op) {
    one >>= 2;
  }

  while (one != 0) {
    if (op >= (res + one)) {
      op -= (res + one);
      res += (one << 1);
    }
    res >>= 1;
    one >>= 2;
  }

  return static_cast<uint32_t>(res);
}

void Satellite1MicRmsSensor::setup() {
  if (this->source_ != MicRmsSource::ESP_LOCAL || this->microphone_source_ == nullptr || this->callback_registered_) {
    return;
  }

  this->microphone_source_->add_data_callback(
      [this](const std::vector<uint8_t> &data) { this->process_esp_local_audio_(data); });
  this->callback_registered_ = true;
}

void Satellite1MicRmsSensor::process_esp_local_audio_(const std::vector<uint8_t> &data) {
  if (data.empty() || this->microphone_source_ == nullptr) {
    return;
  }

  const auto stream_info = this->microphone_source_->get_audio_stream_info();
  const uint8_t channels = stream_info.get_channels();
  const size_t bits_per_sample = stream_info.get_bits_per_sample();
  const size_t bytes_per_sample = bits_per_sample / 8;
  if (channels == 0 || channels > 4) {
    return;
  }
  if (bytes_per_sample == 0 || (data.size() % bytes_per_sample) != 0) {
    return;
  }

  const size_t sample_count = data.size() / bytes_per_sample;
  const size_t frame_count = sample_count / channels;
  if (frame_count == 0) {
    return;
  }

  for (size_t frame = 0; frame < frame_count; frame++) {
    for (size_t ch = 0; ch < channels; ch++) {
      const size_t sample_index = frame * channels + ch;
      const size_t byte_index = sample_index * bytes_per_sample;
      const int32_t s32 = audio::unpack_audio_sample_to_q31(&data[byte_index], bytes_per_sample);
      const int64_t s = static_cast<int64_t>(s32) >> MIC_RMS_SHIFT_BITS;
      this->local_energy_accum_[ch] += static_cast<uint64_t>(s * s);
    }
  }

  this->local_sample_count_ += static_cast<uint32_t>(frame_count);
  if (this->local_sample_count_ < MIC_RMS_WINDOW_SAMPLES) {
    return;
  }

  for (size_t ch = 0; ch < channels; ch++) {
    const uint64_t mean_energy = this->local_energy_accum_[ch] / this->local_sample_count_;
    const uint32_t rms_shifted = isqrt_u64_(mean_energy);
    this->local_rms_q31_[ch] = static_cast<int32_t>(static_cast<uint64_t>(rms_shifted) << MIC_RMS_SHIFT_BITS);
    this->local_energy_accum_[ch] = 0;
  }
  for (size_t ch = channels; ch < 4; ch++) {
    this->local_rms_q31_[ch] = 0;
    this->local_energy_accum_[ch] = 0;
  }
  this->local_sample_count_ = 0;
}

void Satellite1MicRmsSensor::update() {
  if (this->source_ == MicRmsSource::ESP_LOCAL) {
    if (!this->source_started_ && this->microphone_source_ != nullptr) {
      this->microphone_source_->start();
      this->source_started_ = true;
    }

    if (this->channel_ >= 4) {
      ESP_LOGW(TAG, "Invalid channel index: %u", this->channel_);
      return;
    }

    const int32_t rms_q31 = this->local_rms_q31_[this->channel_];
    float linear = static_cast<float>(rms_q31) / Q31_SCALE;
    if (linear < 0.0f) {
      linear = 0.0f;
    }

    float dbfs = DBFS_FLOOR;
    if (linear > 0.0f) {
      dbfs = 20.0f * log10f(linear);
      if (!std::isfinite(dbfs) || dbfs < DBFS_FLOOR) {
        dbfs = DBFS_FLOOR;
      }
    }
    this->publish_state(dbfs);
    return;
  }

  if (this->parent_ == nullptr) {
    return;
  }
  if (this->channel_ >= 4) {
    ESP_LOGW(TAG, "Invalid channel index: %u", this->channel_);
    return;
  }
  if (this->parent_->state != SAT_XMOS_CONNECTED_STATE) {
    return;
  }

  MicInputRmsStatus status{};
  if (!this->parent_->get_mic_rms_status(&status)) {
    ESP_LOGV(TAG, "Failed to read mic RMS status");
    return;
  }
  if (status.sample_count == 0) {
    return;
  }

  const int32_t rms_q31 = status.rms_q31[this->channel_];
  float linear = static_cast<float>(rms_q31) / Q31_SCALE;
  if (linear < 0.0f) {
    linear = 0.0f;
  }

  float dbfs = DBFS_FLOOR;
  if (linear > 0.0f) {
    dbfs = 20.0f * log10f(linear);
    if (!std::isfinite(dbfs) || dbfs < DBFS_FLOOR) {
      dbfs = DBFS_FLOOR;
    }
  }

  this->publish_state(dbfs);
}

}  // namespace satellite1
}  // namespace esphome
