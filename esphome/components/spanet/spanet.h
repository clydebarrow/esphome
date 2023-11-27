#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace spanet {

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

  void dump_config() override { this->check_uart_settings(38400, 1, uart::UART_CONFIG_PARITY_NONE, 8); }

  void decode_r5(size_t fieldcnt, const char *fields[], size_t field_lengths[]) {
    if (fieldcnt >= 16) {
      float temperature = str_to_int(fields[16], field_lengths[16]) / 10.0f;
      esph_log_d(TAG, "Read temperature %f", temperature);
      if (this->temperature_sensor_ != nullptr)
        this->temperature_sensor_->publish_state(temperature);
    }
  }

  void decode_r6(size_t fieldcnt, const char *fields[], size_t field_lengths[]) {
    if (fieldcnt >= 9) {
      float temperature = str_to_int(fields[9], field_lengths[9]) / 10.0f;
      esph_log_d(TAG, "Read target temperature %f", temperature);
      if (this->target_temperature_number_ != nullptr)
        this->target_temperature_number_->publish_state(temperature);
    }
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
    switch (fields[1][1]) {
      case '5':
        this->decode_r5(fieldcnt, fields, field_lengths);
        break;
      case '6':
        this->decode_r6(fieldcnt, fields, field_lengths);
        break;
    }
  }

  void receive_data() {
    uint8_t data;

    while (this->available() > 0) {
      if (this->read_byte(&data)) {
        if (data == '\r') {
          this->process_buffer_();
          this->rxcnt_ = 0;
        } else if (data >= ' ' && data <= '~' && this->rxcnt_ != BUF_LEN - 1) {
          this->rxbuf_[this->rxcnt_++] = (char) data;
        } else {
          this->rxcnt_ = 0;
        }
      }
    }
  }

  char rxbuf_[BUF_LEN];  // uart line buffer
  size_t rxcnt_{};

  // sensors
  sensor::Sensor *temperature_sensor_{};
  number::Number *target_temperature_number_{};

 public:
  float get_setup_priority() const override { return setup_priority::DATA; };
  void update() override { this->write_array(REQUEST, sizeof REQUEST); }
  void setup() override {}
  void loop() override { this->receive_data(); }

  void set_temperature_sensor(sensor::Sensor *sens) { this->temperature_sensor_ = sens; }
  void set_target_temperature(number::Number *target) { this->target_temperature_number_ = target; }
};

class SpanetNumber : public number::Number, public Parented<spanet::Spanet> {
 public:
  void set_command(const char *command) { this->command_ = command; }
  void set_scale(float scale) { this->scale_ = scale; }

 protected:
  void control(float value) override {
    char buf[40];
    auto len = snprintf(buf, sizeof buf, "%s:%d\n", this->command_, (int) (value / this->scale_));
    this->parent_->write_array((uint8_t *) buf, len);
  }

  const char *command_{};
  float scale_{1.0f};
};

}  // namespace spanet
}  // namespace esphome
