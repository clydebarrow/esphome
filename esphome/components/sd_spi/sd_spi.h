#pragma once

#include "esphome/components/spi/spi.h"
#include "esphome/components/storage/block_device.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome::sd_spi {

enum class CardState : uint8_t {
  CARD_STATE_NONE,          // No card, or it failed to start
  CARD_STATE_INITIALIZING,  // Waiting for the card to leave the idle state
  CARD_STATE_READY,
};

enum class CardType : uint8_t {
  CARD_TYPE_UNKNOWN,
  CARD_TYPE_SD1,   // Version 1 standard capacity
  CARD_TYPE_SD2,   // Version 2 standard capacity
  CARD_TYPE_SDHC,  // High or extended capacity, addressed in blocks
};

/// An SD card on an SPI bus. The card is set up in the background after boot and after it is inserted, and a
/// user sees it through has_media() and the media generation.
class SdSpi : public storage::BlockDevice,
              public Component,
              public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                    spi::DATA_RATE_20MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_card_detect_pin(GPIOPin *pin) { this->card_detect_pin_ = pin; }

  CardState get_card_state() const { return this->state_; }
  CardType get_card_type() const { return this->type_; }

 protected:
  storage::StorageError device_read(uint32_t block, uint32_t offset, void *buf, uint32_t len) override;
  storage::StorageError device_program(uint32_t block, uint32_t offset, const void *buf, uint32_t len) override;
  storage::StorageError device_sync() override;

  void check_card_();
  void start_init_();
  void finish_init_();
  void drop_card_();
  void set_fast_(bool fast);

  void select_();
  void deselect_();
  uint8_t command_(uint8_t cmd, uint32_t arg);
  uint8_t app_command_(uint8_t cmd, uint32_t arg);
  bool wait_ready_(uint32_t timeout_ms);
  bool read_data_(uint8_t *buf, uint32_t len);
  bool write_data_(uint8_t token, const uint8_t *buf);
  uint32_t read_block_count_();
  uint8_t xfer_() { return this->transfer_byte(0xFF); }
  void receive_(uint8_t *buf, uint32_t len);
  void send_(const uint8_t *buf, uint32_t len);
  storage::StorageError fail_(storage::StorageError error);

  GPIOPin *card_detect_pin_{nullptr};
  uint32_t fast_rate_{0};
  uint32_t init_start_{0};
  CardState state_{CardState::CARD_STATE_NONE};
  CardType type_{CardType::CARD_TYPE_UNKNOWN};
  bool fast_{false};
};

}  // namespace esphome::sd_spi
