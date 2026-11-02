#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "esphome/components/spi/spi.h"

namespace esphome::sd_spi::testing {

class FakePin : public GPIOPin {
 public:
  void setup() override {}
  void pin_mode(gpio::Flags flags) override {}
  gpio::Flags get_flags() const override { return gpio::Flags::FLAG_NONE; }
  bool digital_read() override { return this->level; }
  void digital_write(bool value) override { this->level = value; }
  size_t dump_summary(char *buffer, size_t len) const override { return 0; }

  bool level{true};
};

/// An SD card seen through an SPI delegate, byte by byte, following the SPI mode protocol of the SD specification.
class FakeCard : public spi::SPIDelegate {
 public:
  struct Options {
    bool high_capacity{true};
    bool version1{false};
    uint8_t idle_polls{3};   // ACMD41 calls answered with "idle" before the card is ready
    uint16_t busy_bytes{2};  // Bytes read as busy after each block is written
  };

  explicit FakeCard(Options options);

  void begin_transaction() override { this->cs.digital_write(false); }
  void end_transaction() override { this->cs.digital_write(true); }
  uint8_t transfer(uint8_t in) override;

  uint32_t block_count() const { return static_cast<uint32_t>(this->data.size() / 512); }

  FakePin cs;
  std::vector<uint8_t> data;
  bool present{true};
  uint32_t commands{0};
  uint32_t clocks_deselected{0};

 protected:
  enum class Mode : uint8_t { COMMAND, WRITE_SINGLE, WRITE_MULTI, READ_MULTI };

  void command_(uint8_t cmd, uint32_t arg);
  void receive_write_(uint8_t in);
  void queue_block_(uint32_t sector);
  void queue_bytes_(const uint8_t *bytes, size_t len);
  void queue_busy_();
  uint32_t sector_(uint32_t arg) const { return this->options_.high_capacity ? arg : arg / 512; }
  uint8_t r1_() const { return this->idle_ ? 0x01 : 0x00; }

  Options options_;
  std::deque<uint8_t> out_;
  uint8_t frame_[6]{};
  uint8_t frame_len_{0};
  std::vector<uint8_t> write_buf_;
  Mode mode_{Mode::COMMAND};
  uint32_t sector_pos_{0};
  uint8_t polls_left_;
  bool idle_{false};
  bool app_next_{false};
  bool write_started_{false};
};

}  // namespace esphome::sd_spi::testing
