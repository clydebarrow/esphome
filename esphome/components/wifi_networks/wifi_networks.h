#pragma once

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#ifdef USE_WIFI_NETWORKS_SKYECHO
#include "esphome/components/skyecho/skyecho.h"
#endif

#include <array>
#include <atomic>

namespace esphome::wifi_networks {

static constexpr size_t SSID_SIZE = wifi::SSID_BUFFER_SIZE;
static constexpr size_t PASSWORD_SIZE = 65;
// Networks of the latest scan that are kept, the strongest of each name
static constexpr size_t MAX_VISIBLE = 24;
static constexpr size_t MAX_LISTED = WIFI_NETWORKS_MAX + MAX_VISIBLE;

/// A network for a list to choose from: one that is saved, configured or was seen in the latest scan.
struct NetworkInfo {
  char ssid[SSID_SIZE];
  int8_t rssi;     // dBm; 0 when not seen in the latest scan
  bool visible;    // seen in the latest scan
  bool known;      // saved or configured, so it can be joined without a password
  bool saved;      // saved at run time, so it can be forgotten
  bool secured;    // needs a password
  bool skyecho;    // GDL90 data has arrived over it
  bool connected;  // the network in use
};

enum class WifiState : uint8_t {
  WIFI_STATE_OFF,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
};

struct Status {
  WifiState state;
  bool automatic;  // the network is chosen by priority, not by the user
  char ssid[SSID_SIZE];
  char ip[network::IP_ADDRESS_BUFFER_SIZE];
};

/// Keeps a list of Wi-Fi networks saved at run time, and chooses which to join.
///
/// The station list of the wifi component becomes: the networks over which SkyEcho (GDL90) data has arrived, then the
/// other saved networks, then the networks from the configuration. A SkyEcho that comes into range while another
/// network is in use is switched to. Joining a network by hand keeps that network until set_automatic(),
/// disconnect() or a restart.
///
/// The public methods may be called from any task: requests are queued for the main loop, and the lists are read
/// from a copy made there.
class WifiNetworks : public Component, public wifi::WiFiScanResultsListener, public wifi::WiFiConnectStateListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Before wifi, so its first connection already uses the saved networks
  float get_setup_priority() const override { return setup_priority::WIFI + 1.0f; }

#ifdef USE_WIFI_NETWORKS_SKYECHO
  void set_skyecho(skyecho::SkyEcho *skyecho);
#endif

  /// Copies up to max networks into dest and returns how many there are.
  size_t get_networks(NetworkInfo *dest, size_t max);
  Status get_status();
  /// Scans for networks; while connected, without leaving the network.
  void request_scan() { this->queue_(CommandType::COMMAND_TYPE_SCAN, nullptr, nullptr); }
  /// Joins ssid and keeps it. A password (empty for an open network) saves the network first; nullptr joins a known
  /// network with its own password.
  void connect(const char *ssid, const char *password) {
    this->queue_(CommandType::COMMAND_TYPE_CONNECT, ssid, password);
  }
  /// Removes a saved network.
  void forget(const char *ssid) { this->queue_(CommandType::COMMAND_TYPE_FORGET, ssid, nullptr); }
  /// Turns Wi-Fi off until connect(), set_automatic() or a restart.
  void disconnect() { this->queue_(CommandType::COMMAND_TYPE_DISCONNECT, nullptr, nullptr); }
  /// Chooses the network by priority again, turning Wi-Fi on if it was off.
  void set_automatic() { this->queue_(CommandType::COMMAND_TYPE_AUTOMATIC, nullptr, nullptr); }

  void on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) override;
  void on_wifi_connect_state(StringRef ssid, std::span<const uint8_t, 6> bssid) override;

 protected:
  enum class CommandType : uint8_t {
    COMMAND_TYPE_SCAN,
    COMMAND_TYPE_CONNECT,
    COMMAND_TYPE_FORGET,
    COMMAND_TYPE_DISCONNECT,
    COMMAND_TYPE_AUTOMATIC,
  };
  struct Command {
    CommandType type;
    bool has_password;
    char ssid[SSID_SIZE];
    char password[PASSWORD_SIZE];
  };

  struct SavedNetwork {
    char ssid[SSID_SIZE];
    char password[PASSWORD_SIZE];
    uint8_t flags;
  } PACKED;  // NOLINT
  struct SavedNetworks {
    uint8_t count;
    SavedNetwork networks[WIFI_NETWORKS_MAX];
  } PACKED;  // NOLINT
  static constexpr uint8_t FLAG_SKYECHO = 1;

  struct Visible {
    char ssid[SSID_SIZE];
    int8_t rssi;
    bool secured;
  };

  void queue_(CommandType type, const char *ssid, const char *password);
  void run_(const Command &command);
  /// Gives the wifi component the station list, and reconnects from the start if restart is set.
  void apply_(bool restart);
  void save_();
  void update_snapshot_();
  void mark_skyecho_();
  SavedNetwork *find_saved_(const char *ssid);
  const wifi::WiFiAP *find_configured_(const char *ssid) const;
  bool has_saved_skyecho_() const;
  bool current_is_skyecho_();

  ESPPreferenceObject pref_;
  SavedNetworks saved_{};
  // The networks of the configuration, as the wifi component had them before setup()
  FixedVector<wifi::WiFiAP> configured_;
  std::array<Visible, MAX_VISIBLE> visible_{};
  uint8_t visible_count_{0};
  // The network joined by hand; empty while the network is chosen by priority
  char pinned_[SSID_SIZE]{};
  char current_[SSID_SIZE]{};
  uint32_t last_snapshot_{0};
  uint32_t last_scan_{0};
  bool snapshot_dirty_{true};
  // A saved SkyEcho was seen while another network is in use
  bool switch_to_skyecho_{false};

  // Shared with other tasks, under lock_
  Mutex lock_;
  StaticVector<Command, 4> commands_;
  std::atomic<bool> has_commands_{false};
  std::array<NetworkInfo, MAX_LISTED> listed_{};
  size_t listed_count_{0};
  Status status_{};
};

}  // namespace esphome::wifi_networks
