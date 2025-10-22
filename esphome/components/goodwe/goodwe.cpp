#include "goodwe.h"
#include <cinttypes>
#include "esphome/components/bytebuffer/bytebuffer.h"

namespace esphome {
namespace goodwe {
static constexpr size_t MAX_MSG_LEN = 255 + 9;
static constexpr uint8_t RX_HDR[] = {0xAA, 0x55, 0x7F, 0xC0};
static constexpr size_t RESPONSE_CODE = 4;
static constexpr size_t LENGTH_POS = 6;
static constexpr size_t CHKSUM_LEN = 2;

void Goodwe::dump_config() {
  ESP_LOGCONFIG(TAG, "Goodwe:");
  for (auto &pair : this->parameters_) {
    ESP_LOGCONFIG(TAG, "  Msg %04X:", pair.first);
    for (auto *p : pair.second) {
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
      continue;
    }
  }
}

static uint16_t calc_check(std::vector<uint8_t> &buffer, size_t size) {
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
  for (auto *p : this->parameters_[msgcode]) {
    p->decode(packet);
  }
}

void Goodwe::send_command_(uint16_t cmd) {
  std::vector<uint8_t> buffer{0xAA, 0x55, 0xC0, 0x7F, (uint8_t) (cmd >> 8), (uint8_t) cmd, 0x00};
  auto checksum = calc_check(buffer, buffer.size());
  buffer.push_back(checksum >> 8);
  buffer.push_back(checksum);
  this->transport_->transmit(buffer);
  ESP_LOGV(TAG, "Sending command %s", format_hex_pretty(buffer).c_str());
}

void Goodwe::update() {
  for (auto q : this->queries_) {
    if (this->counter_ % q.second == 0) {
      this->send_command_(q.first);
      break;
    }
  }
  this->counter_++;
}
}  // namespace goodwe

}  // namespace esphome
