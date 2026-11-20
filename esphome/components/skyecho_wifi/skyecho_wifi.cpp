#include "skyecho_wifi.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::skyecho_wifi {

ESPHOME_LOG_TAG(TAG, "skyecho_wifi");

static constexpr uint32_t CHECK_INTERVAL_MS = 1000;
// A SkyEcho that sends no GDL90 this long after joining is another kind of network, or broken
static constexpr uint32_t FIRST_DATA_MS = 30000;
static constexpr uint32_t NO_DATA_SKIP_MS = 600000;
// GDL90 that stops this long, as when the SkyEcho is switched off, leaves it for a moment
static constexpr uint32_t DATA_GAP_MS = 30000;
static constexpr uint32_t DATA_GAP_SKIP_MS = 60000;
// In flight, a signal below the weak level for this long drops it, and it is not rejoined for a while
static constexpr uint32_t WEAK_MS = 10000;
static constexpr uint32_t WEAK_SKIP_MS = 60000;
// While on another network, how often to look for a SkyEcho
static constexpr uint32_t SCAN_INTERVAL_MS = 60000;
static constexpr int8_t SKYECHO_PRIORITY = 100;
// Networks of a scan that are considered
static constexpr size_t MAX_CANDIDATES = 24;

struct Remembered {
  char ssid[SSID_SIZE];
};

void SkyEchoWifi::setup() {
  auto *wifi = wifi::global_wifi_component;
  const auto &list = wifi->get_sta_list();
  this->configured_.init(list.size());
  for (const auto &ap : list)
    this->configured_.push_back(ap);

  this->pref_ = global_preferences->make_preference<Remembered>(fnv1_hash("skyecho_wifi"), true);
  Remembered remembered{};
  if (this->pref_.load(&remembered) && is_skyecho_ssid(remembered.ssid)) {
    remembered.ssid[SSID_SIZE - 1] = 0;
    this->chooser_.set_remembered(remembered.ssid);
  }
  wifi->add_scan_results_listener(this);
  wifi->add_connect_state_listener(this);
  // The first connection tries the remembered SkyEcho, if it is in range
  this->apply_(this->chooser_.get_remembered()[0] != 0 ? this->chooser_.get_remembered() : nullptr);
}

void SkyEchoWifi::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SkyEcho Wi-Fi:\n"
                "  Remembered: '%s'\n"
                "  Weak signal: %d dBm",
                this->chooser_.get_remembered(), this->chooser_.get_weak_rssi());
}

void SkyEchoWifi::set_skyecho(skyecho::SkyEcho *skyecho) {
  skyecho->add_on_gdl90_data_callback([this](const uint8_t *, size_t) {
    this->last_gdl90_ms_ = millis();
    if (!this->gdl90_seen_ && is_skyecho_ssid(this->current_)) {
      ESP_LOGI(TAG, "GDL90 arrives over '%s'", this->current_);
    }
    this->gdl90_seen_ = true;
    if (!this->flying_ && is_skyecho_ssid(this->current_) &&
        strcmp(this->chooser_.get_remembered(), this->current_) != 0)
      this->remember_pending_ = true;
  });
}

bool SkyEchoWifi::has_skyecho() const {
  return is_skyecho_ssid(this->current_) && this->gdl90_seen_ && millis() - this->last_gdl90_ms_ < DATA_GAP_MS;
}

// The wifi component's list: the SkyEcho to join, if any, then the configured networks
void SkyEchoWifi::apply_(const char *ssid) {
  auto *wifi = wifi::global_wifi_component;
  wifi->clear_sta();
  if (ssid != nullptr) {
    copy_ssid(this->target_, ssid);
    wifi::WiFiAP ap;
    ap.set_ssid(ssid);
    ap.set_priority(SKYECHO_PRIORITY);
    wifi->add_sta(ap);
  } else {
    this->target_[0] = 0;
  }
  for (const auto &ap : this->configured_)
    wifi->add_sta(ap);
}

