#ifdef USE_HOST

#include <filesystem>
#include <fstream>
#include "preferences.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "json.hpp"
#include "core/application.h"

namespace esphome {
namespace host {

using json = nlohmann::json;
namespace fs = std::filesystem;

static const char *const TAG = "host.preferences";

HostPreferences::HostPreferences() {
  this->filename.append(getenv("HOME"));
  this->filename.append("/.esphome/prefs");
  if (!fs::create_directories(this->filename)) {
    ESP_LOGE(TAG, "Failed to create directory %s", this->filename.c_str());
    return;
  }
  this->filename.append("/");
  this->filename.append(App.get_name());
  this->filename.append(".json");
  if (fs::exists(this->filename)) {
    std::ifstream infile(this->filename);
    this->data = json::parse(infile);
    infile.close();
  }



}
bool HostPreferences::sync() { return false; }
bool HostPreferences::reset() { return false; }
ESPPreferenceObject HostPreferences::make_preference(size_t length, uint32_t type, bool in_flash) {
  auto backend = new HostPreferenceBackend(type);
  return ESPPreferenceObject(backend);
};

void setup_preferences() {
  auto *pref = new HostPreferences();  // NOLINT(cppcoreguidelines-owning-memory)
  host_preferences = pref;
  global_preferences = pref;
}

bool HostPreferenceBackend::save(const uint8_t *data, size_t len) {
  std::vector vec(data, data + len);
  json j_vec(vec);

  return false; }
bool HostPreferenceBackend::load(uint8_t *data, size_t len) { return false; }

HostPreferences *host_preferences;

}  // namespace host

ESPPreferences *global_preferences;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esphome

#endif  // USE_HOST
