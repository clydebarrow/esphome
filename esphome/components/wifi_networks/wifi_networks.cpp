#include "wifi_networks.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome::wifi_networks {

ESPHOME_LOG_TAG(TAG, "wifi_networks");

static constexpr uint32_t SKYECHO_SCAN_ID = 0;
// While another network is in use, how often to look for a saved SkyEcho
static constexpr uint32_t SKYECHO_SCAN_INTERVAL_MS = 60000;
static constexpr uint32_t SNAPSHOT_INTERVAL_MS = 1000;
// After switching to a SkyEcho, how long before another switch: one that cannot be joined is not tried every scan
static constexpr uint32_t SWITCH_BACKOFF_MS = 300000;
static constexpr int8_t SKYECHO_PRIORITY = 100;
static constexpr int8_t SAVED_PRIORITY = 50;

static void copy_string(char *dest, size_t size, const char *src) {
  strncpy(dest, src, size - 1);
  dest[size - 1] = 0;
}

// SkyEcho access points are named "SkyEcho_" and part of the serial number
static bool is_skyecho_ssid(const char *ssid) { return strncmp(ssid, "SkyEcho", 7) == 0; }

void WifiNetworks::setup() {
  auto *wifi = wifi::global_wifi_component;
  const auto &list = wifi->get_sta_list();
  this->configured_.init(list.size());
  for (const auto &ap : list)
    this->configured_.push_back(ap);

  this->pref_ = global_preferences->make_preference<SavedNetworks>(fnv1_hash("wifi_networks"), true);
  if (!this->pref_.load(&this->saved_) || this->saved_.count > WIFI_NETWORKS_MAX)
    this->saved_ = {};

  wifi->add_scan_results_listener(this);
  wifi->add_connect_state_listener(this);
  this->apply_(false);

  this->set_interval(SKYECHO_SCAN_ID, SKYECHO_SCAN_INTERVAL_MS, [this]() {
    auto *wifi = wifi::global_wifi_component;
    if (this->pinned_[0] == 0 && wifi->is_connected() && !this->current_is_skyecho_())
      wifi->scan_while_connected();
  });
}

void WifiNetworks::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Wi-Fi networks:\n"
                "  Saved: %u of %u",
                this->saved_.count, WIFI_NETWORKS_MAX);
  for (uint8_t i = 0; i != this->saved_.count; i++) {
    const auto &n = this->saved_.networks[i];
    ESP_LOGCONFIG(TAG, "  " LOG_SECRET("'%s'") "%s", n.ssid,
                  (n.flags & FLAG_SKYECHO) ? LOG_STR_LITERAL(" (SkyEcho)") : LOG_STR_LITERAL(""));
  }
}

#ifdef USE_WIFI_NETWORKS_SKYECHO
void WifiNetworks::set_skyecho(skyecho::SkyEcho *skyecho) {
  skyecho->add_on_gdl90_data_callback([this](const uint8_t *, size_t) { this->mark_skyecho_(); });
}
#endif

void WifiNetworks::loop() {
  if (this->has_commands_.load(std::memory_order_acquire)) {
    StaticVector<Command, 4> commands;
    {
      LockGuard guard(this->lock_);
      commands = this->commands_;
      this->commands_.clear();
      this->has_commands_.store(false, std::memory_order_relaxed);
    }
    for (const auto &command : commands)
      this->run_(command);
  }
  if (this->switch_to_skyecho_) {
    this->switch_to_skyecho_ = false;
    ESP_LOGI(TAG, "A SkyEcho is in range, switching to it");
    this->apply_(true);
  }
  const uint32_t now = App.get_loop_component_start_time();
  if (this->snapshot_dirty_ || now - this->last_snapshot_ >= SNAPSHOT_INTERVAL_MS) {
    this->last_snapshot_ = now;
    this->snapshot_dirty_ = false;
    this->update_snapshot_();
  }
}

void WifiNetworks::queue_(CommandType type, const char *ssid, const char *password) {
  Command command{};
  command.type = type;
  if (ssid != nullptr)
    copy_string(command.ssid, sizeof(command.ssid), ssid);
  if (password != nullptr) {
    command.has_password = true;
    copy_string(command.password, sizeof(command.password), password);
  }
  LockGuard guard(this->lock_);
  if (this->commands_.size() == this->commands_.capacity())
    return;
  this->commands_.push_back(command);
  this->has_commands_.store(true, std::memory_order_release);
}

