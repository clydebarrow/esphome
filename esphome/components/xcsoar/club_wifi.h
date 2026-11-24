#pragma once

#include "esphome/core/defines.h"

#if defined(USE_XCSOAR_WIFI_NETWORKS) && defined(USE_FILESYSTEM_VFS)

#include "esphome/components/wifi_networks/wifi_networks.h"

namespace esphome::xcsoar {

/// Save the networks in the "wifi" list of data_path/club.json, so a club can hand out its own Wi-Fi with the file:
///
///   "wifi": [ { "ssid": "Clubhouse", "password": "a secret" }, { "ssid": "Hangar" } ]
///
/// A network without a password is open. Only the first MAX_CLUB_NETWORKS are taken.
void load_club_wifi(const char *data_path, wifi_networks::WifiNetworks *networks);

}  // namespace esphome::xcsoar

#endif
