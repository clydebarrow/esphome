#include "club_wifi.h"

#if defined(USE_XCSOAR_WIFI_NETWORKS) && defined(USE_FILESYSTEM_VFS)

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <memory>

namespace esphome::xcsoar {

ESPHOME_LOG_TAG(TAG, "xcsoar.club");

// The requests WifiNetworks can queue at once, less one for XCSoar
static constexpr size_t MAX_CLUB_NETWORKS = 4;
static constexpr size_t MAX_CLUB_JSON = 4096;
static constexpr size_t MAX_PATH = 96;

void load_club_wifi(const char *data_path, wifi_networks::WifiNetworks *networks) {
  char path[MAX_PATH];
  snprintf(path, sizeof(path), "%s/club.json", data_path);
  FILE *file = fopen(path, "rb");
  if (file == nullptr)
    return;
  auto buffer = std::make_unique<uint8_t[]>(MAX_CLUB_JSON);
  const size_t length = fread(buffer.get(), 1, MAX_CLUB_JSON, file);
  fclose(file);

  json::parse_json(buffer.get(), length, [networks](JsonObject root) -> bool {
    size_t count = 0;
    for (JsonObject network : root["wifi"].as<JsonArray>()) {
      const char *ssid = network["ssid"];
      if (ssid == nullptr || *ssid == '\0')
        continue;
      if (count++ == MAX_CLUB_NETWORKS) {
        ESP_LOGW(TAG, "Only the first %zu networks of club.json are saved", MAX_CLUB_NETWORKS);
        break;
      }
      const char *password = network["password"];
      networks->remember(ssid, password != nullptr ? password : "");
    }
    return true;
  });
}

}  // namespace esphome::xcsoar

#endif