void WifiNetworks::run_(const Command &command) {
  auto *wifi = wifi::global_wifi_component;
  switch (command.type) {
    case CommandType::COMMAND_TYPE_SCAN:
      // While not connected the wifi component scans by itself, and its results arrive anyway
      if (wifi->is_connected())
        wifi->scan_while_connected();
      return;

    case CommandType::COMMAND_TYPE_CONNECT: {
      SavedNetwork *saved = this->find_saved_(command.ssid);
      if (command.has_password) {
        if (saved == nullptr)
          saved = this->add_saved_(command.ssid);
        if (saved == nullptr)
          return;
        copy_string(saved->password, sizeof(saved->password), command.password);
        this->save_();
      } else if (saved == nullptr && this->find_configured_(command.ssid) == nullptr) {
        ESP_LOGW(TAG, "Unknown network " LOG_SECRET("'%s'"), command.ssid);
        return;
      }
      ESP_LOGI(TAG, "Joining " LOG_SECRET("'%s'") " by hand", command.ssid);
      copy_string(this->pinned_, sizeof(this->pinned_), command.ssid);
      this->apply_(true);
      wifi->enable();
      return;
    }

    case CommandType::COMMAND_TYPE_FORGET: {
      SavedNetwork *saved = this->find_saved_(command.ssid);
      if (saved == nullptr)
        return;
      const size_t index = saved - this->saved_.networks;
      memmove(saved, saved + 1, (this->saved_.count - index - 1) * sizeof(SavedNetwork));
      this->saved_.count--;
      this->save_();
      if (strcmp(this->pinned_, command.ssid) == 0)
        this->pinned_[0] = 0;
      ESP_LOGI(TAG, "Forgot " LOG_SECRET("'%s'"), command.ssid);
      this->apply_(true);
      return;
    }

    case CommandType::COMMAND_TYPE_DISCONNECT:
      ESP_LOGI(TAG, "Wi-Fi off by hand");
      this->pinned_[0] = 0;
      this->apply_(false);
      wifi->disable();
      this->snapshot_dirty_ = true;
      return;

    case CommandType::COMMAND_TYPE_AUTOMATIC:
      ESP_LOGI(TAG, "Choosing the network by priority");
      this->pinned_[0] = 0;
      this->apply_(true);
      wifi->enable();
      return;
  }
}

void WifiNetworks::apply_(bool restart) {
  auto *wifi = wifi::global_wifi_component;
  wifi->clear_sta();
  if (this->pinned_[0] != 0) {
    wifi::WiFiAP ap;
    if (const SavedNetwork *saved = this->find_saved_(this->pinned_); saved != nullptr) {
      ap.set_ssid(saved->ssid);
      ap.set_password(saved->password);
    } else if (const wifi::WiFiAP *configured = this->find_configured_(this->pinned_); configured != nullptr) {
      ap = *configured;
    }
    wifi->init_sta(1);
    wifi->add_sta(ap);
  } else {
    // A saved network stands in for a configured one of the same name
    size_t count = this->saved_.count;
    for (const auto &ap : this->configured_) {
      if (this->find_saved_(ap.get_ssid().c_str()) == nullptr)
        count++;
    }
    wifi->init_sta(count);
    // In order of priority, for the first attempt before any scan
    for (const bool skyecho : {true, false}) {
      for (uint8_t i = 0; i != this->saved_.count; i++) {
        const auto &saved = this->saved_.networks[i];
        if (((saved.flags & FLAG_SKYECHO) != 0) != skyecho)
          continue;
        wifi::WiFiAP ap;
        ap.set_ssid(saved.ssid);
        ap.set_password(saved.password);
        ap.set_priority(skyecho ? SKYECHO_PRIORITY : SAVED_PRIORITY);
        wifi->add_sta(ap);
      }
    }
    for (const auto &ap : this->configured_) {
      if (this->find_saved_(ap.get_ssid().c_str()) == nullptr)
        wifi->add_sta(ap);
    }
  }
  // Reconnecting from the start scans, and joins the visible network with the highest priority
  if (restart && !wifi->is_disabled()) {
    wifi->disable();
    wifi->enable();
  }
  this->snapshot_dirty_ = true;
}

