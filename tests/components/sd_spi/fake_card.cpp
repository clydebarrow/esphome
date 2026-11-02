#include "fake_card.h"

namespace esphome::sd_spi::testing {

static constexpr uint32_t SECTOR = 512;

FakeCard::FakeCard(Options options) : options_(options), polls_left_(options.idle_polls) {
  // A version 2 card reports 1024 blocks (the smallest CSD v2 size); a version 1 card 64 blocks.
  this->data.assign(SECTOR * (options.version1 || !options.high_capacity ? 64 : 1024), 0);
}

uint8_t FakeCard::transfer(uint8_t in) {
  if (this->cs.level) {
    this->clocks_deselected += 8;
    return 0xFF;
  }
  if (!this->present)
    return 0xFF;

  if (this->mode_ == Mode::WRITE_SINGLE || this->mode_ == Mode::WRITE_MULTI) {
    uint8_t out = 0xFF;
    if (!this->out_.empty()) {
      out = this->out_.front();
      this->out_.pop_front();
    }
    this->receive_write_(in);
    return out;
  }

  // Look for a command frame, even while streaming read data, so a stop command is seen
  if (this->frame_len_ != 0 || (in & 0xC0) == 0x40) {
    this->frame_[this->frame_len_++] = in;
    if (this->frame_len_ == 6) {
      this->frame_len_ = 0;
      const uint32_t arg = (uint32_t(this->frame_[1]) << 24) | (uint32_t(this->frame_[2]) << 16) |
                           (uint32_t(this->frame_[3]) << 8) | this->frame_[4];
      this->out_.clear();
      this->command_(this->frame_[0] & 0x3F, arg);
    }
    return 0xFF;
  }
  if (this->out_.empty() && this->mode_ == Mode::READ_MULTI)
    this->queue_block_(this->sector_pos_++);
  if (this->out_.empty())
    return 0xFF;
  const uint8_t out = this->out_.front();
  this->out_.pop_front();
  return out;
}

void FakeCard::queue_bytes_(const uint8_t *bytes, size_t len) {
  for (size_t i = 0; i != len; i++)
    this->out_.push_back(bytes[i]);
}

void FakeCard::queue_busy_() {
  for (uint16_t i = 0; i != this->options_.busy_bytes; i++)
    this->out_.push_back(0x00);
}

void FakeCard::queue_block_(uint32_t sector) {
  this->out_.push_back(0xFF);
  this->out_.push_back(0xFE);
  this->queue_bytes_(this->data.data() + sector * SECTOR, SECTOR);
  this->out_.push_back(0x00);
  this->out_.push_back(0x00);
}

void FakeCard::command_(uint8_t cmd, uint32_t arg) {
  this->commands++;
  const bool app = this->app_next_;
  this->app_next_ = false;
  this->out_.push_back(0xFF);  // One byte of delay before the response
  if (cmd == 0) {
    // The card enters SPI mode only after at least 74 clocks with CS high
    if (this->clocks_deselected < 74) {
      this->out_.clear();
      return;
    }
    this->idle_ = true;
    this->mode_ = Mode::COMMAND;
    this->polls_left_ = this->options_.idle_polls;
    this->out_.push_back(0x01);
    return;
  }
  if (cmd == 12) {
    this->mode_ = Mode::COMMAND;
    this->out_.push_back(0xFF);  // stuff byte
    this->out_.push_back(0x00);
    return;
  }
  if (app && cmd == 41) {
    if (this->polls_left_ != 0) {
      this->polls_left_--;
    } else if (this->options_.version1 || (arg & (1UL << 30)) || !this->options_.high_capacity) {
      this->idle_ = false;
    }
    this->out_.push_back(this->r1_());
    return;
  }
  switch (cmd) {
    case 8:
      if (this->options_.version1) {
        this->out_.push_back(0x05);
        return;
      }
      this->out_.push_back(this->r1_());
      this->out_.push_back(0x00);
      this->out_.push_back(0x00);
      this->out_.push_back(static_cast<uint8_t>((arg >> 8) & 0x0F));
      this->out_.push_back(static_cast<uint8_t>(arg));
      return;
    case 55:
      this->app_next_ = true;
      this->out_.push_back(this->r1_());
      return;
    case 58: {
      this->out_.push_back(this->r1_());
      const uint8_t ocr[4] = {static_cast<uint8_t>(this->options_.high_capacity ? 0xC0 : 0x80), 0xFF, 0x80, 0x00};
      this->queue_bytes_(ocr, sizeof(ocr));
      return;
    }
    case 16:
      this->out_.push_back(arg == SECTOR ? this->r1_() : 0x40);
      return;
    case 9: {
      uint8_t csd[16]{};
      if (this->options_.high_capacity && !this->options_.version1) {
        csd[0] = 0x40;  // CSD version 2, C_SIZE 0: 1024 blocks
      } else {
        csd[5] = 9;        // READ_BL_LEN 512
        csd[7] = 15 >> 2;  // C_SIZE 15, C_SIZE_MULT 0: 16 * 4 = 64 blocks
        csd[8] = (15 & 0x03) << 6;
      }
      this->out_.push_back(this->r1_());
      this->out_.push_back(0xFF);
      this->out_.push_back(0xFE);
      this->queue_bytes_(csd, sizeof(csd));
      this->out_.push_back(0x00);
      this->out_.push_back(0x00);
      return;
    }
    case 17:
      this->out_.push_back(this->r1_());
      this->queue_block_(this->sector_(arg));
      return;
    case 18:
      this->out_.push_back(this->r1_());
      this->sector_pos_ = this->sector_(arg);
      this->queue_block_(this->sector_pos_++);
      this->mode_ = Mode::READ_MULTI;
      return;
    case 24:
    case 25:
      this->out_.push_back(this->r1_());
      this->sector_pos_ = this->sector_(arg);
      this->mode_ = cmd == 24 ? Mode::WRITE_SINGLE : Mode::WRITE_MULTI;
      this->write_started_ = false;
      return;
    default:
      this->out_.push_back(0x04 | this->r1_());
      return;
  }
}

void FakeCard::receive_write_(uint8_t in) {
  if (!this->write_started_) {
    if (this->mode_ == Mode::WRITE_MULTI && in == 0xFD) {
      this->mode_ = Mode::COMMAND;
      this->out_.push_back(0xFF);
      this->queue_busy_();
      return;
    }
    if (in == (this->mode_ == Mode::WRITE_SINGLE ? 0xFE : 0xFC)) {
      this->write_started_ = true;
      this->write_buf_.clear();
    }
    return;
  }
  this->write_buf_.push_back(in);
  if (this->write_buf_.size() != SECTOR + 2)
    return;
  std::copy(this->write_buf_.begin(), this->write_buf_.begin() + SECTOR,
            this->data.begin() + this->sector_pos_ * SECTOR);
  this->sector_pos_++;
  this->write_started_ = false;
  this->out_.push_back(0xE5);  // data accepted
  this->queue_busy_();         // busy while programming
  if (this->mode_ == Mode::WRITE_SINGLE)
    this->mode_ = Mode::COMMAND;
}

}  // namespace esphome::sd_spi::testing
