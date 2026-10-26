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

/** An SD card on an SPI bus. The card is set up in the background after boot and after it is inserted, and a
 * user sees it through has_media() and the media generation.
 *
 * With a write cache, writes of up to half the cache are kept in RAM and written to the card from loop(), joined
 * into multi-sector writes, without waiting for the card while it is busy. sync() writes everything out.
 */
class SdSpi : public storage::BlockDevice,
              public Component,
              public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                    spi::DATA_RATE_20MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_card_detect_pin(GPIOPin *pin) { this->card_detect_pin_ = pin; }
  /// Set the write cache size in sectors; 0 turns it off. Call before setup().
  void set_write_cache(uint16_t sectors) { this->cache_capacity_ = sectors; }

  CardState get_card_state() const { return this->state_; }
  CardType get_card_type() const { return this->type_; }

 protected:
  storage::StorageError device_read(uint32_t block, uint32_t offset, void *buf, uint32_t len) override;
  storage::StorageError device_program(uint32_t block, uint32_t offset, const void *buf, uint32_t len) override;
  storage::StorageError device_sync() override;

  void check_card_();
  void start_init_();
  void init_step_();
  void finish_init_();
  void drop_card_();
  void set_fast_(bool fast);

  void select_();
  void deselect_();
  uint8_t command_(uint8_t cmd, uint32_t arg);
  uint8_t app_command_(uint8_t cmd, uint32_t arg);
  void idle_(uint32_t start_us);
  bool wait_ready_(uint32_t timeout_ms);
  bool read_data_(uint8_t *buf, uint32_t len);
  bool write_data_(uint8_t token, const uint8_t *buf);
  template<typename F> storage::StorageError write_sectors_(uint32_t sector, uint32_t count, F block_at);
  bool card_ready_();
  int find_cached_(uint32_t sector) const;
  void uncache_(uint32_t sector, uint32_t count);
  storage::StorageError flush_run_(uint32_t max_sectors);
  storage::StorageError flush_all_();
  uint32_t read_block_count_();
  uint8_t xfer_() { return this->transfer_byte(0xFF); }
  void receive_(uint8_t *buf, uint32_t len);
  void send_(const uint8_t *buf, uint32_t len);
  void set_warning_(bool warning);
  storage::StorageError fail_(storage::StorageError error);

  GPIOPin *card_detect_pin_{nullptr};
  uint8_t *cache_data_{nullptr};      // cache_capacity_ sectors
  uint32_t *cache_sectors_{nullptr};  // The sector in each slot, or NO_SECTOR when the slot is free
  uint32_t fast_rate_{0};
  uint32_t init_start_{0};
  uint32_t last_write_{0};
  uint16_t cache_capacity_{0};
  uint16_t cache_used_{0};
  CardState state_{CardState::CARD_STATE_NONE};
  CardType type_{CardType::CARD_TYPE_UNKNOWN};
  bool fast_{false};
};

}  // namespace esphome::sd_spi
