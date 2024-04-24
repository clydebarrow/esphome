#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/time/real_time_clock.h"

namespace esphome {
namespace sx1262 {

static const char *const TAG = "sx1262";
static const uint8_t CLR_IRQ_STATUS = 0x02;
static const uint8_t CLR_ERRS = 0x07;
static const uint8_t SET_IRQ = 0x08;
static const uint8_t WRITE_REG = 0x0D;
static const uint8_t GET_IRQ_STATUS = 0x12;
static const uint8_t GET_RX_BUFFER = 0x13;
static const uint8_t GET_PKT_STATUS = 0x14;
static const uint8_t GET_ERRS = 0x17;
static const uint8_t READ_REG = 0x1D;
static const uint8_t READ_BUFFER = 0x1E;
static const uint8_t STANDBY = 0x80;
static const uint8_t START_RX = 0x82;
static const uint8_t SET_FREQ = 0x86;
static const uint8_t SET_PKTTYPE = 0x8A;
static const uint8_t SET_MODUL = 0x8B;
static const uint8_t SET_PKTPARAMS = 0x8C;
static const uint8_t SET_TX = 0x8E;
static const uint8_t SET_BUFFER_BASE = 0x8F;
static const uint8_t SET_FALLBACK = 0x93;
static const uint8_t SET_REG_MODE = 0x96;
static const uint8_t SET_TXCO = 0x97;
static const uint8_t SET_ANTCTRL = 0x9D;
static const uint8_t SET_TIMER = 0x9F;
static const uint8_t SET_PA = 0x95;
static const uint8_t READ_STATUS = 0xC0;

static const uint8_t PKT_RXED = 0x02;

// 32MHz crystal, 100kHz bit rate
static const uint32_t BIT_RATE = 100e3;
static const uint32_t XTAL_FREQ = 32000000;
static const uint32_t BIT_RATE_FACTOR = 32ULL * XTAL_FREQ / BIT_RATE;
static const uint32_t FREQ_DEV = (50000ULL * (1UL << 25)) / XTAL_FREQ;
static const uint32_t BASE_FREQ = 917000000;
static const uint32_t CHAN_SEPR = 400000;
static const size_t N_CHANNELS = 24;

static const size_t PKT_LEN = 24;                     // bytes in data packet
static const size_t ENCODED_LEN = (PKT_LEN + 2) * 2;  // length with CRC manchester encoded.

static const char *chip_modes[] = {"Unused", "RFU", "STBY_RC", "STBY_XOSC", "FS", "RX", "TX"};

typedef enum {
  STANDBY_RC,
  STANDBY_XTAL,
  RX_MODE,
  TX_MODE,
  NO_MODE,
} rf_mode_t;

typedef struct {
  /********************/
  unsigned int addr : 24;
  unsigned int protocol : 4;
  unsigned int addr_type : 3;
  unsigned int zero1 : 1;
  // unsigned int magic:8;
  /********************/
  int vs : 10;
  // unsigned int onGround:1;
  // unsigned int _unk2:1;
  // unsigned int airborne:1;
  unsigned int turnrate : 3;  // 1 (plane on ground), 5 (no/slow turn), 4 (right turn >14deg), 7 (left turn >14deg)
  unsigned int stealth : 1;
  unsigned int no_track : 1;
  unsigned int parity : 1;
  unsigned int gps : 12;
  unsigned int aircraft_type : 4;
  /********************/
  unsigned int lat : 19;
  unsigned int alt : 13;
  /********************/
  unsigned int lon : 20;
  unsigned int zero2 : 10;
  unsigned int smult : 2;
  /********************/
  int8_t ns[4];
  int8_t ew[4];
  /********************/
} __attribute__((packed)) flarm_packet_t;

class SX1262Component : public PollingComponent,
                        public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_HIGH,
                                              spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void update() override;

  float get_setup_priority() const override;
  void set_busy_pin(GPIOPin *busy_pin) { this->busy_pin_ = busy_pin; }
  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_interrupt_pin(GPIOPin *interrupt_pin) { this->interrupt_pin_ = interrupt_pin; }
  void set_clock(time::RealTimeClock *clock) { this->clock_ = clock; }

 protected:
  bool busy_wait_(bool enable = false) {
    auto now = millis();
    while (this->busy_pin_->digital_read()) {
      if (millis() - now > 400) {
        esph_log_e(TAG, "Busy wait timed out");
        return false;
      }
    }
    if (enable)
      this->enable();
    return true;
  }

