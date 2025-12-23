#include "goodwe.h"
#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/components/bytebuffer/bytebuffer.h"

namespace esphome {
namespace goodwe {
static constexpr uint8_t RX_HDR[] = {0xAA, 0x55, 0x7F, 0xC0};
static constexpr size_t RESPONSE_CODE = 4;
static constexpr size_t LENGTH_POS = 6;
static constexpr size_t CHKSUM_LEN = 2;
static constexpr uint16_t CMD_VERSION = 0x102;
static constexpr uint16_t CMD_SETTINGS = 0x109;
static constexpr uint16_t SET_PREFIX = 0x300;  // Settings are 0x300 + offset

void Goodwe::dump_config() {
  ESP_LOGCONFIG(TAG, "Goodwe:");
  for (auto &message : this->messages_) {
    ESP_LOGCONFIG(TAG, "  Msg %04X:", message.first);
    ESP_LOGCONFIG(TAG, "  Interval %ds:", message.second.interval_);
    for (auto p : message.second.parameters_) {
      p->dump_config();
    }
  }
}
void Goodwe::on_receive_(const std::vector<uint8_t> &data) {
  // for now just look for AA 55 commands
  ESP_LOGV(TAG, "Received buffer %s", format_hex_pretty(data).c_str());
  for (auto &b : data) {
    auto pos = this->buffer_.size();
    if (pos < sizeof(RX_HDR)) {
      if (b == RX_HDR[pos]) {
        this->buffer_.push_back(b);
      } else {
        this->buffer_.clear();
      }
      continue;
    }
    this->buffer_.push_back(b);
    if (this->buffer_.size() <= LENGTH_POS + CHKSUM_LEN) {
      continue;  // not enough data yet
    }
    if (this->buffer_[LENGTH_POS] + LENGTH_POS + CHKSUM_LEN + 1 == this->buffer_.size()) {
      // we have a complete message
      ESP_LOGD(TAG, "Received %s", format_hex_pretty(this->buffer_).c_str());
      this->process_data_();
      this->buffer_.clear();
    }
  }
}

static uint16_t calc_check(const std::vector<uint8_t> &buffer, size_t size) {
  uint16_t checksum = 0;
  for (size_t i = 0; i != size; i++) {
    checksum += buffer[i];
  }
  return checksum;
}

void Goodwe::process_data_() {
  auto packet = bytebuffer::ByteBuffer::wrap(this->buffer_, bytebuffer::BIG);
  auto checksum = calc_check(this->buffer_, this->buffer_.size() - CHKSUM_LEN);
  if (checksum != packet.get_uint16(this->buffer_.size() - CHKSUM_LEN)) {
    ESP_LOGD(TAG, "Checksum mismatch, expected %04X, got %04X", checksum,
             packet.get_uint16(this->buffer_.size() - CHKSUM_LEN));
    return;
  }
  auto msgcode = packet.get_uint16(RESPONSE_CODE) & ~0x80;  // remove the response bit
  ESP_LOGD(TAG, "Processing message with code %04X", msgcode);
  if (msgcode == CMD_VERSION) {
    this->arm_version_ = packet.get_uint8(4) - '0';
    if (this->arm_version_ > 9) {
      this->arm_version_ -= 'A' - '0';
    }
    ESP_LOGD(TAG, "ARM version %d", this->arm_version_);
  }
  auto message = this->messages_.find(msgcode);
  if (message != this->messages_.end()) {
    message->second.decode(packet);
  } else {
    ESP_LOGE(TAG, "Unrecognised message code %X", msgcode);
  }
  if (msgcode == this->last_command_) {
    this->last_command_ = 0;
    if ((msgcode & 0xF00) == SET_PREFIX) {
      // have just set something, query data to make sure it took effect.
      this->send_command_(CMD_SETTINGS);
    }
  }
}

void Goodwe::send_command_(uint16_t cmd, std::optional<std::vector<uint8_t>> data) {
  std::vector<uint8_t> buffer{0xAA, 0x55, 0xC0, 0x7F};
  buffer.push_back(cmd >> 8);
  buffer.push_back(cmd);
  if (data.has_value()) {
    auto &bytes = data.value();
    buffer.push_back(bytes.size());
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
  } else {
    buffer.push_back(0);
  }
  auto checksum = calc_check(buffer, buffer.size());
  buffer.push_back(checksum >> 8);
  buffer.push_back(checksum);
  this->transport_->transmit(buffer);
  this->last_command_ = cmd;
  this->last_command_time_ = millis();
  ESP_LOGD(TAG, "Sending command %s", format_hex_pretty(buffer).c_str());
}

bool Goodwe::is_waiting() {
  if (this->last_command_ != 0) {
    auto duration = millis() - this->last_command_time_;
    if (duration < COMMAND_TIMEOUT_MS) {
      return true;
    }
    ESP_LOGW(TAG, "Command %04X did not respond", this->last_command_);
    this->last_command_ = 0;
  }
  return false;
}

void Goodwe::update() {
  if (is_waiting())
    return;
  this->counter_++;
  if (this->counter_ & 1) {
    for (auto *setting : this->settings_) {
      if (setting->should_send()) {
        this->send_command_(setting->get_cmd_code(), setting->get_data());
        return;
      }
    }
    return;
  }

  for (auto q : this->queries_) {
    if (this->counter_ % q.second == 0) {
      this->send_command_(q.first);
      break;
    }
  }
}

void Goodwe::loop() {
  if (is_waiting())
    return;
}
}  // namespace goodwe

}  // namespace esphome
