#pragma once

#include <array>
#include <cmath>

#include "esphome/components/sx126x/sx126x.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/line_reader.h"

#include "legacy_protocol.h"

namespace esphome::flarm_receiver {

struct BandPlan {
  uint32_t base_frequency;  // Hz, channel 0
  uint32_t spacing;         // Hz
  uint8_t channels;
  bool hopping;  // channels picked by a hash of the time; otherwise slot 0 uses channel 0 and slot 1 channel 1
};

/// Receives legacy FLARM position packets with an SX126x radio, following FLARM's channel hopping, and sends what
/// a FLARM sends to a navigation program: the GPS sentences, $PFLAU and a $PFLAA for each aircraft heard.
/// The radio needs the GPS time, to the tenth of a second, to hop with the senders and to decrypt the packets, and a
/// position near the senders to place them. Both come from the GPS UART, or from the system clock and a fixed
/// position when there is none.
class FlarmReceiver : public Component, public uart::UARTDevice, public sx126x::SX126xListener {
 public:
  FlarmReceiver(sx126x::SX126x *radio, const BandPlan &plan) : radio_(radio), plan_(plan) {}
  void setup() override;
  void loop() override;
  void dump_config() override;

  void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) override;

  /// The position used until the GPS has a fix, and always without a GPS
  void set_fixed_position(float latitude, float longitude, float altitude) {
    this->latitude_ = latitude;
    this->longitude_ = longitude;
    this->altitude_ = altitude;
    this->has_position_ = true;
  }
  void set_timeout(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }
  /// Send this aircraft's position in each slot, while the GPS has a fix. An address of 0 is taken from
  /// set_registration(), or until then made from the MAC address.
  void set_transmit(uint32_t address, uint8_t address_type, uint8_t aircraft_type, bool no_track) {
    this->own_address_ = address;
    this->own_address_type_ = address_type;
    this->own_aircraft_type_ = aircraft_type;
    this->no_track_ = no_track;
    this->address_fixed_ = address != 0;
    this->transmit_ = true;
  }
  /// The registration of the aircraft, such as "GKP" or "VH-GKP", whose ICAO address is sent unless one was
  /// configured. An empty or unknown registration sends the FLARM ID made from the MAC address.
  void set_registration(const char *registration);

  /// The ICAO address of an Australian registration, three letters or digits after "VH-"; false if it is not one.
  static bool australian_address(const char *registration, uint32_t &address);

  /// Called with each NMEA sentence sent, "\r\n" included
  template<typename F> void add_on_nmea_callback(F &&callback) { this->nmea_callback_.add(std::forward<F>(callback)); }

  uint32_t get_packet_count() const { return this->packet_count_; }
  uint32_t get_transmit_count() const { return this->transmit_count_; }

 protected:
  struct Traffic {
    LegacyTarget target;
    uint32_t last_seen_ms;
    float rssi;
    bool active;
    bool confirmed;  // two packets agreed on its position
  };
  static constexpr size_t MAX_TRAFFIC = 16;
  static constexpr size_t NMEA_LINE_SIZE = 100;

  bool has_gps_() const { return this->parent_ != nullptr; }
  bool utc_ms_(int64_t &utc_ms) const;
  void hop_();
  void send_position_();
  void read_gps_();
  void parse_sentence_(char *line, size_t len);
  void send_traffic_();
  void send_sentence_(const char *body);
  void send_line_(const char *line, size_t len);

  sx126x::SX126x *radio_;
  BandPlan plan_;
  CallbackManager<void(const uint8_t *, size_t)> nmea_callback_;
  LineReader<NMEA_LINE_SIZE> line_reader_;
  std::array<Traffic, MAX_TRAFFIC> traffic_{};

  // UTC in milliseconds is millis_64() plus this, from the GPS
  int64_t utc_offset_ms_{0};
  bool gps_time_valid_{false};

  float latitude_{NAN};
  float longitude_{NAN};
  float altitude_{0};          // meters above mean sea level
  float geoid_separation_{0};  // meters from the ellipsoid up to mean sea level
  bool has_position_{false};
  bool gps_fix_{false};
  float ground_speed_{0};  // meters per second
  float course_{0};        // degrees true
  float climb_rate_{0};    // meters per second, from the GPS altitude
  bool has_altitude_{false};
  uint32_t last_altitude_ms_{0};

  // Transmitting: the slot (second * 2 + slot) the next packet is for, and when in UTC to send it
  uint32_t own_address_{0};
  uint8_t own_address_type_{0};
  uint8_t own_aircraft_type_{0};
  bool no_track_{false};
  bool address_fixed_{false};
  bool transmit_{false};
  bool transmit_pending_{false};
  uint32_t slot_key_{0};
  int64_t transmit_at_ms_{0};
  uint32_t last_transmit_ms_{0};
  uint32_t transmit_count_{0};

  uint32_t timeout_ms_{0};
  uint32_t last_traffic_ms_{0};
  uint32_t packet_count_{0};
  int channel_{-1};
};

}  // namespace esphome::flarm_receiver
