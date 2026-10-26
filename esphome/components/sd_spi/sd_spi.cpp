#include "sd_spi.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <esp_memory_utils.h>
#endif

namespace esphome::sd_spi {

ESPHOME_LOG_TAG(TAG, "sd_spi");

using storage::StorageError;

static constexpr uint32_t SECTOR_SIZE = 512;
static constexpr uint32_t SLOW_RATE = 400000;  // The card must be started at no more than 400 kHz
static constexpr uint32_t CHECK_INTERVAL_MS = 1000;
static constexpr uint32_t INIT_TIMEOUT_MS = 2000;
static constexpr uint32_t READ_TIMEOUT_MS = 200;
static constexpr uint32_t BUSY_TIMEOUT_MS = 500;
static constexpr uint8_t CMD0_RETRIES = 5;
static constexpr uint8_t RESPONSE_POLLS = 10;
static constexpr uint32_t NO_SECTOR = UINT32_MAX;
// Sectors written again soon after, such as the FAT, are then programmed once
static constexpr uint32_t FLUSH_DELAY_MS = 50;
static constexpr uint32_t MAX_LOOP_RUN = 16;  // Sectors written in one loop() pass

static constexpr uint8_t CMD_GO_IDLE = 0;
static constexpr uint8_t CMD_SEND_IF_COND = 8;
static constexpr uint8_t CMD_SEND_CSD = 9;
static constexpr uint8_t CMD_STOP_TRANSMISSION = 12;
static constexpr uint8_t CMD_SET_BLOCKLEN = 16;
static constexpr uint8_t CMD_READ_SINGLE = 17;
static constexpr uint8_t CMD_READ_MULTIPLE = 18;
static constexpr uint8_t CMD_WRITE_SINGLE = 24;
static constexpr uint8_t CMD_WRITE_MULTIPLE = 25;
static constexpr uint8_t ACMD_SEND_OP_COND = 41;
static constexpr uint8_t CMD_APP = 55;
static constexpr uint8_t CMD_READ_OCR = 58;

static constexpr uint8_t R1_READY = 0x00;
static constexpr uint8_t R1_IDLE = 0x01;
static constexpr uint8_t R1_ILLEGAL_COMMAND = 0x04;
static constexpr uint8_t R1_NO_RESPONSE = 0xFF;

static constexpr uint8_t TOKEN_START_BLOCK = 0xFE;
static constexpr uint8_t TOKEN_START_MULTI_WRITE = 0xFC;
static constexpr uint8_t TOKEN_STOP_MULTI_WRITE = 0xFD;
static constexpr uint8_t DATA_RESPONSE_MASK = 0x1F;
static constexpr uint8_t DATA_RESPONSE_ACCEPTED = 0x05;

static constexpr uint32_t IF_COND_CHECK = 0x1AA;  // 2.7 to 3.6 V, check pattern 0xAA
static constexpr uint32_t OCR_HIGH_CAPACITY = 1UL << 30;

static const LogString *card_type_to_string(CardType type) {
  switch (type) {
    case CardType::CARD_TYPE_SD1:
      return LOG_STR("SD version 1");
    case CardType::CARD_TYPE_SD2:
      return LOG_STR("SD version 2");
    case CardType::CARD_TYPE_SDHC:
      return LOG_STR("SDHC/SDXC");
    default:
      return LOG_STR("none");
  }
}

void SdSpi::setup() {
  this->geometry_.block_size = SECTOR_SIZE;
  this->geometry_.read_size = SECTOR_SIZE;
  this->geometry_.program_size = SECTOR_SIZE;
  this->geometry_.flags = storage::BLOCK_FLAG_REMOVABLE;
  if (this->card_detect_pin_ != nullptr)
    this->card_detect_pin_->setup();
  this->fast_rate_ = this->data_rate_;
  this->data_rate_ = SLOW_RATE;
  if (this->cache_capacity_ != 0) {
    RAMAllocator<uint8_t> data_allocator(RAMAllocator<uint8_t>::ALLOC_INTERNAL);
    RAMAllocator<uint32_t> sector_allocator(RAMAllocator<uint32_t>::ALLOC_INTERNAL);
    this->cache_data_ = data_allocator.allocate(this->cache_capacity_ * SECTOR_SIZE);
    this->cache_sectors_ = sector_allocator.allocate(this->cache_capacity_);
    if (this->cache_data_ == nullptr || this->cache_sectors_ == nullptr) {
      ESP_LOGW(TAG, "No memory for the write cache");
      this->cache_capacity_ = 0;
    }
    for (uint16_t i = 0; i != this->cache_capacity_; i++)
      this->cache_sectors_[i] = NO_SECTOR;
  }
  this->spi_setup();
  this->disable_loop();
  this->check_card_();
  this->set_interval(CHECK_INTERVAL_MS, [this]() { this->check_card_(); });
}

void SdSpi::dump_config() {
  ESP_LOGCONFIG(TAG,
                "SD card (SPI):\n"
                "  Data rate: %" PRIu32 " kHz\n"
                "  Write cache: %" PRIu32 " bytes\n"
                "  Card type: %s",
                this->fast_rate_ / 1000, this->cache_capacity_ * SECTOR_SIZE,
                LOG_STR_ARG(card_type_to_string(this->type_)));
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  Card Detect Pin: ", this->card_detect_pin_);
  this->dump_geometry_(TAG);
}

void SdSpi::check_card_() {
  if (this->card_detect_pin_ != nullptr && !this->card_detect_pin_->digital_read()) {
    if (this->state_ != CardState::CARD_STATE_NONE) {
      ESP_LOGI(TAG, "Card removed");
      this->drop_card_();
    }
    return;
  }
  if (this->state_ == CardState::CARD_STATE_NONE)
    this->start_init_();
}

void SdSpi::start_init_() {
  this->set_fast_(false);
  this->type_ = CardType::CARD_TYPE_UNKNOWN;
  // At least 74 clocks with CS high put the card in SPI mode
  this->enable();
  this->cs_->digital_write(true);
  for (uint8_t i = 0; i != 10; i++)
    this->xfer_();
  this->disable();

  this->select_();
  uint8_t r1 = R1_NO_RESPONSE;
  for (uint8_t i = 0; i != CMD0_RETRIES && r1 != R1_IDLE; i++)
    r1 = this->command_(CMD_GO_IDLE, 0);
  if (r1 != R1_IDLE) {
    this->deselect_();
    ESP_LOGV(TAG, "No card found");
    return;
  }
  r1 = this->command_(CMD_SEND_IF_COND, IF_COND_CHECK);
  if (r1 & R1_ILLEGAL_COMMAND) {
    this->type_ = CardType::CARD_TYPE_SD1;
  } else {
    alignas(4) uint8_t r7[4];
    this->receive_(r7, sizeof(r7));
    if ((r7[2] & 0x0F) != 0x01 || r7[3] != 0xAA) {
      this->deselect_();
      ESP_LOGW(TAG, "Card does not support 3.3 V");
      return;
    }
    this->type_ = CardType::CARD_TYPE_SD2;
  }
  this->deselect_();
  this->state_ = CardState::CARD_STATE_INITIALIZING;
  this->init_start_ = millis();
  this->enable_loop();
}

// Runs while the card is starting, and while the write cache holds sectors.
void SdSpi::loop() {
  if (this->state_ == CardState::CARD_STATE_INITIALIZING) {
    this->init_step_();
    return;
  }
  if (this->cache_used_ == 0 || this->state_ != CardState::CARD_STATE_READY) {
    this->disable_loop();
    return;
  }
  if (App.get_loop_component_start_time() - this->last_write_ < FLUSH_DELAY_MS &&
      this->cache_used_ < this->cache_capacity_ / 2)
    return;
  if (this->card_ready_())
    this->flush_run_(MAX_LOOP_RUN);
}

// The card can take up to a second to leave the idle state.
void SdSpi::init_step_() {
  this->select_();
  const uint8_t r1 =
      this->app_command_(ACMD_SEND_OP_COND, this->type_ == CardType::CARD_TYPE_SD1 ? 0 : OCR_HIGH_CAPACITY);
  this->deselect_();
  if (r1 == R1_READY) {
    this->disable_loop();
    this->finish_init_();
    return;
  }
  if (r1 != R1_IDLE || App.get_loop_component_start_time() - this->init_start_ > INIT_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Card did not start (response 0x%02X)", r1);
    this->disable_loop();
    this->state_ = CardState::CARD_STATE_NONE;
  }
}

void SdSpi::finish_init_() {
  this->select_();
  bool ok = true;
  if (this->type_ == CardType::CARD_TYPE_SD2) {
    alignas(4) uint8_t ocr[4];
    ok = this->command_(CMD_READ_OCR, 0) == R1_READY;
    if (ok) {
      this->receive_(ocr, sizeof(ocr));
      if (ocr[0] & (OCR_HIGH_CAPACITY >> 24))
        this->type_ = CardType::CARD_TYPE_SDHC;
    }
  }
  if (ok && this->type_ != CardType::CARD_TYPE_SDHC)
    ok = this->command_(CMD_SET_BLOCKLEN, SECTOR_SIZE) == R1_READY;
  const uint32_t block_count = ok ? this->read_block_count_() : 0;
  this->deselect_();
  if (block_count == 0) {
    ESP_LOGW(TAG, "Card set up failed");
    this->state_ = CardState::CARD_STATE_NONE;
    return;
  }
  this->set_fast_(true);
  this->geometry_.block_count = block_count;
  this->state_ = CardState::CARD_STATE_READY;
  this->media_changed_();
  ESP_LOGI(TAG, "%s card ready, %" PRIu32 " MiB", LOG_STR_ARG(card_type_to_string(this->type_)),
           static_cast<uint32_t>(this->get_capacity() >> 20));
}

void SdSpi::drop_card_() {
  this->disable_loop();
  if (this->cache_used_ != 0) {
    ESP_LOGW(TAG, "%u cached sectors were not written", this->cache_used_);
    for (uint16_t i = 0; i != this->cache_capacity_; i++)
      this->cache_sectors_[i] = NO_SECTOR;
    this->cache_used_ = 0;
  }
  this->state_ = CardState::CARD_STATE_NONE;
  this->type_ = CardType::CARD_TYPE_UNKNOWN;
  if (this->geometry_.block_count != 0) {
    this->geometry_.block_count = 0;
    this->media_changed_();
  }
}

void SdSpi::set_fast_(bool fast) {
  if (this->fast_ == fast)
    return;
  this->spi_teardown();
  this->data_rate_ = fast ? this->fast_rate_ : SLOW_RATE;
  this->spi_setup();
  this->fast_ = fast;
}

void SdSpi::select_() { this->enable(); }

// The card only lets go of its data line after one more byte is clocked with CS high, which matters on a shared bus.
void SdSpi::deselect_() {
  this->cs_->digital_write(true);
  this->xfer_();
  this->disable();
}

uint8_t SdSpi::command_(uint8_t cmd, uint32_t arg) {
  // A stop command interrupts a transfer, so the card is never idle when it is sent
  if (cmd != CMD_GO_IDLE && cmd != CMD_STOP_TRANSMISSION && !this->wait_ready_(BUSY_TIMEOUT_MS))
    return R1_NO_RESPONSE;
  // CRC is only checked on the first two commands, before the card is in SPI mode
  uint8_t crc = 0x01;
  if (cmd == CMD_GO_IDLE) {
    crc = 0x95;
  } else if (cmd == CMD_SEND_IF_COND) {
    crc = 0x87;
  }
  const uint8_t frame[6] = {static_cast<uint8_t>(0x40 | cmd), static_cast<uint8_t>(arg >> 24),
                            static_cast<uint8_t>(arg >> 16),  static_cast<uint8_t>(arg >> 8),
                            static_cast<uint8_t>(arg),        crc};
  this->write_array(frame, sizeof(frame));
  if (cmd == CMD_STOP_TRANSMISSION)
    this->xfer_();  // stuff byte
  for (uint8_t i = 0; i != RESPONSE_POLLS; i++) {
    const uint8_t r1 = this->xfer_();
    if ((r1 & 0x80) == 0)
      return r1;
  }
  return R1_NO_RESPONSE;
}

uint8_t SdSpi::app_command_(uint8_t cmd, uint32_t arg) {
  const uint8_t r1 = this->command_(CMD_APP, 0);
  if (r1 & ~R1_IDLE)
    return r1;
  return this->command_(cmd, arg);
}

bool SdSpi::wait_ready_(uint32_t timeout_ms) {
  const uint32_t start = millis();
  do {
    if (this->xfer_() == 0xFF)
      return true;
  } while (millis() - start < timeout_ms);
  return false;
}

bool SdSpi::read_data_(uint8_t *buf, uint32_t len) {
  const uint32_t start = millis();
  uint8_t token;
  while ((token = this->xfer_()) == 0xFF) {
    if (millis() - start >= READ_TIMEOUT_MS)
      return false;
  }
  if (token != TOKEN_START_BLOCK)
    return false;
  this->receive_(buf, len);
  this->xfer_();  // CRC, not checked in SPI mode
  this->xfer_();
  return true;
}

// The card is busy programming the block afterwards; whatever comes next waits for that
bool SdSpi::write_data_(uint8_t token, const uint8_t *buf) {
  this->transfer_byte(token);
  this->send_(buf, SECTOR_SIZE);
  this->xfer_();  // CRC, not checked in SPI mode
  this->xfer_();
  return (this->xfer_() & DATA_RESPONSE_MASK) == DATA_RESPONSE_ACCEPTED;
}

// Without waiting: true when the card has finished programming
bool SdSpi::card_ready_() {
  this->select_();
  const bool ready = this->xfer_() == 0xFF;
  this->deselect_();
  return ready;
}

uint32_t SdSpi::read_block_count_() {
  alignas(4) uint8_t csd[16];
  if (this->command_(CMD_SEND_CSD, 0) != R1_READY || !this->read_data_(csd, sizeof(csd)))
    return 0;
  switch (csd[0] >> 6) {
    case 0: {
      const uint32_t read_bl_len = csd[5] & 0x0F;
      const uint32_t c_size = ((csd[6] & 0x03) << 10) | (csd[7] << 2) | (csd[8] >> 6);
      const uint32_t c_size_mult = ((csd[9] & 0x03) << 1) | (csd[10] >> 7);
      const uint32_t shift = c_size_mult + 2 + read_bl_len;
      if (shift < 9)
        return 0;
      return (c_size + 1) << (shift - 9);
    }
    case 1: {
      const uint32_t c_size = ((csd[7] & 0x3F) << 16) | (csd[8] << 8) | csd[9];
      return (c_size + 1) * 1024;
    }
    default:
      return 0;
  }
}

// Reads clock out 0xFF, as the card may take anything else as the start of a command.
void SdSpi::receive_(uint8_t *buf, uint32_t len) {
#ifdef USE_ESP32
  // ESP-IDF allocates a heap copy of a PSRAM or unaligned buffer on every transfer; stage it on the stack instead
  if (!esp_ptr_dma_capable(buf) || (reinterpret_cast<uintptr_t>(buf) & 3) != 0) {
    alignas(4) uint8_t staged[SECTOR_SIZE];
    memset(staged, 0xFF, len);
    this->transfer_array(staged, len);
    memcpy(buf, staged, len);
    return;
  }
#endif
  memset(buf, 0xFF, len);
  this->transfer_array(buf, len);
}

void SdSpi::send_(const uint8_t *buf, uint32_t len) {
#ifdef USE_ESP32
  if (!esp_ptr_dma_capable(buf) || (reinterpret_cast<uintptr_t>(buf) & 3) != 0) {
    alignas(4) uint8_t staged[SECTOR_SIZE];
    memcpy(staged, buf, len);
    this->write_array(staged, len);
    return;
  }
#endif
  this->write_array(buf, len);
}

StorageError SdSpi::fail_(StorageError error) {
  this->status_set_warning();
  // A card that stops answering has most likely been removed; set it up again on the next check
  if (error == StorageError::STORAGE_ERROR_TIMEOUT) {
    ESP_LOGW(TAG, "Card not responding");
    this->drop_card_();
  }
  return error;
}

StorageError SdSpi::device_read(uint32_t block, uint32_t offset, void *buf, uint32_t len) {
  const uint32_t sector = block + offset / SECTOR_SIZE;
  const uint32_t count = len / SECTOR_SIZE;
  const uint32_t address = this->type_ == CardType::CARD_TYPE_SDHC ? sector : sector * SECTOR_SIZE;
  auto *dst = static_cast<uint8_t *>(buf);
  if (count == 1 && this->cache_used_ != 0) {
    const int slot = this->find_cached_(sector);
    if (slot >= 0) {
      memcpy(dst, this->cache_data_ + slot * SECTOR_SIZE, SECTOR_SIZE);
      return StorageError::STORAGE_ERROR_NONE;
    }
  }
  this->select_();
  const uint8_t r1 = this->command_(count == 1 ? CMD_READ_SINGLE : CMD_READ_MULTIPLE, address);
  bool ok = r1 == R1_READY;
  for (uint32_t i = 0; ok && i != count; i++)
    ok = this->read_data_(dst + i * SECTOR_SIZE, SECTOR_SIZE);
  if (r1 == R1_READY && count != 1) {
    ok = this->command_(CMD_STOP_TRANSMISSION, 0) == R1_READY && ok;
    ok = this->wait_ready_(BUSY_TIMEOUT_MS) && ok;
  }
  this->deselect_();
  if (r1 == R1_NO_RESPONSE)
    return this->fail_(StorageError::STORAGE_ERROR_TIMEOUT);
  if (!ok)
    return this->fail_(StorageError::STORAGE_ERROR_IO);
  // Cached sectors are newer than the card's copies
  for (uint16_t i = 0; this->cache_used_ != 0 && i != this->cache_capacity_; i++) {
    const uint32_t cached = this->cache_sectors_[i];
    if (cached != NO_SECTOR && cached - sector < count)
      memcpy(dst + (cached - sector) * SECTOR_SIZE, this->cache_data_ + i * SECTOR_SIZE, SECTOR_SIZE);
  }
  this->status_clear_warning();
  return StorageError::STORAGE_ERROR_NONE;
}

// block_at(i) returns the data for sector + i. Returns while the card may still be programming the last block.
template<typename F> StorageError SdSpi::write_sectors_(uint32_t sector, uint32_t count, F block_at) {
  const uint32_t address = this->type_ == CardType::CARD_TYPE_SDHC ? sector : sector * SECTOR_SIZE;
  this->select_();
  bool ok;
  uint8_t r1;
  if (count == 1) {
    r1 = this->command_(CMD_WRITE_SINGLE, address);
    ok = r1 == R1_READY && this->write_data_(TOKEN_START_BLOCK, block_at(0));
  } else {
    r1 = this->command_(CMD_WRITE_MULTIPLE, address);
    ok = r1 == R1_READY;
    for (uint32_t i = 0; ok && i != count; i++)
      ok = (i == 0 || this->wait_ready_(BUSY_TIMEOUT_MS)) && this->write_data_(TOKEN_START_MULTI_WRITE, block_at(i));
    if (r1 == R1_READY) {
      ok = this->wait_ready_(BUSY_TIMEOUT_MS) && ok;
      this->transfer_byte(TOKEN_STOP_MULTI_WRITE);
      this->xfer_();
    }
  }
  this->deselect_();
  if (r1 == R1_NO_RESPONSE)
    return this->fail_(StorageError::STORAGE_ERROR_TIMEOUT);
  if (!ok)
    return this->fail_(StorageError::STORAGE_ERROR_IO);
  this->status_clear_warning();
  return StorageError::STORAGE_ERROR_NONE;
}

StorageError SdSpi::device_program(uint32_t block, uint32_t offset, const void *buf, uint32_t len) {
  const uint32_t sector = block + offset / SECTOR_SIZE;
  const uint32_t count = len / SECTOR_SIZE;
  const auto *src = static_cast<const uint8_t *>(buf);
  if (count > this->cache_capacity_ / 2) {
    // Too big to cache; the cached copies of these sectors are now out of date
    this->uncache_(sector, count);
    return this->write_sectors_(sector, count, [src](uint32_t i) { return src + i * SECTOR_SIZE; });
  }
  for (uint32_t i = 0; i != count; i++) {
    int slot = this->find_cached_(sector + i);
    if (slot < 0) {
      if (this->cache_used_ == this->cache_capacity_) {
        const StorageError err = this->flush_run_(this->cache_capacity_);
        if (err != StorageError::STORAGE_ERROR_NONE)
          return err;
      }
      slot = this->find_cached_(NO_SECTOR);
      this->cache_sectors_[slot] = sector + i;
      this->cache_used_++;
    }
    memcpy(this->cache_data_ + slot * SECTOR_SIZE, src + i * SECTOR_SIZE, SECTOR_SIZE);
  }
  this->last_write_ = millis();
  this->enable_loop();
  return StorageError::STORAGE_ERROR_NONE;
}

int SdSpi::find_cached_(uint32_t sector) const {
  for (uint16_t i = 0; i != this->cache_capacity_; i++) {
    if (this->cache_sectors_[i] == sector)
      return i;
  }
  return -1;
}

void SdSpi::uncache_(uint32_t sector, uint32_t count) {
  for (uint16_t i = 0; this->cache_used_ != 0 && i != this->cache_capacity_; i++) {
    if (this->cache_sectors_[i] != NO_SECTOR && this->cache_sectors_[i] - sector < count) {
      this->cache_sectors_[i] = NO_SECTOR;
      this->cache_used_--;
    }
  }
}

// Write the lowest cached sector and those that follow it, up to max_sectors, in one command.
StorageError SdSpi::flush_run_(uint32_t max_sectors) {
  uint32_t first = NO_SECTOR;
  for (uint16_t i = 0; i != this->cache_capacity_; i++)
    first = std::min(first, this->cache_sectors_[i]);
  if (first == NO_SECTOR)
    return StorageError::STORAGE_ERROR_NONE;
  uint32_t count = 1;
  while (count != max_sectors && first + count != NO_SECTOR && this->find_cached_(first + count) >= 0)
    count++;
  const StorageError err = this->write_sectors_(first, count, [this, first](uint32_t i) {
    return this->cache_data_ + this->find_cached_(first + i) * SECTOR_SIZE;
  });
  // Data that the card refused is dropped rather than retried for ever
  if (err != StorageError::STORAGE_ERROR_NONE) {
    ESP_LOGE(TAG, "Writing %" PRIu32 " cached sectors at %" PRIu32 " failed", count, first);
  }
  this->uncache_(first, count);
  return err;
}

StorageError SdSpi::flush_all_() {
  StorageError result = StorageError::STORAGE_ERROR_NONE;
  while (this->cache_used_ != 0 && this->state_ == CardState::CARD_STATE_READY) {
    const StorageError err = this->flush_run_(this->cache_capacity_);
    if (err != StorageError::STORAGE_ERROR_NONE)
      result = err;
  }
  return result;
}

StorageError SdSpi::device_sync() {
  const StorageError err = this->flush_all_();
  if (err != StorageError::STORAGE_ERROR_NONE)
    return err;
  this->select_();
  const bool ready = this->wait_ready_(BUSY_TIMEOUT_MS);
  this->deselect_();
  return ready ? StorageError::STORAGE_ERROR_NONE : this->fail_(StorageError::STORAGE_ERROR_TIMEOUT);
}

}  // namespace esphome::sd_spi
