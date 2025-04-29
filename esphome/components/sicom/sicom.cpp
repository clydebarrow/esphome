#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "sicom.h"

namespace esphome {
namespace sicom {

static const char *TAG = "sicom";
static const uint32_t BAUD_RATE = 115200;
static const uint16_t SYMBOL_LENGTH = 16;
static const size_t MAX_DEVICES = 32;

static const uint16_t crc_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5,
    0xe97e, 0xf8f7, 0x0919, 0x1890, 0x2a0b, 0x3b82, 0x4f3d, 0x5eb4, 0x6c2f, 0x7da6, 0x8551, 0x94d8, 0xa643, 0xb7ca,
    0xc375, 0xd2fc, 0xe067, 0xf1ee, 0x1232, 0x03bb, 0x3120, 0x20a9, 0x5416, 0x459f, 0x7704, 0x668d, 0x9e7a, 0x8ff3,
    0xbd68, 0xace1, 0xd85e, 0xc9d7, 0xfb4c, 0xeac5, 0x1b2b, 0x0aa2, 0x3839, 0x29b0, 0x5d0f, 0x4c86, 0x7e1d, 0x6f94,
    0x9763, 0x86ea, 0xb471, 0xa5f8, 0xd147, 0xc0ce, 0xf255, 0xe3dc, 0x2464, 0x35ed, 0x0776, 0x16ff, 0x6240, 0x73c9,
    0x4152, 0x50db, 0xa82c, 0xb9a5, 0x8b3e, 0x9ab7, 0xee08, 0xff81, 0xcd1a, 0xdc93, 0x2d7d, 0x3cf4, 0x0e6f, 0x1fe6,
    0x6b59, 0x7ad0, 0x484b, 0x59c2, 0xa135, 0xb0bc, 0x8227, 0x93ae, 0xe711, 0xf698, 0xc403, 0xd58a, 0x3656, 0x27df,
    0x1544, 0x04cd, 0x7072, 0x61fb, 0x5360, 0x42e9, 0xba1e, 0xab97, 0x990c, 0x8885, 0xfc3a, 0xedb3, 0xdf28, 0xcea1,
    0x3f4f, 0x2ec6, 0x1c5d, 0x0dd4, 0x796b, 0x68e2, 0x5a79, 0x4bf0, 0xb307, 0xa28e, 0x9015, 0x819c, 0xf523, 0xe4aa,
    0xd631, 0xc7b8, 0x48c8, 0x5941, 0x6bda, 0x7a53, 0x0eec, 0x1f65, 0x2dfe, 0x3c77, 0xc480, 0xd509, 0xe792, 0xf61b,
    0x82a4, 0x932d, 0xa1b6, 0xb03f, 0x41d1, 0x5058, 0x62c3, 0x734a, 0x07f5, 0x167c, 0x24e7, 0x356e, 0xcd99, 0xdc10,
    0xee8b, 0xff02, 0x8bbd, 0x9a34, 0xa8af, 0xb926, 0x5afa, 0x4b73, 0x79e8, 0x6861, 0x1cde, 0x0d57, 0x3fcc, 0x2e45,
    0xd6b2, 0xc73b, 0xf5a0, 0xe429, 0x9096, 0x811f, 0xb384, 0xa20d, 0x53e3, 0x426a, 0x70f1, 0x6178, 0x15c7, 0x044e,
    0x36d5, 0x275c, 0xdfab, 0xce22, 0xfcb9, 0xed30, 0x998f, 0x8806, 0xba9d, 0xab14, 0x6cac, 0x7d25, 0x4fbe, 0x5e37,
    0x2a88, 0x3b01, 0x099a, 0x1813, 0xe0e4, 0xf16d, 0xc3f6, 0xd27f, 0xa6c0, 0xb749, 0x85d2, 0x945b, 0x65b5, 0x743c,
    0x46a7, 0x572e, 0x2391, 0x3218, 0x0083, 0x110a, 0xe9fd, 0xf874, 0xcaef, 0xdb66, 0xafd9, 0xbe50, 0x8ccb, 0x9d42,
    0x7e9e, 0x6f17, 0x5d8c, 0x4c05, 0x38ba, 0x2933, 0x1ba8, 0x0a21, 0xf2d6, 0xe35f, 0xd1c4, 0xc04d, 0xb4f2, 0xa57b,
    0x97e0, 0x8669, 0x7787, 0x660e, 0x5495, 0x451c, 0x31a3, 0x202a, 0x12b1, 0x0338, 0xfbcf, 0xea46, 0xd8dd, 0xc954,
    0xbdeb, 0xac62, 0x9ef9, 0x8f70};

static uint16_t calculateCRC16(const std::vector<uint8_t> &data) {
  uint16_t crc = 0x0000;  // Initial value

  for (uint8_t byte : data) {
    uint8_t tbl_idx = ((crc >> 8) ^ byte);
    crc = (crc_table[tbl_idx] ^ (crc << 8));
  }
  return crc;
}

static const uint8_t BROADCAST_ADDRESS = 0xFA;
static const uint8_t REQUEST_DATA_MSG = 0x0A;
static const uint8_t REPLY_DATA_MSG = 0xA0;
static const uint8_t ALL_CALL_MSG = 0x31;
static const uint8_t ENTROL_MSG = 0x20;
static const uint8_t DEVICE_HELLO = 0xC0;
static const uint8_t DEVICE_ACK = 0xC1;

static const size_t MAX_MSG_LEN = 64;
static const size_t MIN_MSG_LEN = 5;
static const size_t ADDR_OFFS = 0;
static const size_t LEN_OFFS = 1;
static const size_t TYPE_OFFs = 2;
static const size_t CMD_OFFS = 3;

static void add_crc(std::vector<uint8_t> &data) {
  data[LEN_OFFS] = data.size() + 1;
  auto crc = calculateCRC16(data);
  data.push_back(crc >> 8);
  data.push_back(crc & 0xFF);
}

bool SicomSensor::decode(ByteBuffer &buffer) {
  if (buffer.get_limit() < this->offset_ + this->length_) {
    return false;
  }
  float value = NAN;
  switch (this->data_type_) {
    case SIGNED16:
      value = buffer.get_int16(this->offset_) * this->scale_;
      break;
    case UNSIGNED16:
      value = buffer.get_uint16(this->offset_) * this->scale_;
      break;
    case SIGNED32:
      value = buffer.get_int32(this->offset_) * this->scale_;
      break;
    case UNSIGNED32:
      value = buffer.get_uint32(this->offset_) * this->scale_;
      break;
    default:
      break;
  }

  this->sensor_->publish_state(value);
  return true;
}
void SicomDevice::invalidate() {
  auto elapsed = millis() - this->last_data_time_;
  if (elapsed > 4000) {
    this->state_ = UNSEEN;
    for (auto &sensor : this->sensors_)
      sensor->invalidate();
    if (this->status_sensor_ != nullptr)
      this->status_sensor_->publish_state(false);
  }
}

void SicomComponent::setup() {
  if (this->tx_enable_pin_ != nullptr) {
    this->tx_enable_pin_->setup();
    this->tx_enable_pin_->digital_write(false);
  }
  rmt_tx_channel_config_t channel{};
  channel.clk_src = RMT_CLK_SRC_DEFAULT;
  channel.resolution_hz = BAUD_RATE * SYMBOL_LENGTH;
  channel.gpio_num = (gpio_num_t) this->tx_pin_;
  channel.flags.with_dma = 0;
  channel.mem_block_symbols = 96;
  channel.trans_queue_depth = 1;
  channel.flags.io_loop_back = 0;
  channel.flags.io_od_mode = 0;
  channel.flags.invert_out = 0;
  channel.intr_priority = 0;
  if (rmt_new_tx_channel(&channel, &this->channel_) != ESP_OK) {
    ESP_LOGE(TAG, "Channel creation failed");
    this->mark_failed();
    return;
  }
  if (rmt_enable(this->channel_) != ESP_OK) {
    ESP_LOGE(TAG, "Enabling channel failed");
    this->mark_failed();
    return;
  }
  if (rmt_apply_carrier(this->channel_, nullptr) != ESP_OK) {
    ESP_LOGE(TAG, "Applying carrier failed");
    this->mark_failed();
    return;
  }
  rmt_copy_encoder_config_t encoder_config{};
  rmt_new_copy_encoder(&encoder_config, &this->encoder_);
  this->transmit_config_.loop_count = 0;
  this->transmit_config_.flags.eot_level = 1;
  this->transmit_config_.flags.queue_nonblocking = false;
}

bool SicomComponent::try_read_(uint8_t *data) {
  if (this->available() == 0)
    return false;
  this->read_array(data, 1);
  return true;
}

// Try to read a message, starting with the given address
// If the address is not found, return an empty vector
std::vector<uint8_t> SicomComponent::read_message_() {
  uint8_t byte = 0xFF;
  std::vector<uint8_t> buffer{};
  if (this->available() < MIN_MSG_LEN) {
    return {};
  }
  for (;;) {
    if (!this->try_read_(&byte))
      return {};
    if (byte != BROADCAST_ADDRESS && byte >= this->devices_.size()) {
      continue;
    }
    buffer.push_back(byte);
    // get the message length
    if (!this->try_read_(&byte))
      return {};
    if (byte >= MIN_MSG_LEN && byte <= MAX_MSG_LEN)
      break;
    buffer.clear();
  }
  buffer.push_back(byte);
  auto len = byte;
  while (--len != 0) {
    if (!this->try_read_(&byte))
      return {};
    buffer.push_back(byte);
  }
  if (this->debug_)
    ESP_LOGD(TAG, "Received message: %s", format_hex_pretty(buffer).c_str());
  if (calculateCRC16(buffer) == 0)
    return buffer;
  ESP_LOGD(TAG, "Failed checksum; message: %s", format_hex_pretty(buffer).c_str());
  return {};
}

void SicomComponent::send_enrol_message_() {
  auto device = this->devices_[this->next_device_];
  ByteBuffer message = ByteBuffer(6, BIG);
  message.put_uint8(ENTROL_MSG);
  message.put_uint32(device->get_serial_number());
  message.put_uint8(this->next_device_ + 1);
  this->send_message_(BROADCAST_ADDRESS, message.get_data());
}
void SicomComponent::enrol_device_(std::vector<uint8_t> &data) {
  if (data.size() == 10 && data[3] == DEVICE_HELLO) {
    auto buffer = ByteBuffer::wrap(data, LITTLE);
    auto serial = buffer.get_uint32(4);
    auto id = buffer.get_uint8(2);
    ESP_LOGD(TAG, "Device Hello: id=%02X, serial=%08lX", id, serial);
    for (auto i = 0; i != this->devices_.size(); i++) {
      auto device = this->devices_[i];
      auto device_serial = device->get_serial_number();
      if (device->get_id() == id && (device_serial == 0 || device_serial == serial)) {
        device->set_serial_number(serial);
        this->next_device_ = i;
        ESP_LOGD(TAG, "Device found: address=%u, id=%02X, serial=%08lX", i + 1, id, serial);
        device->set_serial_number(serial);
        this->send_enrol_message_();
        break;
      }
    }
  }
}

bool SicomComponent::confirm_enrolment_(const std::vector<uint8_t> &data) {
  if (data.size() == 10 && data[3] == DEVICE_ACK) {
    auto buffer = ByteBuffer::wrap(data, LITTLE);
    auto id = buffer.get_uint8(2);
    auto serial = buffer.get_uint32(4);
    auto device = this->devices_[this->next_device_];
    if (device->get_id() == id && device->get_serial_number() == serial) {
      device->set_state(ENROLLED);
      return true;
    }
  }
  return false;
}

void SicomComponent::send_poll_() {
  if (this->next_device_ >= this->devices_.size())
    return;
  auto device = this->devices_[this->next_device_];
  if (device->is_enrolled()) {
    this->send_message_(this->next_device_ + 1, REQUEST_DATA_MSG);
  }
}

bool SicomComponent::process_data_(std::vector<uint8_t> &buffer) {
  if (buffer.empty())
    return false;
  auto index = buffer[0] - 1;
  if (index >= this->devices_.size())
    return false;
  if (buffer[CMD_OFFS] == REPLY_DATA_MSG) {
    this->devices_[index]->decode(buffer);
    return true;
  }
  ESP_LOGD(TAG, "Unknown cmd: %02X for device %d", buffer[CMD_OFFS], this->next_device_ + 1);
  return false;
}

void SicomComponent::update() {
  auto elapsed = millis() - this->last_all_call_;
  switch (this->state_) {
    case STATE_ALL_CALL:
      this->last_all_call_ = millis();
      this->send_message_(BROADCAST_ADDRESS, ALL_CALL_MSG);
      this->state_ = STATE_CALLING;
      return;

    case STATE_CALLING: {
      auto data = this->read_message_();
      if (data.empty()) {
        this->state_ = STATE_POLL_START;
        this->next_device_ = 0;
        return;
      }
      if (this->confirm_enrolment_(data)) {
      }
      this->enrol_device_(data);
      this->state_ = STATE_ENROLLING;
      return;
    }

    case STATE_ENROLLING:
    case STATE_ENROLLING_2: {
      for (;;) {
        auto data = this->read_message_();
        if (!data.empty() && data[0] != BROADCAST_ADDRESS) {
          this->process_data_(data);
          continue;
        }
        if (this->confirm_enrolment_(data) || this->state_ == STATE_ENROLLING_2) {
          this->state_ = STATE_POLL_START;
          this->next_device_ = 0;
          return;
        }
        break;
      }
      this->state_ = STATE_ENROLLING_2;
      this->send_enrol_message_();
    }

    case STATE_POLL_START:
      this->state_ = STATE_POLLING;
      this->next_device_ = 0;
      this->send_poll_();
      return;

    case STATE_POLLING:
      break;
  }
  // restart the state sequence once per second
  if (elapsed > 1000) {
    this->state_ = STATE_ALL_CALL;
    return;
  }
  if (this->next_device_ < this->devices_.size()) {
    // any more devices to poll? Wrap around if
    auto device = this->devices_[this->next_device_];
    if (device->is_enrolled()) {
      auto data = this->read_message_();
      if (!this->process_data_(data))
        device->invalidate();
    }
  }
  if (++this->next_device_ == MAX_DEVICES)
    this->next_device_ = 0;
  this->send_poll_();
}

void SicomDevice::decode(std::vector<uint8_t> &data) {
  auto buffer = ByteBuffer::wrap(data, BIG);
  for (auto *sensor : this->sensors_) {
    sensor->decode(buffer);
  }
  if (this->status_sensor_ != nullptr)
    this->status_sensor_->publish_state(true);
  this->last_data_time_ = millis();
}

/**
 *
 * Send a message. The message is prefixed with the address and the length of the message,
 * and suffixed with a CRC16 checksum.
 *
 * @param address The address of the device to send the message to.
 * @param data The data to send. Passed by value to allow modification.
 */
void SicomComponent::send_message_(uint8_t address, std::vector<uint8_t> data) {
  data.insert(data.begin(), data.size() + 1);
  data.insert(data.begin(), address);
  add_crc(data);
  // encode the message as RMT symbols
  std::vector<rmt_symbol_word_t> symbols;
  symbols.reserve(data.size() * 6 + 16);  // Reserve space for the symbols
                                          // Lead-in of 4 high bits
  symbols.push_back({.duration0 = SYMBOL_LENGTH, .level0 = 1, .duration1 = SYMBOL_LENGTH, .level1 = 1});
  // Encode each byte in the data vector
  bool first = true;
  for (uint8_t byte : data) {
    // One high state plus start bit
    symbols.push_back({.duration0 = SYMBOL_LENGTH, .level0 = 1, .duration1 = SYMBOL_LENGTH, .level1 = 0});
    // 8 bits of data in 4 symbols
    for (int i = 0; i != 8; i += 2) {
      uint8_t bits = byte >> i;
      symbols.push_back({.duration0 = SYMBOL_LENGTH,
                         .level0 = (bits & 0x01) != 0,
                         .duration1 = SYMBOL_LENGTH,
                         .level1 = (bits & 0x02) != 0});
    }
    // 9th bit (low) plus stop bit (high)
    symbols.push_back({.duration0 = SYMBOL_LENGTH, .level0 = first, .duration1 = SYMBOL_LENGTH, .level1 = 1});
    first = false;
  }
  if (this->tx_enable_pin_ != nullptr) {
    this->tx_enable_pin_->digital_write(true);
  }
  // flush the input buffer
  auto buffered = this->available();
  if (buffered != 0) {
    ESP_LOGD(TAG, "Flushing %d bytes from input buffer", buffered);
    uint8_t byte;
    while (buffered--)
      this->read_byte(&byte);
  }
  auto err = rmt_transmit(this->channel_, this->encoder_, symbols.data(), symbols.size() * sizeof(rmt_symbol_word_t),
                          &this->transmit_config_);
  rmt_tx_wait_all_done(this->channel_, 10);
  if (this->tx_enable_pin_ != nullptr) {
    this->tx_enable_pin_->digital_write(false);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to transmit message, err=%s", esp_err_to_name(err));
  } else if (this->debug_) {
    ESP_LOGD(TAG, "Sent message: %s", format_hex_pretty(data).c_str());
  }
}

void SicomComponent::send_message_(uint8_t address, uint8_t cmd) {
  std::vector<uint8_t> message{};
  message.push_back(cmd);
  this->send_message_(address, message);
}

void SicomComponent::dump_config() { ESP_LOGCONFIG(TAG, "Sicom"); }

}  // namespace sicom
}  // namespace esphome