void WifiNetworks::save_() {
  this->pref_.save(&this->saved_);
  global_preferences->sync();
  this->snapshot_dirty_ = true;
}

void WifiNetworks::mark_skyecho_() {
  if (this->current_[0] == 0 || this->current_is_skyecho_())
    return;
  SavedNetwork *saved = this->find_saved_(this->current_);
  if (saved == nullptr) {
    // A configured network becomes a saved one, which holds the flag
    saved = this->add_saved_(this->current_);
    if (saved == nullptr)
      return;
    if (const wifi::WiFiAP *configured = this->find_configured_(this->current_); configured != nullptr)
      copy_string(saved->password, sizeof(saved->password), configured->get_password().c_str());
  }
  saved->flags |= FLAG_SKYECHO;
  ESP_LOGI(TAG, "GDL90 data arrives over " LOG_SECRET("'%s'") ", saved as a SkyEcho", saved->ssid);
  this->save_();
  // The list only changes order; the network in use stays
  this->apply_(false);
}

void WifiNetworks::on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) {
  this->visible_count_ = 0;
  for (const auto &result : results) {
    const StringRef ssid = result.get_ssid();
    if (ssid.empty())
      continue;
    Visible *entry = nullptr;
    for (uint8_t i = 0; i != this->visible_count_; i++) {
      if (strcmp(this->visible_[i].ssid, ssid.c_str()) == 0) {
        entry = &this->visible_[i];
        break;
      }
    }
    if (entry == nullptr) {
      if (this->visible_count_ == MAX_VISIBLE)
        continue;
      entry = &this->visible_[this->visible_count_++];
      copy_string(entry->ssid, sizeof(entry->ssid), ssid.c_str());
      entry->rssi = result.get_rssi();
    } else if (result.get_rssi() > entry->rssi) {
      entry->rssi = result.get_rssi();
    }
    entry->secured = result.get_with_auth();
  }
  this->snapshot_dirty_ = true;

  if (this->pinned_[0] != 0 || this->current_is_skyecho_())
    return;
  const uint32_t now = App.get_loop_component_start_time();
  if (this->last_switch_ != 0 && now - this->last_switch_ < SWITCH_BACKOFF_MS)
    return;
  bool added = false;
  bool in_range = false;
  for (uint8_t i = 0; i != this->visible_count_; i++) {
    const Visible &visible = this->visible_[i];
    SavedNetwork *saved = this->find_saved_(visible.ssid);
    // An open network named as a SkyEcho is joined before any of its data has arrived
    if (saved == nullptr && !visible.secured && is_skyecho_ssid(visible.ssid)) {
      saved = this->add_saved_(visible.ssid);
      if (saved != nullptr) {
        saved->flags = FLAG_SKYECHO;
        ESP_LOGI(TAG, "Saved SkyEcho " LOG_SECRET("'%s'") " by its name", visible.ssid);
        added = true;
      }
    }
    if (saved != nullptr && (saved->flags & FLAG_SKYECHO) != 0)
      in_range = true;
  }
  if (added)
    this->save_();
  // While connecting, a SkyEcho saved before is on the list already and is chosen by its priority
  if (added || (in_range && wifi::global_wifi_component->is_connected())) {
    this->switch_to_skyecho_ = true;
    this->last_switch_ = now == 0 ? 1 : now;
  }
}

void WifiNetworks::on_wifi_connect_state(StringRef ssid, std::span<const uint8_t, 6> bssid) {
  // An empty name means disconnected
  if (ssid.empty()) {
    this->current_[0] = 0;
  } else {
    copy_string(this->current_, sizeof(this->current_), ssid.c_str());
  }
  this->snapshot_dirty_ = true;
}

