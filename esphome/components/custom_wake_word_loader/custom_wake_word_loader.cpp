#include "custom_wake_word_loader.h"

#ifdef USE_ESP32

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <nvs_flash.h>
#include <nvs.h>
#include <cstring>
#include <cJSON.h>

namespace esphome {
namespace custom_wake_word_loader {

static const char *const NVS_NAMESPACE = "cww_loader";
static const char *const NVS_KEY_MODEL_BLOB = "cww_model";

// ── ManifestUrlText ──────────────────────────────────────────

void ManifestUrlText::control(const std::string &value) {
  this->publish_state(value);
  if (this->loader_ != nullptr) {
    this->loader_->on_url_changed(value);
  }
}

// ── Component lifecycle ──────────────────────────────────────

float CustomWakeWordLoader::get_setup_priority() const {
  // Run after micro_wake_word setup but before voice_assistant connects to HA
  return setup_priority::PROCESSOR - 1.0f;
}

void CustomWakeWordLoader::setup() {
  this->url_text_->set_loader(this);

  // Try loading a cached model from NVS (metadata + model blob)
  if (this->load_from_nvs_()) {
    ESP_LOGI(TAG, "Loaded cached custom wake word from NVS");
    this->state_ = COMPLETE;
  } else {
    // Check if we have a saved URL but no cached blob -- schedule a re-download
    std::string saved_url = this->load_url_from_nvs_();
    if (!saved_url.empty()) {
      ESP_LOGI(TAG, "Found saved URL but no cached model, scheduling re-download");
      this->url_text_->publish_state(saved_url);
      this->pending_url_ = saved_url;
      this->state_ = DOWNLOADING_MANIFEST;
    } else {
      ESP_LOGD(TAG, "No cached custom wake word found");
      this->state_ = IDLE;
    }
  }
}

void CustomWakeWordLoader::dump_config() {
  ESP_LOGCONFIG(TAG, "Custom Wake Word Loader:");
  ESP_LOGCONFIG(TAG, "  State: %s", this->state_ == COMPLETE ? "loaded" : "idle");
  if (!this->current_url_.empty()) {
    ESP_LOGCONFIG(TAG, "  URL: %s", this->current_url_.c_str());
  }
  if (this->current_model_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Wake word: %s", this->current_model_->get_wake_word().c_str());
    ESP_LOGCONFIG(TAG, "  Model size: %zu bytes", this->model_buffer_size_);
  }
}

void CustomWakeWordLoader::loop() {
  if (this->state_ != DOWNLOADING_MANIFEST || this->pending_url_.empty()) {
    return;
  }

  std::string url = this->pending_url_;
  this->pending_url_.clear();

  ESP_LOGI(TAG, "Starting custom wake word download from: %s", url.c_str());

  // Step 1: Download and parse the manifest
  ManifestData manifest;
  if (!this->download_manifest_(url, manifest)) {
    ESP_LOGE(TAG, "Failed to download or parse manifest");
    this->state_ = ERROR_STATE;
    return;
  }

  ESP_LOGI(TAG, "Manifest parsed: wake_word='%s', model_url='%s', arena=%zu",
           manifest.wake_word.c_str(), manifest.model_url.c_str(), manifest.tensor_arena_size);

  // Step 2: Allocate PSRAM buffer and download the tflite model
  this->state_ = DOWNLOADING_MODEL;

  // Allocate in PSRAM (external RAM)
  uint8_t *model_buffer = (uint8_t *) heap_caps_malloc(MAX_TFLITE_SIZE, MALLOC_CAP_SPIRAM);
  if (model_buffer == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM buffer for model (%zu bytes)", MAX_TFLITE_SIZE);
    this->state_ = ERROR_STATE;
    return;
  }

  size_t model_size = 0;
  if (!this->download_model_(manifest.model_url, model_buffer, model_size)) {
    ESP_LOGE(TAG, "Failed to download tflite model");
    heap_caps_free(model_buffer);
    this->state_ = ERROR_STATE;
    return;
  }

  ESP_LOGI(TAG, "Model downloaded: %zu bytes", model_size);

  // Shrink the PSRAM allocation to actual size
  uint8_t *shrunk = (uint8_t *) heap_caps_realloc(model_buffer, model_size, MALLOC_CAP_SPIRAM);
  if (shrunk != nullptr) {
    model_buffer = shrunk;
  }

  // Step 3: Unregister any previously loaded custom model
  this->unregister_current_model_();

  // Step 4: Register the new model with micro_wake_word
  this->state_ = REGISTERING_MODEL;
  if (!this->register_model_(manifest, model_buffer, model_size)) {
    ESP_LOGE(TAG, "Failed to register model with micro_wake_word");
    heap_caps_free(model_buffer);
    this->state_ = ERROR_STATE;
    return;
  }

  this->model_buffer_ = model_buffer;
  this->model_buffer_size_ = model_size;
  this->current_url_ = url;

  // Step 5: Cache to NVS for persistence across reboots
  this->save_to_nvs_(url, manifest, model_buffer, model_size);

  this->state_ = COMPLETE;
  ESP_LOGI(TAG, "Custom wake word '%s' loaded and registered successfully", manifest.wake_word.c_str());

  // Restart MWW so HA re-discovers the new wake word list
  if (this->mww_ != nullptr) {
    this->mww_->stop();
    this->mww_->start();
  }
}

// ── URL change handler ───────────────────────────────────────

void CustomWakeWordLoader::on_url_changed(const std::string &url) {
  if (url.empty()) {
    ESP_LOGI(TAG, "URL cleared, removing custom wake word");
    this->unregister_current_model_();
    this->clear_nvs_();
    this->current_url_.clear();
    this->state_ = IDLE;
    if (this->mww_ != nullptr) {
      this->mww_->stop();
      this->mww_->start();
    }
    return;
  }

  if (url == this->current_url_ && this->current_model_ != nullptr) {
    ESP_LOGD(TAG, "URL unchanged, skipping re-download");
    return;
  }

  this->pending_url_ = url;
  this->state_ = DOWNLOADING_MANIFEST;
}

// ── Manifest download & parsing ──────────────────────────────

bool CustomWakeWordLoader::download_manifest_(const std::string &url, ManifestData &manifest) {
  if (this->http_request_ == nullptr) {
    return false;
  }

  ESP_LOGD(TAG, "Downloading manifest from: %s", url.c_str());
  auto container = this->http_request_->get(url);
  if (container == nullptr) {
    ESP_LOGE(TAG, "HTTP GET failed for manifest");
    return false;
  }

  size_t content_length = container->content_length;
  bool chunked = (content_length == 0);
  size_t max_read = chunked ? MAX_MANIFEST_SIZE : content_length;

  if (!chunked && content_length > MAX_MANIFEST_SIZE) {
    ESP_LOGE(TAG, "Manifest size too large: %zu (max %zu)", content_length, MAX_MANIFEST_SIZE);
    container->end();
    return false;
  }

  if (chunked) {
    ESP_LOGD(TAG, "No Content-Length header, streaming up to %zu bytes", MAX_MANIFEST_SIZE);
  }

  std::string json_str;
  json_str.resize(max_read);
  size_t total_read = 0;
  while (total_read < max_read) {
    int bytes_read = container->read((uint8_t *) json_str.data() + total_read, max_read - total_read);
    if (bytes_read <= 0) {
      break;
    }
    total_read += bytes_read;
    App.feed_wdt();
    yield();
  }
  container->end();

  if (total_read == 0) {
    ESP_LOGE(TAG, "Manifest download returned no data");
    return false;
  }

  if (!chunked && total_read < content_length) {
    ESP_LOGE(TAG, "Incomplete manifest download: %zu / %zu", total_read, content_length);
    return false;
  }

  json_str.resize(total_read);

  return this->parse_manifest_json_(json_str, manifest);
}

bool CustomWakeWordLoader::parse_manifest_json_(const std::string &json_str, ManifestData &manifest) {
  cJSON *root = cJSON_Parse(json_str.c_str());
  if (root == nullptr) {
    ESP_LOGE(TAG, "Failed to parse manifest JSON");
    return false;
  }

  cJSON *wake_word = cJSON_GetObjectItem(root, "wake_word");
  cJSON *model_url = cJSON_GetObjectItem(root, "model");
  cJSON *micro = cJSON_GetObjectItem(root, "micro");

  if (!cJSON_IsString(wake_word) || !cJSON_IsString(model_url)) {
    ESP_LOGE(TAG, "Manifest missing required fields 'wake_word' or 'model'");
    cJSON_Delete(root);
    return false;
  }

  manifest.wake_word = wake_word->valuestring;
  manifest.model_url = model_url->valuestring;

  if (cJSON_IsObject(micro)) {
    cJSON *prob = cJSON_GetObjectItem(micro, "probability_cutoff");
    cJSON *window = cJSON_GetObjectItem(micro, "sliding_window_size");
    cJSON *arena = cJSON_GetObjectItem(micro, "tensor_arena_size");

    if (cJSON_IsNumber(prob)) manifest.probability_cutoff = (float) prob->valuedouble;
    if (cJSON_IsNumber(window)) manifest.sliding_window_size = (size_t) window->valueint;
    if (cJSON_IsNumber(arena)) manifest.tensor_arena_size = (size_t) arena->valueint;
  }

  cJSON *languages = cJSON_GetObjectItem(root, "trained_languages");
  if (cJSON_IsArray(languages)) {
    int count = cJSON_GetArraySize(languages);
    for (int i = 0; i < count; i++) {
      cJSON *lang = cJSON_GetArrayItem(languages, i);
      if (cJSON_IsString(lang)) {
        manifest.trained_languages.push_back(lang->valuestring);
      }
    }
  }

  cJSON_Delete(root);
  return true;
}

// ── Model download ───────────────────────────────────────────

bool CustomWakeWordLoader::download_model_(const std::string &url, uint8_t *buffer, size_t &bytes_downloaded) {
  if (this->http_request_ == nullptr) {
    return false;
  }

  ESP_LOGD(TAG, "Downloading tflite model from: %s", url.c_str());
  auto container = this->http_request_->get(url);
  if (container == nullptr) {
    ESP_LOGE(TAG, "HTTP GET failed for model");
    return false;
  }

  size_t content_length = container->content_length;
  bool chunked = (content_length == 0);
  size_t max_read = chunked ? MAX_TFLITE_SIZE : content_length;

  if (!chunked && content_length > MAX_TFLITE_SIZE) {
    ESP_LOGE(TAG, "Model size too large: %zu (max %zu)", content_length, MAX_TFLITE_SIZE);
    container->end();
    return false;
  }

  if (chunked) {
    ESP_LOGD(TAG, "No Content-Length header, streaming up to %zu bytes", MAX_TFLITE_SIZE);
  }

  bytes_downloaded = 0;
  while (bytes_downloaded < max_read) {
    size_t to_read = std::min(DOWNLOAD_BLOCK_SIZE, max_read - bytes_downloaded);
    int bytes_read = container->read(buffer + bytes_downloaded, to_read);
    if (bytes_read <= 0) {
      break;
    }
    bytes_downloaded += bytes_read;
    App.feed_wdt();
    yield();
  }
  container->end();

  if (bytes_downloaded == 0) {
    ESP_LOGE(TAG, "Model download returned no data");
    return false;
  }

  if (!chunked && bytes_downloaded < content_length) {
    ESP_LOGE(TAG, "Incomplete model download: %zu / %zu", bytes_downloaded, content_length);
    return false;
  }

  return true;
}

// ── Model registration ───────────────────────────────────────

bool CustomWakeWordLoader::register_model_(const ManifestData &manifest, uint8_t *model_data, size_t model_size) {
  if (this->mww_ == nullptr) {
    return false;
  }

  // Convert float probability to quantized uint8 (0.0-1.0 -> 0-255)
  uint8_t prob_cutoff = (uint8_t) (manifest.probability_cutoff * 255.0f);

  // Generate a stable ID from the wake word
  std::string model_id = "custom_" + str_sanitize(str_snake_case(manifest.wake_word));

  auto *model = new micro_wake_word::WakeWordModel(
      model_id,
      model_data,
      prob_cutoff,
      manifest.sliding_window_size,
      manifest.wake_word,
      manifest.tensor_arena_size,
      true,   // default_enabled
      false   // internal_only = false, so HA can see it
  );

  this->mww_->add_wake_word_model(model);
  this->current_model_ = model;

  return true;
}

void CustomWakeWordLoader::unregister_current_model_() {
  if (this->current_model_ != nullptr) {
    // Disable the model so MWW unloads it on next inference cycle
    this->current_model_->disable();
    // We cannot remove from the vector, but disabling + internal_only hides it
    this->current_model_ = nullptr;
  }

  if (this->model_buffer_ != nullptr) {
    heap_caps_free(this->model_buffer_);
    this->model_buffer_ = nullptr;
    this->model_buffer_size_ = 0;
  }
}

// ── NVS persistence ──────────────────────────────────────────

void CustomWakeWordLoader::save_to_nvs_(const std::string &url, const ManifestData &manifest,
                                         const uint8_t *model_data, size_t model_size) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
    return;
  }

