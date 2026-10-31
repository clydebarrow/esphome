#include "skyecho_time.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <cinttypes>
#include <cstdlib>
#include <cstring>

namespace esphome::skyecho {

ESPHOME_LOG_TAG(TAG, "skyecho.time");

static constexpr uint32_t RETRY_ID = 0;
// Retry interval until the first sync, while there is no device or it has no GPS time yet.
static constexpr uint32_t RETRY_MS = 30000;
static constexpr uint32_t SYNC_TIMEOUT_MS = 15000;
// The GPS epoch is 1980-01-06; GPS time runs ahead of UTC by the leap seconds since then (18 from 2017).
static constexpr uint32_t GPS_EPOCH_UNIX = 315964800;
static constexpr uint32_t GPS_LEAP_SECONDS = 18;
// 2024-01-01: anything earlier is a receiver without a fix.
static constexpr uint32_t MIN_PLAUSIBLE_UNIX = 1704067200;

static constexpr uint8_t WS_OPCODE_TEXT = 1;
static constexpr uint8_t WS_OPCODE_CLOSE = 8;

void SkyEchoTime::setup() {
  this->link_.set_port(this->port_);
  this->disable_loop();
  // The device is known only once GDL90 data arrives, so the first attempt waits for it.
  this->set_timeout(RETRY_ID, RETRY_MS, [this]() { this->update(); });
}

void SkyEchoTime::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SkyEcho time:\n"
                "  Websocket port: %u",
                this->port_);
  RealTimeClock::dump_config();
}

