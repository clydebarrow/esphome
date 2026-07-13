#include "wifi_chooser.h"
#ifdef USE_LVGL

#include <algorithm>
#include <cstring>

namespace esphome::wifi_chooser {

// SSIDs are at most 32 bytes; " (-128)" (the longest RSSI suffix) is 7 more, plus a terminator.
static constexpr size_t MAX_ENTRY_LEN = 32 + 7 + 1;

void WifiChooser::set_obj(lv_obj_t *lv_obj) {
  LvCompound::set_obj(lv_obj);
  wifi::global_wifi_component->add_scan_results_listener(this);
}

void WifiChooser::rescan() { wifi::global_wifi_component->start_scanning(); }

void WifiChooser::on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) {
  lv_obj_clean(this->obj);
  this->ssids_.init(results.size());
  for (const auto &scan : results) {
    if (this->hide_hidden_ && scan.get_is_hidden())
      continue;
    StringRef ssid = scan.get_ssid();
    this->ssids_.push_back(std::string(ssid.c_str(), ssid.size()));

    char text[MAX_ENTRY_LEN];
    size_t len = std::min(ssid.size(), sizeof(text) - 9);
    memcpy(text, ssid.c_str(), len);
    char *ptr = text + len;
    if (this->show_rssi_) {
      *ptr++ = ' ';
      *ptr++ = '(';
      ptr = int8_to_str(ptr, scan.get_rssi());
      *ptr++ = ')';
    }
    *ptr = '\0';

    lv_obj_t *button = lv_list_add_button(this->obj, nullptr, text);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_user_data(button, reinterpret_cast<void *>(this->ssids_.size() - 1));
    lv_obj_add_event_cb(button, WifiChooser::on_button_clicked_, LV_EVENT_CLICKED, this);
  }
}

void WifiChooser::on_button_clicked_(lv_event_t *e) {
  auto *self = static_cast<WifiChooser *>(lv_event_get_user_data(e));
  auto *button = static_cast<lv_obj_t *>(lv_event_get_target(e));
  self->toggle_(button);
}

void WifiChooser::toggle_(lv_obj_t *button) {
  if (!this->multi_select_ && lv_obj_has_state(button, LV_STATE_CHECKED)) {
    // Radio-button behaviour: uncheck every other entry.
    uint32_t count = lv_obj_get_child_count(this->obj);
    for (uint32_t i = 0; i < count; i++) {
      lv_obj_t *child = lv_obj_get_child(this->obj, i);
      if (child != button)
        lv_obj_remove_state(child, LV_STATE_CHECKED);
    }
  }
  auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(button));
  bool selected = lv_obj_has_state(button, LV_STATE_CHECKED);
  this->select_callback_.call(this->ssids_[index], selected);
}

std::vector<std::string> WifiChooser::get_selected_ssids() const {
  std::vector<std::string> selected;
  uint32_t count = lv_obj_get_child_count(this->obj);
  for (uint32_t i = 0; i < count; i++) {
    lv_obj_t *child = lv_obj_get_child(this->obj, i);
    if (lv_obj_has_state(child, LV_STATE_CHECKED)) {
      auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(child));
      selected.push_back(this->ssids_[index]);
    }
  }
  return selected;
}

}  // namespace esphome::wifi_chooser

#endif  // USE_LVGL