  nvs_set_str(handle, NVS_KEY_URL, url.c_str());
  nvs_set_str(handle, NVS_KEY_WAKE_WORD, manifest.wake_word.c_str());
  nvs_set_u32(handle, NVS_KEY_MODEL_SIZE, (uint32_t) model_size);
  nvs_set_u32(handle, NVS_KEY_ARENA_SIZE, (uint32_t) manifest.tensor_arena_size);

  uint8_t prob_u8 = (uint8_t) (manifest.probability_cutoff * 255.0f);
  nvs_set_u8(handle, NVS_KEY_PROB_CUTOFF, prob_u8);
  nvs_set_u16(handle, NVS_KEY_WINDOW_SIZE, (uint16_t) manifest.sliding_window_size);

  // Store the model binary as an NVS blob
  err = nvs_set_blob(handle, NVS_KEY_MODEL_BLOB, model_data, model_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write model blob to NVS (%zu bytes): %s", model_size, esp_err_to_name(err));
    // Model may be too large for NVS; the component will re-download on next boot
  }

  nvs_commit(handle);
  nvs_close(handle);

  ESP_LOGI(TAG, "Saved custom wake word to NVS (%zu bytes)", model_size);
}

bool CustomWakeWordLoader::load_from_nvs_() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return false;
  }

  // Read URL
  size_t url_len = 0;
  err = nvs_get_str(handle, NVS_KEY_URL, nullptr, &url_len);
  if (err != ESP_OK || url_len == 0) {
    nvs_close(handle);
    return false;
  }
  std::string cached_url(url_len - 1, '\0');
  nvs_get_str(handle, NVS_KEY_URL, cached_url.data(), &url_len);

  // Read wake word
  size_t ww_len = 0;
  err = nvs_get_str(handle, NVS_KEY_WAKE_WORD, nullptr, &ww_len);
  if (err != ESP_OK || ww_len == 0) {
    nvs_close(handle);
    return false;
  }
  std::string wake_word(ww_len - 1, '\0');
  nvs_get_str(handle, NVS_KEY_WAKE_WORD, wake_word.data(), &ww_len);

  // Read model size
  uint32_t model_size = 0;
  err = nvs_get_u32(handle, NVS_KEY_MODEL_SIZE, &model_size);
  if (err != ESP_OK || model_size == 0) {
    nvs_close(handle);
    return false;
  }

  // Read arena size
  uint32_t arena_size = 30000;
  nvs_get_u32(handle, NVS_KEY_ARENA_SIZE, &arena_size);

  // Read probability cutoff
  uint8_t prob_cutoff = 247;  // ~0.97
  nvs_get_u8(handle, NVS_KEY_PROB_CUTOFF, &prob_cutoff);

  // Read sliding window size
  uint16_t window_size = 5;
  nvs_get_u16(handle, NVS_KEY_WINDOW_SIZE, &window_size);

  // Read model blob
  size_t blob_size = model_size;
  uint8_t *model_buffer = (uint8_t *) heap_caps_malloc(model_size, MALLOC_CAP_SPIRAM);
  if (model_buffer == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate PSRAM for cached model (%u bytes)", model_size);
    nvs_close(handle);
    return false;
  }

  err = nvs_get_blob(handle, NVS_KEY_MODEL_BLOB, model_buffer, &blob_size);
  nvs_close(handle);

  if (err != ESP_OK || blob_size != model_size) {
    ESP_LOGW(TAG, "NVS model blob read failed or size mismatch, will re-download");
    heap_caps_free(model_buffer);
    return false;
  }

  // Build ManifestData from cached values
  ManifestData manifest;
  manifest.wake_word = wake_word;
  manifest.probability_cutoff = prob_cutoff / 255.0f;
  manifest.sliding_window_size = window_size;
  manifest.tensor_arena_size = arena_size;

  if (!this->register_model_(manifest, model_buffer, model_size)) {
    heap_caps_free(model_buffer);
    return false;
  }

  this->model_buffer_ = model_buffer;
  this->model_buffer_size_ = model_size;
  this->current_url_ = cached_url;
  this->url_text_->publish_state(cached_url);

  return true;
}

std::string CustomWakeWordLoader::load_url_from_nvs_() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return "";
  }

  size_t url_len = 0;
  err = nvs_get_str(handle, NVS_KEY_URL, nullptr, &url_len);
  if (err != ESP_OK || url_len == 0) {
    nvs_close(handle);
    return "";
  }
  std::string url(url_len - 1, '\0');
  nvs_get_str(handle, NVS_KEY_URL, url.data(), &url_len);
  nvs_close(handle);
  return url;
}

void CustomWakeWordLoader::clear_nvs_() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return;
  }
  nvs_erase_all(handle);
  nvs_commit(handle);
  nvs_close(handle);
  ESP_LOGD(TAG, "Cleared custom wake word from NVS");
}

}  // namespace custom_wake_word_loader
}  // namespace esphome

#endif  // USE_ESP32
