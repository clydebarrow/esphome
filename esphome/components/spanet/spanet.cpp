#include "spanet.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace spanet {

static const char *const TAG = "spanet";
static const uint8_t REQUEST[3] = {'R', 'F', '\n'};

void Spanet::setup() {}

void Spanet::dump_config() { this->check_uart_settings(38400, 1, uart::UART_CONFIG_PARITY_NONE, 8); }

void Spanet::update() { this->write_array(REQUEST, sizeof REQUEST); }

void Spanet::loop() {
  uint8_t data;

  while (this->available() > 0) {
    if (this->read_byte(&data)) {
      if (data == '\r') {
        this->process_buffer_();
        this->rxcnt_ = 0;
      } else if (data > ' ' && data <= '~' && this->rxcnt_ != BUF_LEN - 1) {
        this->rxbuf_[this->rxcnt_++] = (char)data;
      } else {
        this->rxcnt_ = 0;
      }
    }
  }
}

void Spanet::process_buffer_() {
  this->fieldcnt_ = 0;
  size_t buf_idx = 0;

  while (this->fieldcnt_ != MAX_FIELDS) {
    this->fields_[this->fieldcnt_++] = this->rxbuf_ + buf_idx;
    while (buf_idx != this->rxcnt_ && this->rxbuf_[buf_idx] != ',')
      buf_idx++;
    this->rxbuf_[buf_idx] = 0;  // safe, since rxcnt_ is always less than BUF_LEN.
    if (this->rxcnt_++ == buf_idx)
      break;
  }
  size_t field_idx = 0;
  while (field_idx != this->fieldcnt_) {
    const char * ptr = this->fields_[field_idx++];
    if (*ptr == 0)
      continue;
  }

  // the following would ideally be split out into separate functions, but that's a pain to do
  // in C++11.



}

}  // namespace spanet
}  // namespace esphome
