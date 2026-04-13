#include "mic_tests.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esp_dsp.h"

#include <cinttypes>
#include <cstdio>


namespace esphome {
namespace online_testing {

static const char *const TAG = "mic_tests";

static constexpr size_t INPUT_BUFFER_SIZE = 16000 * 2;

static const size_t SWEEP_LEN = 512;
static constexpr size_t MAX_BUFFER_SIZE = SWEEP_LEN * 2;
static constexpr float DETECTION_THRESHOLD = 0.25f;
static constexpr float DETECTION_RESET_THRESHOLD = 0.20f;
static constexpr uint32_t DETECTION_COOLDOWN_MS = 150;

// Aligned buffers
__attribute__((aligned(16))) float mic_buffer[MAX_BUFFER_SIZE];
__attribute__((aligned(16))) float sweep_f32[SWEEP_LEN];
__attribute__((aligned(16))) float window[SWEEP_LEN];
__attribute__((aligned(16))) float work_buf[SWEEP_LEN];

size_t mic_write_index = 0;
size_t mic_filled = 0;

static inline size_t wrap_index(size_t idx) {
    return idx % MAX_BUFFER_SIZE;
}

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>

float detect_sweep_streaming(
    const int16_t* chunk,
    size_t chunk_len,
    float sweep_l2_norm,
    float min_window_energy = 1.0e6f,
    bool use_abs_similarity = false
) {
    // Append new samples into circular buffer
    for (size_t i = 0; i < chunk_len; i++) {
        mic_buffer[mic_write_index] = static_cast<float>(chunk[i]);
        mic_write_index = wrap_index(mic_write_index + 1);
    }

    mic_filled = std::min(mic_filled + chunk_len, MAX_BUFFER_SIZE);

    if (mic_filled < SWEEP_LEN) {
        return 0.0f;
    }

    if (sweep_l2_norm <= 0.0f) {
        return 0.0f;
    }

    float max_similarity = 0.0f;
    const size_t num_windows = mic_filled - SWEEP_LEN + 1;

    for (size_t i = 0; i < num_windows; i++) {
        // Gather one candidate window from ring buffer into contiguous scratch
        for (size_t j = 0; j < SWEEP_LEN; j++) {
            size_t idx = wrap_index(mic_write_index + MAX_BUFFER_SIZE - mic_filled + i + j);
            work_buf[j] = mic_buffer[idx];
        }

        float dot = 0.0f;
        float mic_energy = 0.0f;

        dsps_dotprod_f32(work_buf, sweep_f32, &dot, SWEEP_LEN);
        dsps_dotprod_f32(work_buf, work_buf, &mic_energy, SWEEP_LEN);

        // Ignore near-silent / too-weak windows
        if (mic_energy < min_window_energy) {
            continue;
        }

        const float mic_l2_norm = sqrtf(mic_energy);
        if (mic_l2_norm <= 0.0f) {
            continue;
        }

        float similarity = dot / (sweep_l2_norm * mic_l2_norm);

        // Guard against tiny floating-point overshoot
        if (similarity > 1.0f) similarity = 1.0f;
        if (similarity < -1.0f) similarity = -1.0f;

        float score = use_abs_similarity ? fabsf(similarity) : similarity;

        if (score > max_similarity) {
            max_similarity = score;
        }
    }

    return max_similarity;
}


float MicTester::get_setup_priority() const { return setup_priority::AFTER_CONNECTION; }


void MicTester::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MicTester...");
  this->read_sweep_();
}

bool MicTester::allocate_buffers_() {
  if (this->input_buffer_ != nullptr)
    return true;
  ExternalRAMAllocator<int16_t> allocator(ExternalRAMAllocator<int16_t>::ALLOW_FAILURE);
  this->input_buffer_ = allocator.allocate(INPUT_BUFFER_SIZE);
  if (this->input_buffer_ == nullptr) {
    ESP_LOGW(TAG, "Could not allocate input buffer");
    return false;
  }
  this->input_buffer_size_ = INPUT_BUFFER_SIZE;
  return true;
}

void MicTester::clear_buffers_() {
  this->write_pos_ = 0;
  this->read_pos_ = 0;
  this->energy_accumulator_ = 0.0f;
  this->energy_sample_count_ = 0;
}

void MicTester::deallocate_buffers_() {
  if (this->input_buffer_ == nullptr)
    return;
  ExternalRAMAllocator<int16_t> deallocator(ExternalRAMAllocator<int16_t>::ALLOW_FAILURE);
  deallocator.deallocate(this->input_buffer_, INPUT_BUFFER_SIZE);
  this->input_buffer_ = nullptr;
  this->input_buffer_size_ = 0;
}

void MicTester::read_sweep_() {
    const uint8_t *data = this->ref_media_file_->data;
    const size_t len = this->ref_media_file_->length;
    if (len < 2 * SWEEP_LEN) {
        ESP_LOGE(TAG, "Sweep length should be 1024 samples long.  Length: %d", len / 2 );
        this->mark_failed();
        return;
    }
    ESP_LOGD(TAG, "Reading sweep data from file.  Length: %d", len);
    const int16_t *src = reinterpret_cast<const int16_t *>(data);
    for (size_t i = 0; i < SWEEP_LEN; i++) {
        sweep_f32[i] = static_cast<float>(src[i]);
    }

    dsps_wind_hann_f32(window, SWEEP_LEN);
    for (int i = 0; i < SWEEP_LEN; i++) {
        sweep_f32[i] *= window[i];
    }

    this->sweep_norm_ = .0f;
    for (int i = 0; i < SWEEP_LEN; i++) {
        this->sweep_norm_ += sweep_f32[i] * sweep_f32[i];
    }
    this->sweep_norm_ = sqrtf(this->sweep_norm_);
}


void MicTester::on_audio_data_(const std::vector<uint8_t> &data) {
  if (this->state_ != State::DETECTING_SWEEP)
    return;
  if (this->input_buffer_ == nullptr)
    return;

  const int32_t *samples_32 = reinterpret_cast<const int32_t *>(data.data());
  const size_t total_samples = data.size() / sizeof(int32_t);
  if (total_samples < 2)
    return;

  const size_t num_frames = total_samples / 2;
  size_t wp = this->write_pos_;
  const size_t limit = this->input_buffer_size_;

  for (size_t i = 0; i < num_frames; i++) {
    size_t idx = wp % limit;
    int16_t sample = static_cast<int16_t>(samples_32[i * 2 + this->channel_] >> 16);
    this->input_buffer_[idx] = sample;
    this->energy_accumulator_ += static_cast<float>(sample) * static_cast<float>(sample);
    this->energy_sample_count_++;
    wp++;
  }
  this->write_pos_ = wp;
}


void MicTester::loop() {
  switch (this->state_) {
    case State::IDLE: {
        if (this->continuous_ && this->desired_state_ == State::IDLE) {
        this->idle_trigger_->trigger();
        {
          this->set_state_(State::START_MICROPHONE, State::DETECTING_SWEEP);
        }
      } else {
        this->high_freq_.stop();
      }
      break;
    }
    case State::START_MICROPHONE: {
      ESP_LOGD(TAG, "Starting Microphone");
      if (!this->allocate_buffers_()) {
        this->status_set_error(LOG_STR("Failed to allocate buffers"));
        return;
      }
      if (this->status_has_error()) {
        this->status_clear_error();
      }
      this->clear_buffers_();

      mic_write_index = 0;
      mic_filled = 0;

      if (!this->callback_registered_) {
        this->mic_->add_data_callback([this](const std::vector<uint8_t> &data) {
          this->on_audio_data_(data);
        });
        this->callback_registered_ = true;
      }

      this->mic_->start();
      this->high_freq_.start();
      this->set_state_(State::STARTING_MICROPHONE);
      break;
    }
    case State::STARTING_MICROPHONE: {
      if (this->mic_->is_running()) {
        this->set_state_(this->desired_state_);
      }
      break;
    }
    case State::DETECTING_SWEEP: {
      size_t wp = this->write_pos_;
      size_t rp = this->read_pos_;
      if (wp <= rp)
        break;

      size_t available = wp - rp;
      if (available < 480)
        break;

      size_t chunk = std::min(available, (size_t) 960);
      const size_t limit = this->input_buffer_size_;

      int16_t temp[960];
      for (size_t i = 0; i < chunk; i++) {
        temp[i] = this->input_buffer_[(rp + i) % limit];
      }
      this->read_pos_ = rp + chunk;

      float corr = detect_sweep_streaming(temp, chunk, this->sweep_norm_, 1.0e6f, true);
      ESP_LOGD("sweep", "Sweep-Detect corr=%.2f (ch=%d)", corr, this->channel_);

      if (corr > DETECTION_THRESHOLD) {
        ESP_LOGI("sweep", "Sweep detected: corr=%.2f", corr);
        this->sweep_detected_trigger_->trigger();
      }
      break;
    }
    case State::STOP_MICROPHONE: {
      if (this->mic_->is_running()) {
        this->mic_->stop();
        this->set_state_(State::STOPPING_MICROPHONE);
      } else {
        this->set_state_(this->desired_state_);
      }
      break;
    }
    case State::STOPPING_MICROPHONE: {
      if (this->mic_->is_stopped()) {
        this->deallocate_buffers_();
        this->set_state_(this->desired_state_);
      }
      break;
    }
    default:
      break;
  }
}


static const LogString *voice_assistant_state_to_string(State state) {
  switch (state) {
    case State::IDLE:
      return LOG_STR("IDLE");
    case State::START_MICROPHONE:
      return LOG_STR("START_MICROPHONE");
    case State::STARTING_MICROPHONE:
      return LOG_STR("STARTING_MICROPHONE");
    case State::DETECTING_SWEEP:
      return LOG_STR("DETECTING_SWEEP");
    case State::STOP_MICROPHONE:
      return LOG_STR("STOP_MICROPHONE");
    case State::STOPPING_MICROPHONE:
      return LOG_STR("STOPPING_MICROPHONE");
    default:
      return LOG_STR("UNKNOWN");
  }
};

void MicTester::set_state_(State state) {
  State old_state = this->state_;
  this->state_ = state;
  ESP_LOGD(TAG, "State changed from %s to %s", LOG_STR_ARG(voice_assistant_state_to_string(old_state)),
           LOG_STR_ARG(voice_assistant_state_to_string(state)));
}

void MicTester::set_state_(State state, State desired_state) {
  this->set_state_(state);
  this->desired_state_ = desired_state;
  ESP_LOGD(TAG, "Desired state set to %s", LOG_STR_ARG(voice_assistant_state_to_string(desired_state)));
}

void MicTester::failed_to_start() {
  ESP_LOGE(TAG, "Failed to start server. See Home Assistant logs for more details.");
  this->error_trigger_->trigger("failed-to-start", "Failed to start server. See Home Assistant logs for more details.");
  this->set_state_(State::STOP_MICROPHONE, State::IDLE);
}


void MicTester::request_start(bool continuous) {
  if (this->state_ == State::IDLE) {
    this->continuous_ = continuous;
    this->set_state_(State::START_MICROPHONE, State::DETECTING_SWEEP);
  }
}

void MicTester::request_stop() {
  this->continuous_ = false;

  switch (this->state_) {
    case State::IDLE:
      break;
    case State::START_MICROPHONE:
    case State::STARTING_MICROPHONE:
      this->set_state_(State::STOP_MICROPHONE, State::IDLE);
      break;
    case State::DETECTING_SWEEP:
      this->signal_stop_();
      this->set_state_(State::STOP_MICROPHONE, State::IDLE);
      break;
    case State::STOP_MICROPHONE:
    case State::STOPPING_MICROPHONE:
      this->desired_state_ = State::IDLE;
      break;
  }
}

void MicTester::pause_detection() {
  if (this->state_ == State::DETECTING_SWEEP) {
    this->continuous_ = false;
    this->state_ = State::DETECTION_PAUSED;
    ESP_LOGD(TAG, "Detection paused (mic stays running)");
  }
}

void MicTester::reset_detection() {
  this->clear_buffers_();
  mic_write_index = 0;
  mic_filled = 0;
  std::fill_n(mic_buffer, MAX_BUFFER_SIZE, 0.0f);
  std::fill_n(work_buf, SWEEP_LEN, 0.0f);
  if (this->state_ == State::STARTING_MICROPHONE || this->state_ == State::DETECTING_SWEEP || this->state_ == State::DETECTION_PAUSED) {
    this->continuous_ = true;
    this->state_ = State::DETECTING_SWEEP;
    ESP_LOGD(TAG, "Detection reset (ch=%d)", this->channel_);
  }
}

float MicTester::get_mic_energy() {
  if (this->energy_sample_count_ == 0)
    return -1.0f;
  float rms = sqrtf(this->energy_accumulator_ / static_cast<float>(this->energy_sample_count_));
  this->energy_accumulator_ = 0.0f;
  this->energy_sample_count_ = 0;
  return rms;
}

void MicTester::signal_stop_() {

}


}  // namespace online_testing
}  // namespace esphome
