#ifdef USE_HOST

#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include "preferences.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace host {
namespace fs = std::filesystem;

static const char *const TAG = "host.preferences";

HostPreferences::HostPreferences() {
  this->filename.append(getenv("HOME"));
  this->filename.append("/.esphome");
  this->filename.append("/prefs");
  fs::create_directories(this->filename);
  this->filename.append("/");
  this->filename.append(App.get_name());
  this->filename.append(".json");
  FILE *fp = fopen(this->filename.c_str(), "rb");
  if (fp != nullptr) {
    while (!feof((fp))) {
      uint32_t key;
      uint8_t len;
      if (fread(&key, sizeof(key), 1, fp) != sizeof(key))
        break;
      if (fread(&len, sizeof(len), 1, fp) != sizeof(len))
        break;
      uint8_t data[len];
      if (fread(data, sizeof(uint8_t), len, fp) != len)
        break;
      std::vector vec(data, data + len);
      this->data[key] = vec;
    }
    fclose(fp);
  }
}

bool HostPreferences::sync() {
  FILE *fp = fopen(this->filename.c_str(), "wb");
  std::map<uint32_t, std::vector<uint8_t>>::iterator it;

  for (it = this->data.begin(); it != this->data.end(); ++it) {
    fwrite(&it->first, sizeof(uint32_t), 1, fp);
    uint8_t len = it->second.size();
    fwrite(&len, sizeof(len), 1, fp);
    fwrite(it->second.data(), sizeof(uint8_t), it->second.size(), fp);
  }
  fclose(fp);
  return true;
}

bool HostPreferences::reset() {
  return false;
}

ESPPreferenceObject HostPreferences::make_preference(size_t length, uint32_t type, bool in_flash) {
  auto backend = new HostPreferenceBackend(type);
  return ESPPreferenceObject(backend);
};

void setup_preferences() {
  auto *pref = new HostPreferences(); // NOLINT(cppcoreguidelines-owning-memory)
  host_preferences = pref;
  global_preferences = pref;
}

bool HostPreferenceBackend::save(const uint8_t *data, size_t len) {
  return host_preferences->save(this->key_, data, len);
}

bool HostPreferenceBackend::load(uint8_t *data, size_t len) {
  return host_preferences->load(this->key_, data, len);
}

HostPreferences *host_preferences;
} // namespace host

ESPPreferences *global_preferences; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}                                   // namespace esphome

#endif  // USE_HOST