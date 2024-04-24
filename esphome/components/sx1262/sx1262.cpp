#include "sx1262.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cinttypes>

namespace esphome {
namespace sx1262 {

static const uint32_t FLARM_KEY1[]{0xe43276df, 0xdca83759, 0x9802b8ac, 0x4675a56b,
                                   0xfc78ea65, 0x804b90ea, 0xb76542cd, 0x329dfa32};
static const uint32_t FLARM_KEY2 = 0x045d9f3b;
static const uint32_t FLARM_KEY3 = 0x87b562f4;

static void flarm_btea(void *inp, int8_t n, const uint32_t key[4]) {
  auto v = (uint32_t *) inp;
  uint32_t y, z, sum;
  uint32_t p, rounds, e;

#define DELTA 0x9e3779b9
// #define ROUNDS (6 + 52 / n)
#define ROUNDS 6
#define MX (((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4)) ^ ((sum ^ y) + (key[(p & 3) ^ e] ^ z)))

  if (n > 1) {
    /* Coding Part */
    rounds = ROUNDS;
    sum = 0;
    z = v[n - 1];
    do {
      sum += DELTA;
      e = (sum >> 2) & 3;
      for (p = 0; p < n - 1; p++) {
        y = v[p + 1];
        z = v[p] += MX;
      }
      y = v[0];
      z = v[n - 1] += MX;
    } while (--rounds);
  } else if (n < -1) {
    /* Decoding Part */
    n = -n;
    rounds = ROUNDS;
    sum = rounds * DELTA;
    y = v[0];
    do {
      e = (sum >> 2) & 3;
      for (p = n - 1; p > 0; p--) {
        z = v[p - 1];
        y = v[p] -= MX;
      }
      z = v[n - 1];
      y = v[0] -= MX;
      sum -= DELTA;
    } while (--rounds);
  }
}

static uint32_t flarm_obscure(uint32_t key, uint32_t seed) {
  uint32_t m1 = seed * (key ^ (key >> 16));
  uint32_t m2 = (seed * (m1 ^ (m1 >> 16)));
  return m2 ^ (m2 >> 16);
}

static void flarm_make_key(uint32_t key[4], uint32_t timestamp, uint32_t address) {
  size_t i, ndx;
  for (i = 0; i != 4; i++) {
    ndx = ((timestamp >> 23) & 1) ? i + 4 : i;
    key[i] = flarm_obscure(FLARM_KEY1[ndx] ^ ((timestamp >> 6) ^ address), FLARM_KEY2) ^ FLARM_KEY3;
  }
}

static size_t flarm_encrypt(uint8_t *flarm_pkt, long timestamp) {
  auto *pkt = (flarm_packet_t *) flarm_pkt;
  uint32_t key[4];
  flarm_make_key(key, timestamp, (pkt->addr << 8) & 0xffffff);
  flarm_btea(pkt + 4, 5, key);

  return (sizeof(flarm_packet_t));
}

size_t flarm_decrypt(void *flarm_pkt, long timestamp) {
  auto *pkt = (flarm_packet_t *) flarm_pkt;
  uint32_t key[4];
  flarm_make_key(key, timestamp, (pkt->addr << 8) & 0xffffff);
  if (pkt->protocol == 2) {
    flarm_btea((uint32_t *) pkt + 2, -4, key);
  } else {
    flarm_btea((uint32_t *) pkt + 1, -5, key);
  }

  return (sizeof(flarm_packet_t));
}

static uint32_t get_freq_(uint32_t timestamp) {
  uint32_t nts = ~timestamp;
  uint32_t ts16 = timestamp * uint32_t(32768) + nts;
  uint32_t v4096 = (ts16 >> 12) ^ ts16;
  uint32_t v5 = uint32_t(5) * v4096;
  uint32_t v16 = (v5 >> uint32_t(4)) ^ v5;
  uint32_t v2057 = uint32_t(2057) * v16;
  uint32_t v9 = (v2057 >> uint32_t(16)) ^ v2057;
  unsigned channel = v9 % N_CHANNELS;
  uint32_t freq = channel * CHAN_SEPR + BASE_FREQ;
  ESP_LOGV(TAG, "%04X,channel %u, %lukHz", timestamp, channel, (unsigned long) freq / 1000);
  return freq;
}

static const uint8_t reg_mode[]{SET_REG_MODE, 0x01};
static const uint8_t pkt_type[]{SET_PKTTYPE, 0x00};  // FSK
static const uint8_t cad_params[]{0x88, 0x03, 0x14, 0x8A, 0, 0, 0, 0};
static const uint8_t ant_ctrl[]{SET_ANTCTRL, 0x01};
static const uint8_t fallback_mode[]{SET_FALLBACK, 0x30};
static const uint8_t timer_ctrl[]{SET_TIMER, 0x00};
static const uint8_t tx_ctrl[]{SET_TX, 0x16, 0x02};
static const uint8_t pa_ctrl[]{SET_PA, 0x04, 0x07, 0x00, 0x01};
static const uint8_t pkt_params[]{SET_PKTPARAMS, 0x00, 24, 4, 56, 0, 0, 52, 1, 0};
static const uint8_t irq_ctrl[]{SET_IRQ, 0, 0x06, 0x0, 0x06, 0, 0, 0, 0};
static const uint8_t buffer_base[]{SET_BUFFER_BASE, 0, 0};
static const uint8_t set_txco[]{SET_TXCO, 7, 8, 0};
static const uint8_t clr_errs[]{CLR_ERRS, 0, 0};
static const uint8_t mod_params[]{
    SET_MODUL, (uint8_t) (BIT_RATE_FACTOR >> 16), (uint8_t) (BIT_RATE_FACTOR >> 8), (uint8_t) BIT_RATE_FACTOR, 0x09,
    0x0B,      (uint8_t) (FREQ_DEV >> 16),        (uint8_t) (FREQ_DEV >> 8),        (uint8_t) FREQ_DEV,
};
static const uint8_t sync_data[]{0x99, 0xA5, 0xA9, 0x55, 0x66, 0x65, 0x96};

static const uint16_t crc_tabccitt[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7, 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD,
    0xE1CE, 0xF1EF, 0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6, 0x9339, 0x8318, 0xB37B, 0xA35A,
    0xD3BD, 0xC39C, 0xF3FF, 0xE3DE, 0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485, 0xA56A, 0xB54B,
    0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D, 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC, 0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861,
    0x2802, 0x3823, 0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B, 0x5AF5, 0x4AD4, 0x7AB7, 0x6A96,
    0x1A71, 0x0A50, 0x3A33, 0x2A12, 0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A, 0x6CA6, 0x7C87,
    0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41, 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70, 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A,
    0x9F59, 0x8F78, 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F, 0x1080, 0x00A1, 0x30C2, 0x20E3,
    0x5004, 0x4025, 0x7046, 0x6067, 0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E, 0x02B1, 0x1290,
    0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256, 0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E,
    0xC71D, 0xD73C, 0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634, 0xD94C, 0xC96D, 0xF90E, 0xE92F,
    0x99C8, 0x89E9, 0xB98A, 0xA9AB, 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3, 0xCB7D, 0xDB5C,
    0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A, 0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9, 0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83,
    0x1CE0, 0x0CC1, 0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8, 0x6E17, 0x7E36, 0x4E55, 0x5E74,
    0x2E93, 0x3EB2, 0x0ED1, 0x1EF0};

static const uint8_t ManchesterDecode[0x100] =  // lookup table for quick Manchester decoding
    {                                           // lower nibble has the data bits and the upper nibble the error pattern
        0xF0, 0xE1, 0xE0, 0xF1, 0xD2, 0xC3, 0xC2, 0xD3, 0xD0, 0xC1, 0xC0, 0xD1, 0xF2, 0xE3, 0xE2, 0xF3, 0xB4, 0xA5,
        0xA4, 0xB5, 0x96, 0x87, 0x86, 0x97, 0x94, 0x85, 0x84, 0x95, 0xB6, 0xA7, 0xA6, 0xB7, 0xB0, 0xA1, 0xA0, 0xB1,
        0x92, 0x83, 0x82, 0x93, 0x90, 0x81, 0x80, 0x91, 0xB2, 0xA3, 0xA2, 0xB3, 0xF4, 0xE5, 0xE4, 0xF5, 0xD6, 0xC7,
        0xC6, 0xD7, 0xD4, 0xC5, 0xC4, 0xD5, 0xF6, 0xE7, 0xE6, 0xF7, 0x78, 0x69, 0x68, 0x79, 0x5A, 0x4B, 0x4A, 0x5B,
        0x58, 0x49, 0x48, 0x59, 0x7A, 0x6B, 0x6A, 0x7B, 0x3C, 0x2D, 0x2C, 0x3D, 0x1E, 0x0F, 0x0E, 0x1F, 0x1C, 0x0D,
        0x0C, 0x1D, 0x3E, 0x2F, 0x2E, 0x3F, 0x38, 0x29, 0x28, 0x39, 0x1A, 0x0B, 0x0A, 0x1B, 0x18, 0x09, 0x08, 0x19,
        0x3A, 0x2B, 0x2A, 0x3B, 0x7C, 0x6D, 0x6C, 0x7D, 0x5E, 0x4F, 0x4E, 0x5F, 0x5C, 0x4D, 0x4C, 0x5D, 0x7E, 0x6F,
        0x6E, 0x7F, 0x70, 0x61, 0x60, 0x71, 0x52, 0x43, 0x42, 0x53, 0x50, 0x41, 0x40, 0x51, 0x72, 0x63, 0x62, 0x73,
        0x34, 0x25, 0x24, 0x35, 0x16, 0x07, 0x06, 0x17, 0x14, 0x05, 0x04, 0x15, 0x36, 0x27, 0x26, 0x37, 0x30, 0x21,
        0x20, 0x31, 0x12, 0x03, 0x02, 0x13, 0x10, 0x01, 0x00, 0x11, 0x32, 0x23, 0x22, 0x33, 0x74, 0x65, 0x64, 0x75,
        0x56, 0x47, 0x46, 0x57, 0x54, 0x45, 0x44, 0x55, 0x76, 0x67, 0x66, 0x77, 0xF8, 0xE9, 0xE8, 0xF9, 0xDA, 0xCB,
        0xCA, 0xDB, 0xD8, 0xC9, 0xC8, 0xD9, 0xFA, 0xEB, 0xEA, 0xFB, 0xBC, 0xAD, 0xAC, 0xBD, 0x9E, 0x8F, 0x8E, 0x9F,
        0x9C, 0x8D, 0x8C, 0x9D, 0xBE, 0xAF, 0xAE, 0xBF, 0xB8, 0xA9, 0xA8, 0xB9, 0x9A, 0x8B, 0x8A, 0x9B, 0x98, 0x89,
        0x88, 0x99, 0xBA, 0xAB, 0xAA, 0xBB, 0xFC, 0xED, 0xEC, 0xFD, 0xDE, 0xCF, 0xCE, 0xDF, 0xDC, 0xCD, 0xCC, 0xDD,
        0xFE, 0xEF, 0xEE, 0xFF};

static uint16_t update_crc_ccitt(uint16_t crc, uint8_t c) {
  uint16_t tmp, short_c;

  short_c = 0x00ff & (unsigned short) c;

  tmp = (crc >> 8) ^ short_c;
  crc = (crc << 8) ^ crc_tabccitt[tmp];
  return crc;

} /* update_crc_ccitt */

static uint16_t update_crc_ccitt(uint16_t crc, const uint8_t *buf, size_t len) {
  while (len-- != 0) {
    crc = update_crc_ccitt(crc, *buf++);
  }
  return crc;
}

void SX1262Component::setup() {
  ESP_LOGD(TAG, "Setting up SX1262Component...");
  this->spi_setup();
  this->reset_pin_->setup();  // OUTPUT
  this->reset_pin_->digital_write(true);
  this->interrupt_pin_->setup();  // Input
  this->busy_pin_->setup();       // Input
  delay(5);
  this->reset_pin_->digital_write(false);
  delay(5);
  this->reset_pin_->digital_write(true);
  delay(10);
  this->get_status_();
  uint8_t data[4]{};
  this->standby_(STANDBY_RC);
  this->get_status_();
  this->read_registers_(0x740, data, 2);
  ESP_LOGD(TAG, "Read %X:%X from LoRa Sync Word", data[0], data[1]);
  if (data[0] != 0x14 || data[1] != 0x24) {
    ESP_LOGE(TAG, "Bad ident data: %02X%02X received", data[0], data[1]);
    this->mark_failed();
  }
  this->read_registers_(0x8AC, data, 1);
  ESP_LOGD(TAG, "Read %X from RX gain", data[0]);

  // Initialize chip
  this->command_write_(pkt_type, sizeof pkt_type);
  this->command_write_(cad_params, sizeof cad_params);  // Should not be needed for GFSK?
  this->command_write_(reg_mode, sizeof reg_mode);
  this->command_write_(mod_params, sizeof mod_params);
  this->command_write_(pkt_params, sizeof pkt_params);
  this->command_write_(buffer_base, sizeof buffer_base);
  this->write_registers_(0x6C0, sync_data, sizeof sync_data);
  this->command_write_(pa_ctrl, sizeof pa_ctrl);
  this->command_write_(tx_ctrl, sizeof tx_ctrl);
  this->command_write_(fallback_mode, sizeof fallback_mode);
  this->clear_irq_status();
  this->command_write_(timer_ctrl, sizeof timer_ctrl);
  this->command_write_(ant_ctrl, sizeof ant_ctrl);
  this->command_write_(set_txco, sizeof set_txco);
  this->command_write_(irq_ctrl, sizeof irq_ctrl);
  this->standby_();
  ESP_LOGCONFIG(TAG, "SX1262Component started!");
}

void SX1262Component::update() {
  if (this->busy_pin_->digital_read())
    return;
  auto now = this->clock_->timestamp_now();
  if (now < 1680691751) {
    // time is invalid
    return;
  }
  uint8_t buf[3];
  this->command_read_(GET_ERRS, buf, 2);
  if (buf[0] != 0 || buf[1] != 0) {
    esph_log_w(TAG, "Errors %02X %02X", buf[0], buf[1]);
    this->command_write_(clr_errs, sizeof clr_errs);
  }

  auto freq = get_freq_(now);
  if (freq != this->current_freq_ || this->current_mode_ != RX_MODE) {
    this->standby_();
    this->set_freq_(freq);
    this->start_rx_();
  }
}
void SX1262Component::loop() {
  if (this->interrupt_pin_->digital_read()) {
    uint8_t rbuf[ENCODED_LEN];
    // this->command_read_(GET_IRQ_STATUS, rbuf, 2);
    // ESP_LOGD(TAG, "IRQ status %X %X", rbuf[0], rbuf[0]);
    this->command_read_(GET_PKT_STATUS, rbuf, 3);
    if (rbuf[0] & PKT_RXED) {
      ESP_LOGV(TAG, "Packet status %X %X %X", rbuf[0], rbuf[1], rbuf[2]);
      this->command_read_(GET_RX_BUFFER, rbuf, 2);
      ESP_LOGV(TAG, "RX Buffer %X %X", rbuf[0], rbuf[1]);
      memset(rbuf, 0, sizeof rbuf);
      if (this->read_buffer_(rbuf, sizeof rbuf, rbuf[1])) {
        this->process_rx_(rbuf);
      }
    }
    this->clear_irq_status();
  }
}

void SX1262Component::process_rx_(const uint8_t *buf) {
  uint8_t data[PKT_LEN + 2];
  static uint16_t last_crc;
  for (size_t i = 0; i != sizeof data; i++) {
    size_t mx = i * 2;
    data[i] = ((ManchesterDecode[buf[mx]] << 4) & 0xF0) + (ManchesterDecode[buf[mx + 1]] & 0x0F);
  }
  const uint8_t preamble[]{0x31, 0xFA, 0xB6};
  auto calc_crc = update_crc_ccitt(0x051E, data, PKT_LEN);
  uint16_t crc = (data[PKT_LEN] << 8) + data[PKT_LEN + 1];
  if (crc == calc_crc && crc != last_crc) {
    last_crc = crc;
    auto s = format_hex(data, PKT_LEN);
    ESP_LOGD(TAG, "crc %04X raw data  %s", crc, s.c_str());
    flarm_decrypt(data, this->clock_->timestamp_now());
    s = format_hex(data, PKT_LEN);
    ESP_LOGD(TAG, "crc %04X decrypted %s", crc, s.c_str());
  }
}

void SX1262Component::dump_config() {
  ESP_LOGCONFIG(TAG, "SX1262");
  LOG_PIN("  CS pin: ", this->cs_);
  ESP_LOGCONFIG(TAG, "  Mode: %d", this->mode_);
  if (this->data_rate_ < 1000000) {
    ESP_LOGCONFIG(TAG, "  Data rate: %" PRId32 "kHz", this->data_rate_ / 1000);
  } else {
    ESP_LOGCONFIG(TAG, "  Data rate: %" PRId32 "MHz", this->data_rate_ / 1000000);
  }
}

float SX1262Component::get_setup_priority() const { return setup_priority::DATA; }

}  // namespace sx1262
}  // namespace esphome
