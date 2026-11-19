#pragma once

#include "esphome/core/defines.h"
#include "esphome/components/socket/tcp_client_link.h"

#include <cstdint>
#include <string>

namespace esphome::skyecho {

/**
 * The SkyEcho's "/stats" websocket stream, which reports what GDL90 doesn't, such as how many satellites its GPS is
 * using. It is a message about once a second, as JSON:
 *   {"host": {"GPS": {"NumSats": 7, "FixType": 3, "UTCTime": ..., ...}}, ...}
 * The time platform reads the same stream once, for the date; this keeps it open.
 * The client connects to the address the GDL90 packets come from, upgrades to a websocket, and reads text frames. It
 * sends nothing but the upgrade request and answers to pings, so it needs no websocket library.
 */
class StatusSocket {
 public:
  void set_port(uint16_t port) { this->port_ = port; }
  uint16_t get_port() const { return this->port_; }
  /// Call every loop with the SkyEcho's IPv4 address in network order, or 0 while it is unknown
  void loop(uint32_t address);
  /// The satellites the SkyEcho's GPS is using; false unless a status message arrived lately
  bool get_satellites(uint8_t *satellites) const;

 protected:
  enum class State : uint8_t { STATE_CLOSED, STATE_HANDSHAKE, STATE_OPEN };

  void start_(uint32_t address);
  void handshake_();
  void frames_();
  void message_(const std::string &text);
  void send_pong_(const uint8_t *payload, size_t length);

  socket::TcpClientLink link_;
  // The link keeps a reference to the host text, so it lives here
  char host_[16]{};
  uint32_t address_{0};
  uint16_t port_{80};
  State state_{State::STATE_CLOSED};
  std::string rx_;
  uint8_t satellites_{0};
  uint32_t status_ms_{0};
  bool have_status_{false};
};

}  // namespace esphome::skyecho