void SkyEchoTime::update() {
  if (this->state_ != SyncState::SYNC_STATE_IDLE)
    return;
  const uint32_t address = this->parent_->get_source_address();
  if (address == 0) {
    ESP_LOGD(TAG, "No SkyEcho seen yet");
    if (!this->synced_)
      this->set_timeout(RETRY_ID, RETRY_MS, [this]() { this->update(); });
    return;
  }
  const auto *octets = reinterpret_cast<const uint8_t *>(&address);
  snprintf(this->host_, sizeof(this->host_), "%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
  this->link_.set_host(this->host_);
  this->link_.begin(TAG);
  this->rx_len_ = 0;
  this->skip_ = 0;
  this->started_ms_ = App.get_loop_component_start_time();
  this->state_ = SyncState::SYNC_STATE_CONNECTING;
  this->enable_loop();
}

void SkyEchoTime::loop() {
  if (App.get_loop_component_start_time() - this->started_ms_ > SYNC_TIMEOUT_MS) {
    ESP_LOGW(TAG, "No time from %s:%u", this->host_, this->port_);
    this->finish_(false);
    return;
  }
  this->link_.poll();
  if (!this->link_.connected()) {
    // A dropped link reconnects, and the request goes out again.
    if (this->state_ != SyncState::SYNC_STATE_CONNECTING) {
      this->state_ = SyncState::SYNC_STATE_CONNECTING;
      this->rx_len_ = 0;
      this->skip_ = 0;
    }
    return;
  }
  if (this->state_ == SyncState::SYNC_STATE_CONNECTING) {
    // The key is the example from RFC 6455; the server only echoes a hash of it.
    const int len = snprintf(reinterpret_cast<char *>(this->link_.tx_tail()), this->link_.tx_free(),
                             "GET /stats HTTP/1.1\r\n"
                             "Host: %s:%u\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                             "Sec-WebSocket-Version: 13\r\n\r\n",
                             this->host_, this->port_);
    this->link_.tx_commit(len);
    this->state_ = SyncState::SYNC_STATE_HANDSHAKE;
  }
  this->link_.flush_tx();

  // Read until the socket would block, as Socket::ready() requires.
  for (;;) {
    const ssize_t count =
        this->link_.read(reinterpret_cast<uint8_t *>(this->rx_) + this->rx_len_, RX_BUFFER_SIZE - this->rx_len_);
    if (count <= 0)
      return;
    this->rx_len_ += count;
    if (this->state_ == SyncState::SYNC_STATE_HANDSHAKE) {
      this->rx_[this->rx_len_] = 0;
      char *end = strstr(this->rx_, "\r\n\r\n");
      if (end == nullptr) {
        if (this->rx_len_ == RX_BUFFER_SIZE) {
          ESP_LOGW(TAG, "Response header too long");
          this->finish_(false);
          return;
        }
        continue;
      }
      if (strncmp(this->rx_, "HTTP/1.1 101", 12) != 0) {
        char *eol = strchr(this->rx_, '\r');
        *eol = 0;
        ESP_LOGW(TAG, "Websocket refused: %s", this->rx_);
        this->finish_(false);
        return;
      }
      end += 4;
      this->rx_len_ -= end - this->rx_;
      memmove(this->rx_, end, this->rx_len_);
      this->state_ = SyncState::SYNC_STATE_FRAMES;
    }
    if (this->parse_frames_()) {
      this->finish_(true);
      return;
    }
    if (this->state_ == SyncState::SYNC_STATE_IDLE)
      return;
  }
}

bool SkyEchoTime::parse_frames_() {
  for (;;) {
    if (this->skip_ != 0) {
      const uint32_t n = std::min<uint32_t>(this->skip_, this->rx_len_);
      this->skip_ -= n;
      this->rx_len_ -= n;
      memmove(this->rx_, this->rx_ + n, this->rx_len_);
      if (this->skip_ != 0)
        return false;
    }
    // Server frames are not masked: 2 header bytes, then a 16 or 64 bit length when the 7 bit one is 126 or 127.
    if (this->rx_len_ < 2)
      return false;
    const auto *hdr = reinterpret_cast<const uint8_t *>(this->rx_);
    const uint8_t opcode = hdr[0] & 0x0F;
    uint64_t payload_len = hdr[1] & 0x7F;
    size_t header_len = 2;
    if (payload_len == 126) {
      header_len = 4;
    } else if (payload_len == 127) {
      header_len = 10;
    }
    if (this->rx_len_ < header_len)
      return false;
    if (header_len == 4) {
      payload_len = encode_uint16(hdr[2], hdr[3]);
    } else if (header_len == 10) {
      payload_len = 0;
      for (size_t i = 2; i != 10; i++)
        payload_len = (payload_len << 8) | hdr[i];
    }
    if (opcode == WS_OPCODE_CLOSE) {
      ESP_LOGW(TAG, "Websocket closed by %s", this->host_);
      this->finish_(false);
      return false;
    }
    if (header_len + payload_len > RX_BUFFER_SIZE) {
      ESP_LOGD(TAG, "Skipped a %" PRIu32 " byte frame", (uint32_t) payload_len);
      this->skip_ = header_len + payload_len;
      continue;
    }
    const size_t frame_len = header_len + payload_len;
    if (this->rx_len_ < frame_len)
      return false;
    if (opcode == WS_OPCODE_TEXT) {
      char *text = this->rx_ + header_len;
      const char saved = text[payload_len];
      text[payload_len] = 0;
      const bool synced = this->parse_stats_(text);
      text[payload_len] = saved;
      if (synced)
        return true;
    }
    // Pings are not answered: the connection only lives until the first message with the time.
    this->rx_len_ -= frame_len;
    memmove(this->rx_, this->rx_ + frame_len, this->rx_len_);
  }
}

bool SkyEchoTime::parse_stats_(char *text) {
  // {"host": {"GPS": {"UTCTime": <seconds since the GPS epoch>, ...}}, ...}
  const char *field = strstr(text, "\"UTCTime\"");
  if (field == nullptr)
    return false;
  const char *colon = strchr(field, ':');
  if (colon == nullptr)
    return false;
  const uint32_t gps = strtoul(colon + 1, nullptr, 10);
  const uint32_t unix_time = gps + GPS_EPOCH_UNIX - GPS_LEAP_SECONDS;
  if (gps == 0 || unix_time < MIN_PLAUSIBLE_UNIX) {
    ESP_LOGD(TAG, "GPS time %" PRIu32 " not valid yet", gps);
    return false;
  }
  this->synchronize_epoch_(unix_time);
  return true;
}

void SkyEchoTime::finish_(bool synced) {
  this->link_.close();
  this->state_ = SyncState::SYNC_STATE_IDLE;
  this->rx_len_ = 0;
  this->skip_ = 0;
  this->disable_loop();
  if (synced) {
    this->synced_ = true;
    this->cancel_timeout(RETRY_ID);
  } else if (!this->synced_) {
    this->set_timeout(RETRY_ID, RETRY_MS, [this]() { this->update(); });
  }
}

}  // namespace esphome::skyecho
