#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/preferences.h"

#include "esphome/components/packet_interface/basic_addressing.h"
#include "esphome/components/packet_interface/xxtea_encryption.h"

#include "identity_command.h"

#ifdef USE_TRACKER_IDENTITY_FLARM
#include "esphome/components/flarm_receiver/flarm_receiver.h"
#endif
#ifdef USE_TRACKER_IDENTITY_GLIDER_TRACKER
#include "esphome/components/glider_tracker/glider_tracker.h"
#endif
#ifdef USE_TRACKER_IDENTITY_BLE
#include "esphome/components/esp32_ble/ble.h"
#endif

namespace esphome::tracker_identity {

/**
 * The tracker's identity, kept apart from the firmware, so one build serves every tracker of a board type and an
 * update leaves the identity alone: in preferences, written with SET, on ESP32; in its own flash page, written by the
 * set-up UF2, on nRF52. Answers ID and SET on the logger's serial port.
 */
class TrackerIdentity : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // Before the radio's layers, the tracker and Bluetooth start, which take what it sets
  float get_setup_priority() const override { return setup_priority::BUS + 10.0f; }

  void set_addressing(packet_interface::BasicAddressing *addressing) { this->addressing_ = addressing; }
  void set_encryption(packet_interface::XxteaEncryption *encryption) { this->encryption_ = encryption; }
#ifdef USE_TRACKER_IDENTITY_FLARM
  void set_flarm(flarm_receiver::FlarmReceiver *flarm) { this->flarm_ = flarm; }
#endif
#ifdef USE_TRACKER_IDENTITY_GLIDER_TRACKER
  void set_glider_tracker(glider_tracker::GliderTracker *tracker) { this->tracker_ = tracker; }
#endif
#ifdef USE_TRACKER_IDENTITY_BLE
  void set_ble(esp32_ble::ESP32BLE *ble) { this->ble_ = ble; }
#endif

  bool is_set() const { return this->set_; }
  const char *registration() const { return this->set_ ? this->identity_.registration : ""; }

 protected:
  /// The stored form: a magic word marks it as written
  struct Stored {
    uint32_t magic;
    Identity identity;
  };
  static constexpr uint32_t MAGIC = 0x32444947;  // "GID2", as the CubeCell's

  void apply_();
  void handle_line_(const std::string &line);
  void answer_(const std::string &text);
  int read_(uint8_t *buffer, size_t size);

  packet_interface::BasicAddressing *addressing_{nullptr};
  packet_interface::XxteaEncryption *encryption_{nullptr};
#ifdef USE_TRACKER_IDENTITY_FLARM
  flarm_receiver::FlarmReceiver *flarm_{nullptr};
#endif
#ifdef USE_TRACKER_IDENTITY_GLIDER_TRACKER
  glider_tracker::GliderTracker *tracker_{nullptr};
#endif
#ifdef USE_TRACKER_IDENTITY_BLE
  esp32_ble::ESP32BLE *ble_{nullptr};
  // The name Bluetooth keeps a pointer to
  char ble_name_[REGISTRATION_SIZE + 8]{};
#endif
#ifndef USE_ZEPHYR
  ESPPreferenceObject pref_;
#endif
  Identity identity_{};
  bool set_{false};
  std::string line_;
};

}  // namespace esphome::tracker_identity
