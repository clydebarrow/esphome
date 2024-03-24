#pragma once

#ifdef USE_HOST

#include "esphome/core/preferences.h"
#include "json.hpp"

namespace esphome {
namespace host {

using json = nlohmann::json;

class HostPreferenceBackend : public ESPPreferenceBackend {
 public:
  explicit HostPreferenceBackend(uint32_t key) { this->key_ = key; }

  bool save(const uint8_t *data, size_t len) override;
  bool load(uint8_t *data, size_t len) override;

 protected:
  uint32_t key_{};
};

class HostPreferences : public ESPPreferences {
 public:
  HostPreferences();
  bool sync() override;
  bool reset() override;

  ESPPreferenceObject make_preference(size_t length, uint32_t type, bool in_flash) override;
  ESPPreferenceObject make_preference(size_t length, uint32_t type) override {
    return make_preference(length, type, false);
  }

  void save(uint32_t key, std::vector<uint8_t>& vec) {
    this->data[key] = vec;
  }

 protected:
  std::string filename{};
  json data{};
};
void setup_preferences();
extern HostPreferences *host_preferences;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace host
}  // namespace esphome

#endif  // USE_HOST
