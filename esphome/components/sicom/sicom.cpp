#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "sicom.h"

namespace esphome {
namespace sicom {

static const char *TAG = "sicom";
static const uint32_t BAUD_RATE = 115200;
static const uint16_t SYMBOL_LENGTH = 16;

static float decode_voltage(ByteBuffer &data, size_t offset) { return (data.get<int16_t>(offset) * 0.001f); }
static float decode_resistance(ByteBuffer &data, size_t offset) { return data.get<uint16_t>(offset); }
static float decode_current(ByteBuffer &data, size_t offset) { return data.get<uint32_t>(offset) * 0.01f; }
static float not_set = nanf("");
static void unset(std::vector<float> &vec) { std::fill(vec.begin(), vec.end(), not_set); }

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

static void add_crc(std::vector<uint8_t> &data) {
  data[1] = data.size() + 1;
  auto crc = calculateCRC16(data);
  data.push_back(crc >> 8);
  data.push_back(crc & 0xFF);
}

static const uint8_t BROADCAST_ADDRESS = 0xFA;
static const uint8_t REQUEST_DATA_MSG = 0x0A;
static const uint8_t REPLY_DATA_MSG = 0xA0;
static const uint8_t ALL_CALL_MSG = 0x31;
static const uint8_t ENTROL_MSG = 0x20;
static const uint8_t DEVICE_HELLO = 0xC0;
static const uint8_t DEVICE_ACK = 0xC1;
static const uint8_t MAX_ADDRESS = 32;

static const size_t MAX_MSG_LEN = 64;
static const size_t MIN_MSG_LEN = 5;
static const size_t LEN_OFFS = 1;

bool SicomSensor::decode(ByteBuffer &buffer) {
  if (buffer.get_limit() < this->offset_ + this->length_) {
    return false;
  }
  float value = not_set;
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
  this->state_ = UNSEEN;
  for (auto &sensor : this->sensors_) {
    sensor->invalidate();
  }
}
void SicomDevice::update() {
  size_t i = 0;
  for (auto &sensor : this->voltage_sensors_) {
    if (sensor)
      sensor->publish_state(this->get_voltage(i++));
  }
  i = 0;
  for (auto &sensor : this->resistance_sensors_) {
    if (sensor)
      sensor->publish_state(this->get_resistance(i++));
  }
  i = 0;
  for (auto &sensor : this->current_sensors_) {
    if (sensor)
      sensor->publish_state(this->get_current(i++));
  }
  unset(this->voltages_);
  unset(this->resistances_);
  unset(this->currents_);
}

float SicomDevice::get_voltage(size_t index) const {
  if (index >= this->voltages_.size()) {
    return not_set;
  }
  return voltages_[index];
}
float SicomDevice::get_resistance(size_t index) const {
  if (index >= this->resistances_.size()) {
    return not_set;
  }
  return resistances_[index];
}
float SicomDevice::get_current(size_t index) const {
  if (index >= this->resistances_.size()) {
    return not_set;
  }
  return currents_[index];
}
bool SicomDevice::get_relay(size_t index) {
  if (index >= this->resistances_.size()) {
    return false;
  }
  return relays_[index];
}
// 01:2C:02:A0:00:00:00:00:00:00:00:00:00:00:00:12:00:00:00:12:00:00:00:12:00:00:00:12:FF:ED:FF:EC:FF:EC:FF:FF:FF:FF:FF:FF:FF:FF:00:A4:6C
bool SicomSCQ25TDevice::decode(ByteBuffer &data) {
  if (data.get_limit() != 45)
    return false;
  for (size_t i = 0; i != this->voltages_.size(); i++) {
    this->voltages_[i] = decode_voltage(data, 28 + i * 2);
  }
  for (size_t i = 0; i != this->currents_.size(); i++) {
    this->currents_[i] = data.get<int16_t>(4 + i * 2) * .01f;
  }
  for (size_t i = 0; i != this->resistances_.size(); i++) {
    this->resistances_[i] = decode_resistance(data, 34 + i * 2);
  }
  this->state_ = TALKING;
  return true;
}

//[16:15:25][I][sicom:095]: Received Slave message: 01.14.03.A0.00.25.0A.28.FF.FF.25.EE.FF.FF.FF.FF.FF.FF.00.E9.0A (21)
bool SicomST107Device::decode(ByteBuffer &data) {
  if (data.get_limit() != 21)
    return false;
  for (size_t i = 0; i != this->voltages_.size(); i++) {
    this->voltages_[i] = decode_voltage(data, 4 + i * 2);
  }
  for (size_t i = 0; i != this->resistances_.size(); i++) {
    this->resistances_[i] = decode_resistance(data, 10 + i * 2);
  }
  this->relays_[0] = data.get_uint8(18);
  this->state_ = TALKING;
  return true;
}

//  [16:15:44][I][sicom:095]: Received Slave message:
//  02.17.0E.A0.FF.FF.FF.FC.04.E8.3C.7C.00.08.66.00.0C.9C.28.67.28.67.1A.96 (24)
bool SicomSC301Device::decode(ByteBuffer &data) {
  if (data.get_limit() != 24)
    return false;
  this->currents_[0] = decode_current(data, 4);
  this->voltages_[0] = decode_voltage(data, 13);
  this->voltages_[1] = decode_voltage(data, 16);
  this->state_ = TALKING;
  return true;
}

// [16:15:45][I][sicom:095]: Received Slave message:
// 01.19.10.A0.00.00.01.26.00.13.5C.4A.00.00.00.00.34.4D.25.4C.FF.FF.44.CA.66.27 (26)
bool SicomSC303Device::decode(ByteBuffer &data) {
  if (data.get_limit() != 26)
    return false;
  for (size_t i = 0; i != this->resistances_.size(); i++) {
    this->resistances_[i] = decode_resistance(data, 18 + i * 2);
  }
  this->currents_[0] = decode_current(data, 4);
  this->voltages_[0] = decode_voltage(data, 14);
  this->voltages_[1] = decode_voltage(data, 16);
  this->state_ = TALKING;
  return true;
}

void SicomComponent::loop() {}

static void sicomTask(void *arg) {
  SicomComponent *sicom = (SicomComponent *) arg;
  sicom->run_task();
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

// Try to read a message, starting with the given address
// If the address is not found, return an empty vector
// This should be called after a delay to allow the data to have been buffered
std::vector<uint8_t> SicomComponent::read_msg(uint8_t address) {
  uint8_t byte = 0xFF;
  std::vector<uint8_t> buffer{};
  for (;;) {
    if (!this->read_byte(&byte))
      return {};
    if (byte == address) {
      break;
    }
    // get the message length
    if (!this->read_byte(&byte))
      return {};
    if (byte >= MIN_MSG_LEN && byte <= MAX_MSG_LEN)
      break;
  }
  buffer.push_back(address);
  buffer.push_back(byte);
  auto len = byte;
  while (--len != 0) {
    if (!this->read_byte(&byte))
      return {};
    buffer.push_back(byte);
  }
  ESP_LOGD(TAG, "Received message: %s", format_hex_pretty(buffer).c_str());
  if (calculateCRC16(buffer) == 0)
    return buffer;
  return {};
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
          ESP_LOGD(TAG, "Device found: address=%u, id=%02X, serial=%08lX", i + 1, id, serial);
          device->set_serial_number(serial);
          ByteBuffer message = ByteBuffer(6, BIG);
          message.put_uint8(ENTROL_MSG);
          message.put_uint32(serial);
          message.put_uint8(i + 1);
          this->send_message_(BROADCAST_ADDRESS, message.get_data());
}
void SicomComponent::update() {
  switch (this->state_) {
    case STATE_ALL_CALL:
      this->send_message_(BROADCAST_ADDRESS, ALL_CALL_MSG);
      this->state_ = STATE_CALLING;
      return;

    case STATE_CALLING: {
      auto data = this->read_msg(BROADCAST_ADDRESS);
      if (data.empty()) {
        this->state_ = STATE_POLLING;
        return;
      }
      this->enrol_device_(data);

      break;
    }


    default:;
  }

  this->last_all_call_ = millis();
  for (;;) {
    int32_t remaining = this->last_all_call_ + 1000 - millis();
    ESP_LOGD(TAG, "Delaying for %d ms", remaining);
    if (remaining > 0)
      delay(remaining);
    this->send_message_(BROADCAST_ADDRESS, ALL_CALL_MSG);
    this->last_all_call_ = millis();
    auto data = this->read_msg(BROADCAST_ADDRESS);
    // process a device HELLO message
          data = this->read_msg(i + 1);
          if (data.empty()) {
            this->send_message_(BROADCAST_ADDRESS, message.get_data());
            data = this->read_msg(i + 1);
          }
          ESP_LOGD(TAG, "Received data message: %s", format_hex_pretty(data).c_str());
          if (data.size() == 10 && data[3] == DEVICE_ACK) {
            device->set_state(ENROLLED);
          }
          break;
        }
        i++;
      }
    }
    for (auto i = 0; i != this->devices_.size(); i++) {
      auto device = this->devices_[i];
      if (device->get_state() == ENROLLED) {
        this->send_message_(i + 1, REQUEST_DATA_MSG);
        auto dev_data = this->read_msg(i + 1);
        if (dev_data.size() != 0) {
          ESP_LOGD(TAG, "Received data message: %s", format_hex_pretty(dev_data).c_str());
          xQueueSend(this->rx_queue_, &dev_data, portMAX_DELAY);
        }
      }
      delay(20);
    }
  }
}