void SkyEchoWifi::drop_(uint32_t skip_ms, const char *why) {
  ESP_LOGW(TAG, "Leaving '%s': %s", this->current_, why);
  this->chooser_.skip(this->current_, millis() + skip_ms);
  this->apply_(nullptr);
  // Reconnecting from the start scans, and the scan chooses again
  auto *wifi = wifi::global_wifi_component;
  wifi->disable();
  wifi->enable();
}

void SkyEchoWifi::on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) {
  this->last_scan_ms_ = millis();
  // Staying on a SkyEcho that works
  if (is_skyecho_ssid(this->current_))
    return;
  Candidate candidates[MAX_CANDIDATES];
  size_t count = 0;
  for (const auto &result : results) {
    if (count == MAX_CANDIDATES)
      break;
    const StringRef ssid = result.get_ssid();
    if (ssid.empty())
      continue;
    candidates[count++] = Candidate{ssid.c_str(), result.get_rssi(), result.get_with_auth()};
  }
  const char *choice = this->chooser_.choose(candidates, count, millis());
  size_t skyechoes = 0;
  for (size_t i = 0; i != count; i++)
    skyechoes += is_skyecho_ssid(candidates[i].ssid);
  ESP_LOGD(TAG, "Scan: %zu networks, %zu SkyEcho", count, skyechoes);
  if (choice == nullptr || strcmp(choice, this->target_) == 0)
    return;
  ESP_LOGI(TAG, "Joining '%s'%s", choice,
           strcmp(choice, this->chooser_.get_remembered()) == 0 ? LOG_STR_LITERAL(", as remembered")
                                                                : LOG_STR_LITERAL(", the strongest in range"));
  this->apply_(choice);
  auto *wifi = wifi::global_wifi_component;
  // While connected elsewhere, start again to join it; while connecting, the next attempt uses the new list
  if (wifi->is_connected()) {
    wifi->disable();
    wifi->enable();
  }
}

void SkyEchoWifi::on_wifi_connect_state(StringRef ssid, std::span<const uint8_t, 6> bssid) {
  copy_ssid(this->current_, ssid.empty() ? "" : ssid.c_str());
  this->joined_ms_ = millis();
  this->gdl90_seen_ = false;
  this->weak_since_ms_ = 0;
}

void SkyEchoWifi::loop() {
  const uint32_t now = App.get_loop_component_start_time();
  if (now - this->last_check_ms_ < CHECK_INTERVAL_MS)
    return;
  this->last_check_ms_ = now;
  auto *wifi = wifi::global_wifi_component;

  if (this->remember_pending_) {
    this->remember_pending_ = false;
    Remembered remembered{};
    copy_ssid(remembered.ssid, this->current_);
    this->chooser_.set_remembered(remembered.ssid);
    this->pref_.save(&remembered);
    global_preferences->sync();
    ESP_LOGI(TAG, "Remembering '%s' as this glider's SkyEcho", remembered.ssid);
  }

  if (!wifi->is_connected())
    return;
  if (!is_skyecho_ssid(this->current_)) {
    if (now - this->last_scan_ms_ >= SCAN_INTERVAL_MS) {
      this->last_scan_ms_ = now;
      wifi->scan_while_connected();
    }
    return;
  }
  if (!this->gdl90_seen_) {
    if (now - this->joined_ms_ >= FIRST_DATA_MS)
      this->drop_(NO_DATA_SKIP_MS, "no GDL90 from it");
    return;
  }
  if (now - this->last_gdl90_ms_ >= DATA_GAP_MS) {
    this->drop_(DATA_GAP_SKIP_MS, "its GDL90 stopped");
    return;
  }
  if (!this->flying_ || wifi->wifi_rssi() >= this->chooser_.get_weak_rssi()) {
    this->weak_since_ms_ = 0;
    return;
  }
  if (this->weak_since_ms_ == 0)
    this->weak_since_ms_ = now;
  if (now - this->weak_since_ms_ >= WEAK_MS)
    this->drop_(WEAK_SKIP_MS, "weak signal in flight");
}

}  // namespace esphome::skyecho_wifi
