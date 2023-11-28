#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace spanet {

class SpaValue {
 public:
  SpaValue(char row, size_t col) : row{row}, col{col} {}
  const char row;
  const size_t col;
  virtual void set_value(int value) {}
};

class Spanet : public PollingComponent, public uart::UARTDevice {
 protected:
  constexpr static const char *const TAG = "spanet";
  constexpr static const uint8_t REQUEST[3] = {'R', 'F', '\n'};
  static const size_t BUF_LEN = 512;    // length of rx buffer
  static const size_t MAX_FIELDS = 40;  // maximum fields to decode

  static int32_t str_to_int(const char *buf, size_t len) {
    bool negative = false;
    int32_t value = 0;
    if (len == 0)
      return 0;
    if (*buf == '+') {
      buf++;
      len--;
    } else if (*buf == '-') {
      negative = true;
      buf++;
      len--;
    }
    while (len-- != 0 && isdigit(*buf)) {
      value = value * 10 + *buf++ - '0';
    }
    if (negative)
      return -value;
    return value;
  }

  void process_buffer_() {
    if (this->rxcnt_ == 3)  // ignore RF: line
      return;
    const char *fields[MAX_FIELDS];    // stores extracted fields
    size_t field_lengths[MAX_FIELDS];  // stores extracted fields
    size_t fieldcnt = 0;
    size_t buf_idx = 0;

    while (fieldcnt != MAX_FIELDS) {
      const size_t field_start = buf_idx;
      fields[fieldcnt] = this->rxbuf_ + field_start;
      while (buf_idx != this->rxcnt_ && this->rxbuf_[buf_idx] != ',')
        buf_idx++;
      field_lengths[fieldcnt++] = buf_idx - field_start;
      if (this->rxcnt_ == buf_idx)
        break;
      buf_idx++;  // skip trailing comma
    }

    if (fieldcnt < 2 || field_lengths[1] < 2 || fields[1][0] != 'R') {
      esph_log_d(TAG, "Invalid line received: %.*s", this->rxcnt_, this->rxbuf_);
      return;
    }
    esph_log_v(TAG, "Processing %.*s", this->rxcnt_, this->rxbuf_);
    auto row = fields[1][1];
    for (auto val : values_) {
      if (val->row == row && fieldcnt > val->col) {
        val->set_value(str_to_int(fields[val->col], field_lengths[val->col]));
      }
    }
  }

  void receive_data_() {
    uint8_t data;

    while (this->available() > 0) {
      if (this->read_byte(&data)) {
        if (data == '\r') {
          this->process_buffer_();
          this->rxcnt_ = 0;
        } else if (data >= ' ' && data <= '~' && this->rxcnt_ != BUF_LEN) {
          this->rxbuf_[this->rxcnt_++] = (char) data;
        } else {
          this->rxcnt_ = 0;
        }
      }
    }
  }

  char rxbuf_[BUF_LEN];  // uart line buffer
  size_t rxcnt_{};

  std::vector<SpaValue *> values_{};

 public:
  void dump_config() override { this->check_uart_settings(38400, 1, uart::UART_CONFIG_PARITY_NONE, 8); }
  float get_setup_priority() const override { return setup_priority::DATA; };
  void update() override { this->write_array(REQUEST, sizeof REQUEST); }
  void setup() override {}
  void loop() override { this->receive_data_(); }

  void add_value(SpaValue  *value) { this->values_.push_back(value); }
};

class SpanetNumber : public SpaValue, public number::Number, public Parented<spanet::Spanet> {
 public:
  SpanetNumber(char row, size_t col, const char * cmd, float scale): SpaValue(row, col), cmd_{cmd}, scale_{scale} {}
  void set_value(int value) override {
    this->publish_state(value * this->scale_);
  }

 protected:
  void control(float value) override {
    char buf[40];
    auto len = snprintf(buf, sizeof buf, "%s:%d\n", this->cmd_, (int) (value / this->scale_));
    this->parent_->write_array((uint8_t *) buf, len);
    this->publish_state(value);
    this->parent_->update();
  }

  const char * const cmd_;
  const float scale_;
};

class SpanetSwitch : public SpaValue, public switch_::Switch, public Parented<spanet::Spanet> {
 public:
  SpanetSwitch(char row, size_t col, const char * cmd): SpaValue(row, col), cmd_{cmd} {}
  void set_value(int value) override {
    this->publish_state(value != 0);
  }

 protected:
  void write_state(bool state) override {
    char buf[40];
    auto len = snprintf(buf, sizeof buf, "%s:%d\n", this->cmd_, state ? 1 : 0);
    if (len < sizeof buf) {
      this->parent_->write_array((uint8_t *) buf, len);
      this->publish_state(state);
      this->parent_->update();
    }
  }

  const char *cmd_{};
};

class SpanetBinarySensor : public SpaValue, public binary_sensor::BinarySensor, public Parented<spanet::Spanet> {
 public:
  SpanetBinarySensor(char row, size_t col): SpaValue(row, col) {}
  void set_value(int value) override {
    this->publish_state(value != 0);
  }
};

class SpanetSensor : public SpaValue, public sensor::Sensor, public Parented<spanet::Spanet> {
 public:
  SpanetSensor(char row, size_t col, float scale): SpaValue(row, col), scale_{scale} {}
  void set_value(int value) override {
    this->publish_state(value * this->scale_);
  }
 protected:
  const float scale_;
};

}  // namespace spanet
}  // namespace esphome
