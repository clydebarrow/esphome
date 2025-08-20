#include "goodwe.h"
#include "esphome/components/bytebuffer/bytebuffer.h"

namespace esphome {
namespace goodwe {

static constexpr size_t MAX_MSG_LEN = 255 + 9;
static constexpr uint8_t RX_HDR[] = {0xAA, 0x55, 0x7F, 0xC0};
static constexpr size_t RESPONSE_CODE = 4;
static constexpr size_t LEN_BYTE = 6;
static constexpr size_t CHKSUM_LEN = 2;

// request and response codes. Response has 0x80 added

static constexpr uint16_t RUNNING_DATA = 0x106;
static constexpr uint16_t VERSION_DATA = 0x102;
static constexpr uint16_t SETTINGS_DATA = 0x109;

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
  ESP_LOGD(TAG, "Received buffer %s", format_hex_pretty(data).c_str());
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
    if (this->buffer_.size() <= LEN_BYTE + CHKSUM_LEN) {
      continue;  // not enough data yet
    }
    if (this->buffer_[LEN_BYTE] + LEN_BYTE + CHKSUM_LEN + 1 == this->buffer_.size()) {
      // we have a complete message
      ESP_LOGD(TAG, "Received %s", format_hex_pretty(this->buffer_).c_str());
      this->process_data_();
      this->buffer_.clear();
      continue;
    }
  }
}

void Goodwe::process_data_() {
  auto packet = bytebuffer::ByteBuffer::wrap(this->buffer_, bytebuffer::BIG);
  uint16_t checksum = 0;
  for (size_t i = 0; i != this->buffer_.size() - CHKSUM_LEN; i++) {
    checksum += this->buffer_[i];
  }
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
}  // namespace goodwe

}  // namespace esphome
