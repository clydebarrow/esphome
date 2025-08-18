#include "goodwe.h"
#include "esphome/components/bytebuffer/bytebuffer.h"

namespace esphome {
namespace goodwe {

static constexpr size_t MAX_MSG_LEN = 255 + 9;
static constexpr uint8_t RX_HDR[] = {0xAA, 0x55, 0xC0, 0x7F};
static constexpr size_t RESPONSE_CODE = 5;
static constexpr size_t LEN_BYTE = 7;
static constexpr size_t CHKSUM_LEN = 2;

// request and response codes. Response has 0x80 added

static constexpr uint16_t RUNNING_DATA = 0x106;
static constexpr uint16_t VERSION_DATA = 0x102;
static constexpr uint16_t SETTINGS_DATA = 0x109;

void Goodwe::on_receive_(const std::vector<uint8_t> &data) {
  // for now just look for AA 55 commands
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
    if (this->buffer_.size() < LEN_BYTE) {
      continue;  // not enough data yet
    }
    if (this->buffer_[LEN_BYTE] + LEN_BYTE + CHKSUM_LEN == this->buffer_.size()) {
      // we have a complete message
      ESP_LOGD(TAG, "Received %s", format_hex_pretty(data).c_str());
      this->buffer_.clear();
      continue;
    }
  }
}

void Goodwe::process_data_() {
  auto packet = bytebuffer::ByteBuffer::wrap(this->buffer_, bytebuffer::BIG);
  uint16_t checksum = 0;
  for (size_t i = 0; i < this->buffer_.size() - CHKSUM_LEN; i++) {
    checksum += this->buffer_[i];
  }
  if (checksum != packet.get_uint16(this->buffer_.size() - CHKSUM_LEN)) {
    ESP_LOGD(TAG, "Checksum mismatch, expected %04X, got %04X", checksum,
             packet.get_uint16(this->buffer_.size() - CHKSUM_LEN));
    return;
  }
  switch (packet.get_uint16(RESPONSE_CODE) {
    case RUNNING_DATA + 0x80:
      ESP_LOGD(TAG, "Running data response");
      auto pv1 = break;
  }
}
}  // namespace goodwe

}  // namespace esphome