void WifiNetworks::update_snapshot_() {
  auto *wifi = wifi::global_wifi_component;
  Status status{};
  status.automatic = this->pinned_[0] == 0;
  if (wifi->is_disabled()) {
    status.state = WifiState::WIFI_STATE_OFF;
  } else if (wifi->is_connected()) {
    status.state = WifiState::WIFI_STATE_CONNECTED;
    copy_string(status.ssid, sizeof(status.ssid), this->current_);
    for (const auto &ip : wifi->wifi_sta_ip_addresses()) {
      if (ip.is_ip4()) {
        ip.str_to(status.ip);
        break;
      }
    }
  } else {
    status.state = WifiState::WIFI_STATE_CONNECTING;
  }
  const bool connected = status.state == WifiState::WIFI_STATE_CONNECTED;

  LockGuard guard(this->lock_);
  this->status_ = status;
  size_t n = 0;
  auto add = [&](const char *ssid) -> NetworkInfo & {
    NetworkInfo &info = this->listed_[n++];
    info = {};
    copy_string(info.ssid, sizeof(info.ssid), ssid);
    info.connected = connected && strcmp(ssid, this->current_) == 0;
    for (uint8_t i = 0; i != this->visible_count_; i++) {
      if (strcmp(this->visible_[i].ssid, ssid) == 0) {
        info.visible = true;
        info.rssi = this->visible_[i].rssi;
        info.secured = this->visible_[i].secured;
        break;
      }
    }
    return info;
  };
  for (uint8_t i = 0; i != this->saved_.count; i++) {
    const auto &saved = this->saved_.networks[i];
    NetworkInfo &info = add(saved.ssid);
    info.known = true;
    info.saved = true;
    info.secured = saved.password[0] != 0;
    info.skyecho = (saved.flags & FLAG_SKYECHO) != 0;
  }
  for (const auto &ap : this->configured_) {
    if (this->find_saved_(ap.get_ssid().c_str()) != nullptr)
      continue;
    NetworkInfo &info = add(ap.get_ssid().c_str());
    info.known = true;
    info.secured = !ap.get_password().empty();
    info.skyecho = is_skyecho_ssid(info.ssid);
  }
  for (uint8_t i = 0; i != this->visible_count_; i++) {
    const char *ssid = this->visible_[i].ssid;
    if (this->find_saved_(ssid) == nullptr && this->find_configured_(ssid) == nullptr)
      add(ssid).skyecho = is_skyecho_ssid(ssid);
  }
  this->listed_count_ = n;
}

size_t WifiNetworks::get_networks(NetworkInfo *dest, size_t max) {
  LockGuard guard(this->lock_);
  const size_t n = std::min(max, this->listed_count_);
  memcpy(dest, this->listed_.data(), n * sizeof(NetworkInfo));
  return n;
}

Status WifiNetworks::get_status() {
  LockGuard guard(this->lock_);
  return this->status_;
}

WifiNetworks::SavedNetwork *WifiNetworks::find_saved_(const char *ssid) {
  for (uint8_t i = 0; i != this->saved_.count; i++) {
    if (strcmp(this->saved_.networks[i].ssid, ssid) == 0)
      return &this->saved_.networks[i];
  }
  return nullptr;
}

const wifi::WiFiAP *WifiNetworks::find_configured_(const char *ssid) const {
  for (const auto &ap : this->configured_) {
    if (strcmp(ap.get_ssid().c_str(), ssid) == 0)
      return &ap;
  }
  return nullptr;
}

WifiNetworks::SavedNetwork *WifiNetworks::add_saved_(const char *ssid) {
  if (this->saved_.count == WIFI_NETWORKS_MAX) {
    ESP_LOGW(TAG, "No room to save " LOG_SECRET("'%s'"), ssid);
    return nullptr;
  }
  SavedNetwork *saved = &this->saved_.networks[this->saved_.count++];
  *saved = {};
  copy_string(saved->ssid, sizeof(saved->ssid), ssid);
  if (is_skyecho_ssid(ssid))
    saved->flags = FLAG_SKYECHO;
  return saved;
}

bool WifiNetworks::current_is_skyecho_() {
  const SavedNetwork *saved = this->find_saved_(this->current_);
  return saved != nullptr && (saved->flags & FLAG_SKYECHO) != 0;
}

}  // namespace esphome::wifi_networks
