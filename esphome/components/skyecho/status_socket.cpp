#include "status_socket.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>

namespace esphome::skyecho {

ESPHOME_LOG_TAG(TAG, "skyecho.status");

// A real SkyEcho's "/stats" messages are about 1400 bytes
static constexpr size_t MAX_BUFFER = 4096;
// The SkyEcho sends about once a second, so a link this quiet has gone
static constexpr uint32_t IDLE_TIMEOUT_MS = 10000;
static constexpr uint32_t STATUS_MAX_AGE_MS = 5000;
static constexpr uint8_t OPCODE_TEXT = 0x1;
static constexpr uint8_t OPCODE_CLOSE = 0x8;
static constexpr uint8_t OPCODE_PING = 0x9;
static constexpr uint8_t OPCODE_PONG = 0xA;

void StatusSocket::start_(uint32_t address) {
  this->link_.close();
  this->address_ = address;
  const auto *b = reinterpret_cast<const uint8_t *>(&address);
  snprintf(this->host_, sizeof(this->host_), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  this->link_.set_host(this->host_);
  this->link_.set_port(this->port_);
  this->link_.set_idle_timeout(IDLE_TIMEOUT_MS);
  this->link_.begin(TAG);
  this->state_ = State::STATE_CLOSED;
}

void StatusSocket::loop(uint32_t address) {
  if (address == 0)
    return;
  if (address != this->address_)
    this->start_(address);
  this->link_.poll();
  if (!this->link_.connected()) {
    this->state_ = State::STATE_CLOSED;
    return;
  }
  if (this->state_ == State::STATE_CLOSED) {
    // Any key will do: the client only checks that the server agreed to the upgrade
    char request[192];
    int length = snprintf(request, sizeof(request),
                          "GET /stats HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
                          this->host_, this->port_);
    this->link_.queue(reinterpret_cast<const uint8_t *>(request), length);
    this->rx_.clear();
    this->state_ = State::STATE_HANDSHAKE;
  }
  this->link_.flush_tx();
  this->link_.check_idle();
  uint8_t buffer[512];
  ssize_t count = this->link_.read(buffer, sizeof(buffer));
  if (count < 0) {
    this->state_ = State::STATE_CLOSED;
    return;
  }
  if (count == 0)
    return;
  this->rx_.append(reinterpret_cast<const char *>(buffer), count);
  if (this->rx_.size() > MAX_BUFFER) {
    ESP_LOGW(TAG, "Status message too large; reconnecting");
    this->link_.close();
    this->link_.begin(TAG);
    this->state_ = State::STATE_CLOSED;
    return;
  }
  if (this->state_ == State::STATE_HANDSHAKE)
    this->handshake_();
  if (this->state_ == State::STATE_OPEN)
    this->frames_();
}

void StatusSocket::handshake_() {
  const size_t end = this->rx_.find("\r\n\r\n");
  if (end == std::string::npos)
    return;
  if (this->rx_.compare(0, 12, "HTTP/1.1 101") != 0) {
    ESP_LOGW(TAG, "%s:%u refused the websocket upgrade", this->host_, this->port_);
    this->link_.close();
    this->link_.begin(TAG);
    this->state_ = State::STATE_CLOSED;
    return;
  }
  ESP_LOGI(TAG, "Reading the status from %s:%u", this->host_, this->port_);
  this->rx_.erase(0, end + 4);
  this->state_ = State::STATE_OPEN;
}

void StatusSocket::frames_() {
  for (;;) {
    const auto *p = reinterpret_cast<const uint8_t *>(this->rx_.data());
    const size_t available = this->rx_.size();
    if (available < 2)
      return;
    const uint8_t opcode = p[0] & 0x0F;
    const bool masked = (p[1] & 0x80) != 0;
    uint64_t length = p[1] & 0x7F;
    size_t header = 2;
    if (length == 126) {
      if (available < 4)
        return;
      length = (uint64_t(p[2]) << 8) | p[3];
      header = 4;
    } else if (length == 127) {
      if (available < 10)
        return;
      length = 0;
      for (int i = 0; i != 8; i++)
        length = (length << 8) | p[2 + i];
      header = 10;
    }
    if (length > MAX_BUFFER) {
      this->rx_.clear();
      this->link_.close();
      this->link_.begin(TAG);
      this->state_ = State::STATE_CLOSED;
      return;
    }
    const size_t mask_at = header;
    if (masked)
      header += 4;
    if (available < header + length)
      return;
    std::string payload = this->rx_.substr(header, length);
    // Servers don't mask, but a mask is easy to undo
    if (masked) {
      for (size_t i = 0; i != payload.size(); i++)
        payload[i] = char(payload[i] ^ p[mask_at + (i % 4)]);
    }
    this->rx_.erase(0, header + length);
    if (opcode == OPCODE_TEXT) {
      this->message_(payload);
    } else if (opcode == OPCODE_PING) {
      this->send_pong_(reinterpret_cast<const uint8_t *>(payload.data()), payload.size());
    } else if (opcode == OPCODE_CLOSE) {
      ESP_LOGD(TAG, "The status websocket was closed");
      this->link_.close();
      this->link_.begin(TAG);
      this->state_ = State::STATE_CLOSED;
      return;
    }
  }
}

void StatusSocket::message_(const std::string &text) {
  json::parse_json(text, [this](JsonObject root) -> bool {
    JsonVariant satellites = root["host"]["GPS"]["NumSats"];
    if (!satellites.is<int>())
      return false;
    int value = satellites.as<int>();
    this->satellites_ = value < 0 ? 0 : (value > 255 ? 255 : uint8_t(value));
    this->status_ms_ = millis();
    this->have_status_ = true;
    return true;
  });
}

// A client's frames must be masked; a mask of zeros leaves the payload as it is
void StatusSocket::send_pong_(const uint8_t *payload, size_t length) {
  if (length > 125)
    return;
  uint8_t frame[2 + 4 + 125];
  frame[0] = 0x80 | OPCODE_PONG;
  frame[1] = 0x80 | uint8_t(length);
  memset(frame + 2, 0, 4);
  memcpy(frame + 6, payload, length);
  this->link_.queue(frame, 6 + length);
  this->link_.flush_tx();
}

bool StatusSocket::get_satellites(uint8_t *satellites) const {
  if (!this->have_status_ || millis() - this->status_ms_ > STATUS_MAX_AGE_MS)
    return false;
  *satellites = this->satellites_;
  return true;
}

}  // namespace esphome::skyecho
