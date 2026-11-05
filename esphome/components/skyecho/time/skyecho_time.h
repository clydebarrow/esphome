#pragma once

#include "esphome/components/socket/tcp_client_link.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/helpers.h"
#include "../skyecho.h"

namespace esphome::skyecho {

/// Sets the clock from the GPS time in the SkyEcho's websocket "/stats" stream, the only source of the date:
/// GDL90 sends the time of day alone. Each update() connects to the device the GDL90 data comes from, reads
/// messages until one holds the time, then closes the connection. With a pressure sensor the connection stays
/// open, and each message (about one a second) gives the pressure from the SkyEcho's barometer.
class SkyEchoTime : public time::RealTimeClock, public Parented<SkyEcho> {
 public:
  explicit SkyEchoTime(uint16_t port) : port_(port) {}

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
#ifdef USE_SENSOR
  void set_pressure_sensor(sensor::Sensor *sensor) { this->pressure_sensor_ = sensor; }
#endif

 protected:
  enum class SyncState : uint8_t {
    SYNC_STATE_IDLE,
    SYNC_STATE_CONNECTING,  // waiting for the connection; the upgrade request goes out once it is up
    SYNC_STATE_HANDSHAKE,   // waiting for the end of the HTTP response
    SYNC_STATE_FRAMES,      // reading websocket frames
  };

  void finish_(bool synced);
  /// Consumes complete frames from the start of rx_; returns true once the time is set.
  bool parse_frames_();
  /// Takes the time, if it is still wanted, and the pressure from one message; true once the time is set.
  bool parse_stats_(char *text);
  bool streaming_() const;
  void send_pong_(const uint8_t *payload, size_t len);

  // A real SkyEcho's "/stats" messages are about 1400 bytes
  static constexpr size_t RX_BUFFER_SIZE = 2048;

  socket::TcpClientLink link_;
  uint32_t started_ms_{0};
  // A frame too big for rx_ is dropped; this many of its bytes are still to come.
  uint32_t skip_{0};
  uint16_t port_;
  uint16_t rx_len_{0};
  SyncState state_{SyncState::SYNC_STATE_IDLE};
  bool synced_{false};
  // The time is wanted from this connection
  bool want_time_{false};
#ifdef USE_SENSOR
  sensor::Sensor *pressure_sensor_{nullptr};
#endif
  char host_[16]{};
  // One byte more than RX_BUFFER_SIZE to terminate a text payload in place.
  char rx_[RX_BUFFER_SIZE + 1]{};
};

}  // namespace esphome::skyecho