  uint8_t read_registers_(uint16_t address, uint8_t *data, size_t len) {
    uint8_t buf[4];
    buf[0] = READ_REG;
    buf[1] = address >> 8;
    buf[2] = address & 0xFF;
    buf[3] = 0;  // Will receive status

    if (!this->busy_wait_(true))
      return 0;
    this->transfer_array(buf, sizeof buf);
    this->read_array(data, len);
    this->disable();
    esph_log_v(TAG, "Read registers at 0x%03X, status 0x%02X", address, buf[3]);
    return buf[3];
  }

  // send a command, optionally read data
  bool command_write_(const uint8_t *data, size_t len) {
    esph_log_v(TAG, "Command write %X", data[0]);
    if (!this->busy_wait_(true))
      return false;
    this->write_array(data, len);
    this->disable();
    // get_status_();
    return true;
  }

  void clear_irq_status() {
    static const uint8_t buf[]{CLR_IRQ_STATUS, 3, 0xFF};
    this->command_write_(buf, sizeof buf);
  }

  // send a command and read data.
  uint8_t command_read_(const uint8_t cmd, uint8_t *data, size_t len) {
    if (!this->busy_wait_(true))
      return 0;
    this->write_array(&cmd, 1);
    uint8_t status = this->read_byte();
    if (len != 0)
      this->read_array(data, len);
    this->disable();
    esph_log_v(TAG, "Read command 0x%02X, status 0x%02X", cmd, status);
    return status;
  }

  bool read_buffer_(uint8_t *data, size_t len, uint8_t offset) {
    if (!this->busy_wait_(true))
      return false;
    const uint8_t buf[]{READ_BUFFER, offset, 0};
    this->write_array(buf, sizeof buf);
    this->read_array(data, len);
    this->disable();
    return true;
  }

  uint8_t get_status_() {
    auto status = this->command_read_(READ_STATUS, nullptr, 0);
    switch ((status & 0xF) >> 1) {
      case 0:
      case 1:
        break;
      case 2:
        esph_log_d(TAG, "Data available");
        break;
      case 3:
        esph_log_d(TAG, "Command timeout");
        break;
      case 4:
        esph_log_d(TAG, "Command processing error");
        break;
      case 5:
        esph_log_d(TAG, "Command execute failure");
        break;
      case 6:
        esph_log_d(TAG, "TX done");
        break;
    }

    esph_log_d(TAG, "Chip mode: %s", chip_modes[(status >> 4) & 7]);
    return status;
  }

  void write_registers_(uint16_t address, const uint8_t *data, size_t len) {
    uint8_t buf[]{WRITE_REG, uint8_t(address >> 8), (uint8_t) address};
    this->busy_wait_(true);
    this->write_array(buf, sizeof buf);
    this->write_array(data, len);
    this->disable();
  }

  void start_rx_() {
    static const uint8_t buf[]{START_RX, 0xFF, 0xFF, 0xFF};
    if (this->command_write_(buf, sizeof buf))
      this->current_mode_ = RX_MODE;
  }

  void standby_(rf_mode_t type = STANDBY_XTAL) {
    if (this->current_mode_ == type)
      return;
    const uint8_t buf[]{STANDBY, type == STANDBY_RC ? (uint8_t) 0 : (uint8_t) 1};
    if (this->command_write_(buf, sizeof buf))
      this->current_mode_ = type;
  }

  // set frequency in Hz.
  void set_freq_(uint32_t freq) {
    uint8_t buf[]{SET_FREQ, 0, 0, 0, 0};
    // Assume 64 bit int arithmetic not available.
    auto q = (uint32_t) ((1UL << 25) * (double) freq / 32000000.0);

    esph_log_v(TAG, "Freq %.1fMHz, PLL %lX", freq / 1000000.0, (unsigned long) q);

    buf[1] = q >> 24;
    buf[2] = q >> 16;
    buf[3] = q >> 8;
    buf[4] = q;
    this->command_write_(buf, sizeof buf);
    this->busy_wait_();
    this->current_freq_ = freq;
  }

  GPIOPin *reset_pin_{nullptr};
  GPIOPin *interrupt_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
  time::RealTimeClock *clock_{};
  uint32_t current_freq_{};
  rf_mode_t current_mode_{NO_MODE};
  void process_rx_(const uint8_t *buf);
};

}  // namespace sx1262
}  // namespace esphome
