#pragma once

#include "esphome/core/defines.h"

#include <cstdint>

#include "esphome/components/skyecho/skyecho.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "chooser.h"

namespace esphome::skyecho_wifi {

/**
 * Joins a SkyEcho over Wi-Fi with no name configured, so one firmware serves any glider. After each scan it joins the
 * SkyEcho it remembers, or else the strongest open one in range (SkyEchoChooser). The one whose GDL90 traffic arrives
 * while on the ground is remembered, in flash: that is the glider's own, joined before launch.
 *
 * A SkyEcho that sends no GDL90 soon after joining is left alone for a while. In flight, one whose signal stays weak
 * is dropped and the networks are scanned again. The configured networks, as for a bench, come after the SkyEcho.
 */
class SkyEchoWifi : public Component, public wifi::WiFiScanResultsListener, public wifi::WiFiConnectStateListener {
 public:
  explicit SkyEchoWifi(int8_t weak_rssi) : chooser_(weak_rssi) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  // Before wifi, so its first connection already tries the remembered SkyEcho
  float get_setup_priority() const override { return setup_priority::WIFI + 1.0f; }

  void on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) override;
  void on_wifi_connect_state(StringRef ssid, std::span<const uint8_t, 6> bssid) override;

  void set_skyecho(skyecho::SkyEcho *skyecho);
  /// In flight a weak signal drops the SkyEcho; on the ground the one with traffic is remembered
  void set_flying(bool flying) { this->flying_ = flying; }

  /// True while joined to a SkyEcho that is sending GDL90
  bool has_skyecho() const;

 protected:
  void apply_(const char *ssid);
  void drop_(uint32_t skip_ms, const char *why);

  SkyEchoChooser chooser_;
  ESPPreferenceObject pref_;
  FixedVector<wifi::WiFiAP> configured_;
  char target_[SSID_SIZE]{};
  char current_[SSID_SIZE]{};
  uint32_t joined_ms_{0};
  uint32_t last_gdl90_ms_{0};
  uint32_t weak_since_ms_{0};
  uint32_t last_scan_ms_{0};
  uint32_t last_check_ms_{0};
  bool flying_{false};
  bool gdl90_seen_{false};
  bool remember_pending_{false};
};

}  // namespace esphome::skyecho_wifi
