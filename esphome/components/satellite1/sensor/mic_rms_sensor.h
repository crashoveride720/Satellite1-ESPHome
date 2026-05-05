#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/microphone/microphone_source.h"
#include "esphome/core/component.h"
#include "../satellite1.h"

namespace esphome {
namespace satellite1 {

enum MicRmsSource : uint8_t {
  XMOS = 0,
  ESP_LOCAL = 1,
};

class Satellite1MicRmsSensor : public sensor::Sensor, public PollingComponent {
 public:
  void set_parent(Satellite1 *parent) { this->parent_ = parent; }
  void set_channel(uint8_t channel) { this->channel_ = channel; }
  void set_source(MicRmsSource source) { this->source_ = source; }
  void set_microphone_source(microphone::MicrophoneSource *source) { this->microphone_source_ = source; }
  void setup() override;
  void update() override;

 protected:
  static uint32_t isqrt_u64_(uint64_t x);
  void process_esp_local_audio_(const std::vector<uint8_t> &data);

  static constexpr uint8_t MIC_RMS_SHIFT_BITS = 8;
  static constexpr uint32_t MIC_RMS_WINDOW_SAMPLES = 3200;

  Satellite1 *parent_{nullptr};
  microphone::MicrophoneSource *microphone_source_{nullptr};
  MicRmsSource source_{MicRmsSource::XMOS};
  uint8_t channel_{0};
  bool callback_registered_{false};
  bool source_started_{false};

  uint64_t local_energy_accum_[4] = {0, 0, 0, 0};
  uint32_t local_sample_count_{0};
  int32_t local_rms_q31_[4] = {0, 0, 0, 0};
};

}  // namespace satellite1
}  // namespace esphome
