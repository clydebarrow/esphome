#pragma once

#include "esphome/core/defines.h"
#ifdef USE_LVGL

#include <string>

#include "esphome/core/helpers.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome::wifi_chooser {

/// An LVGL widget (a scrollable `lv_list`) that lists the WiFi networks found by the most
/// recent scan and lets the user select one of them (or, in multi-select mode, several).
class WifiChooser : public lvgl::LvCompound, public wifi::WiFiScanResultsListener {
 public:
  void set_obj(lv_obj_t *lv_obj) override;

  void set_multi_select(bool multi_select) { this->multi_select_ = multi_select; }
  void set_show_rssi(bool show_rssi) { this->show_rssi_ = show_rssi; }
  void set_hide_hidden(bool hide_hidden) { this->hide_hidden_ = hide_hidden; }

  /// Start (or restart) a WiFi scan. The list refreshes once the results arrive.
  void rescan();

  /// The SSIDs currently checked by the user (at most one unless multi-select is enabled).
  std::vector<std::string> get_selected_ssids() const;

  /// WiFiScanResultsListener interface: rebuilds the list from the latest scan.
  void on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) override;

  /// Registers a callback fired as (ssid, selected) whenever an entry is checked/unchecked.
  template<typename F> void add_on_select_callback(F &&callback) {
    this->select_callback_.add(std::forward<F>(callback));
  }

 protected:
  static void on_button_clicked_(lv_event_t *e);
  void toggle_(lv_obj_t *button);

  FixedVector<std::string> ssids_;
  LazyCallbackManager<void(std::string, bool)> select_callback_;
  bool multi_select_{false};
  bool show_rssi_{true};
  bool hide_hidden_{true};
};

}  // namespace esphome::wifi_chooser

#endif  // USE_LVGL