void SicomComponent::process_data_(ByteBuffer &buffer) {
  ESP_LOGV(TAG, "Received data message: %s", format_hex_pretty(buffer.get_data()).c_str());
  uint8_t address = buffer.get_uint8(0);
  uint8_t id = buffer.get_uint8(2);
  uint8_t cmd = buffer.get_uint8(3);
  if (cmd == REPLY_DATA_MSG && address > 0 && address <= this->devices_.size()) {
    auto device = this->devices_[address - 1];
    // [14:00:39][D][sicom:296]: Received: 03.09.0E.C1.B4.6D.65.24.1E.43 (10)
    ESP_LOGV(TAG, "data message: id=%02X, address=%02X, data=%s", id, address,
             format_hex_pretty(buffer.get_data()).c_str());
    device->decode(buffer);
    return;
  }
  ESP_LOGD(TAG, "No device found for message: id=%02X, address=%02X, data=%s", id, address,
           format_hex_pretty(buffer.get_data()).c_str());
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
  auto err = rmt_transmit(this->channel_, this->encoder_, symbols.data(), symbols.size() * sizeof(rmt_symbol_word_t),
                          &this->transmit_config_);
  rmt_tx_wait_all_done(this->channel_, 10);
  if (this->tx_enable_pin_ != nullptr) {
    this->tx_enable_pin_->digital_write(false);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to transmit message, err=%s", esp_err_to_name(err));
  } else if (true || this->debug_) {
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
