#include "lps25hb.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace lps25hb {

static const char *const TAG = "lps25hb.sensor";
// register addresses
static const uint8_t REF_P_XL = 0x08;
static const uint8_t REF_P_L = 0x09;
static const uint8_t REF_P_H = 0x0A;
static const uint8_t WHO_AM_I = 0x0F;
static const uint8_t RES_CONF = 0x10;
static const uint8_t CTRL_REG1 = 0x20;
static const uint8_t CTRL_REG2 = 0x21;
static const uint8_t CTRL_REG3 = 0x22;
static const uint8_t CTRL_REG4 = 0x23;
static const uint8_t INTERRUPT_CFG = 0x24;
static const uint8_t INT_SOURCE = 0x25;
static const uint8_t STATUS_REG = 0x27;
static const uint8_t PRESS_OUT_XL = 0x28;
static const uint8_t PRESS_OUT_L = 0x29;
static const uint8_t PRESS_OUT_H = 0x2A;
static const uint8_t TEMP_OUT_L = 0x2B;
static const uint8_t TEMP_OUT_H = 0x2C;
static const uint8_t FIFO_CTRL = 0x2E;
static const uint8_t FIFO_STATUS = 0x2F;
static const uint8_t THS_P_L = 0x30;
static const uint8_t THS_P_H = 0x31;
static const uint8_t RPDS_L = 0x39;
static const uint8_t RPDS_H = 0x3A;

void LPS25HBComponent::update() {
  ESP_LOGV(TAG, "Update");
  this->read_pressure_();
}

void LPS25HBComponent::setup() {
  ESP_LOGD(TAG, "Setting up LPS25HB...");
  this->spi_setup();
  this->write_register(CTRL_REG1, 0x80);  // bring out of power-down mode
  uint8_t const id = this->read_register(WHO_AM_I);
  if (id != 0xBD) {
    ESP_LOGCONFIG(TAG, "Failed ID - got %X", id);
    this->mark_failed();
    return;
  }
  ESP_LOGCONFIG(TAG, "LPS25HB started!");
}

void LPS25HBComponent::dump_config() {
  LOG_SENSOR("  ", "LPS25HB", this);
  LOG_PIN("  CS pin: ", this->cs_);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Connection with LPS25HB failed!");
  }
  LOG_UPDATE_INTERVAL(this);
}

void LPS25HBComponent::read_pressure_() {
  this->write_register(CTRL_REG2, 0x09);  // One shot conversion
  uint8_t buffer[4];
  buffer[0] = PRESS_OUT_XL | 0xC0;  // increment read
  this->enable();
  this->transfer_array(buffer, sizeof buffer);
  this->disable();
  uint32_t p = buffer[1] + (buffer[2] << 8) + (buffer[3] << 16);
  if (p & (1UL << 23))
    p |= ~0UL << 24;      // sign extend
  float const pressure = p / 4096.0f;
  ESP_LOGV(TAG, "Got raw pressure=%d, converted %.1f", p, pressure);
  buffer[0] = TEMP_OUT_L | 0xC0;
  this->enable();
  this->transfer_array(buffer, 3);
  this->disable();
  int16_t const t = buffer[1] + (buffer[2] << 8);
  float const temperature = t / 480.0f;
  ESP_LOGV(TAG, "Got raw temperature=%d, converted %.1f", t, temperature);
  this->publish_state(pressure);
  this->status_clear_warning();
}

float LPS25HBComponent::get_setup_priority() const { return setup_priority::DATA; }

void LPS25HBComponent::write_register(uint8_t address, uint8_t data) {
  uint8_t buffer[2];
  buffer[0] = address;
  buffer[1] = data;
  this->enable();
  this->transfer_array(buffer, sizeof buffer);
  this->disable();
  ESP_LOGV(TAG, "write_register(%X) data 0x%02X", address, data);
}

uint8_t LPS25HBComponent::read_register(uint8_t address) {
  uint8_t buffer[2];
  buffer[0] = address | 0x80;
  buffer[1] = 0;
  this->enable();
  this->transfer_array(buffer, sizeof buffer);
  this->disable();
  ESP_LOGV(TAG, "read_register(%X) returns 0x%02X", address, buffer[1]);
  return buffer[1];
}

}  // namespace lps25hb
}  // namespace esphome
