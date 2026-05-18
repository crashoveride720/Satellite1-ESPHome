#pragma once

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/micro_wake_word/micro_wake_word.h"
#include "esphome/components/micro_wake_word/streaming_model.h"
#include "esphome/components/text/text.h"

#include <string>

namespace esphome {
namespace custom_wake_word_loader {

static const char *const TAG = "custom_wake_word_loader";

static const size_t MAX_MANIFEST_SIZE = 4096;
static const size_t MAX_TFLITE_SIZE = 512 * 1024;  // 512 KB max model size
static const size_t DOWNLOAD_BLOCK_SIZE = 4096;

// NVS keys for persistent cache
static const char *const NVS_KEY_URL = "cww_url";
static const char *const NVS_KEY_WAKE_WORD = "cww_wakeword";
static const char *const NVS_KEY_MODEL_SIZE = "cww_mdl_size";
static const char *const NVS_KEY_ARENA_SIZE = "cww_arena";
static const char *const NVS_KEY_PROB_CUTOFF = "cww_prob";
static const char *const NVS_KEY_WINDOW_SIZE = "cww_window";

struct ManifestData {
  std::string wake_word;
  std::string model_url;
  float probability_cutoff{0.97f};
  size_t sliding_window_size{5};
  size_t tensor_arena_size{30000};
  std::vector<std::string> trained_languages;
};

enum LoaderState : uint8_t {
  IDLE,
  DOWNLOADING_MANIFEST,
  DOWNLOADING_MODEL,
  REGISTERING_MODEL,
  COMPLETE,
  ERROR_STATE,
};

class ManifestUrlText : public text::Text {
 public:
  void control(const std::string &value) override;
  void set_loader(class CustomWakeWordLoader *loader) { this->loader_ = loader; }

 protected:
  class CustomWakeWordLoader *loader_{nullptr};
};

class CustomWakeWordLoader : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_micro_wake_word(micro_wake_word::MicroWakeWord *mww) { this->mww_ = mww; }
  void set_http_request(http_request::HttpRequestComponent *http) { this->http_request_ = http; }
  void set_text_entity(ManifestUrlText *text) { this->url_text_ = text; }

  void on_url_changed(const std::string &url);

  ManifestUrlText *get_text_entity() { return this->url_text_; }

 protected:
  bool download_manifest_(const std::string &url, ManifestData &manifest);
  bool download_model_(const std::string &url, uint8_t *buffer, size_t &bytes_downloaded);
  bool parse_manifest_json_(const std::string &json_str, ManifestData &manifest);
  bool register_model_(const ManifestData &manifest, uint8_t *model_data, size_t model_size);

  void save_to_nvs_(const std::string &url, const ManifestData &manifest, const uint8_t *model_data,
                     size_t model_size);
  bool load_from_nvs_();
  std::string load_url_from_nvs_();
  void clear_nvs_();

  void unregister_current_model_();

  micro_wake_word::MicroWakeWord *mww_{nullptr};
  http_request::HttpRequestComponent *http_request_{nullptr};

  ManifestUrlText *url_text_{nullptr};

  LoaderState state_{IDLE};
  std::string pending_url_;

  // Currently loaded custom model
  micro_wake_word::WakeWordModel *current_model_{nullptr};
  uint8_t *model_buffer_{nullptr};
  size_t model_buffer_size_{0};
  std::string current_url_;
};

}  // namespace custom_wake_word_loader
}  // namespace esphome

#endif  // USE_ESP32
