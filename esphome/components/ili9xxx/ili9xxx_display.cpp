#include "ili9xxx_display.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ili9xxx {

static const uint16_t SPI_SETUP_US = 100;         // estimated fixed overhead in microseconds for an SPI write
static const uint16_t SPI_MAX_BLOCK_SIZE = 4092;  // Max size of continuous SPI transfer

// store a 16 bit value in a buffer, big endian.
static inline void put16_be(uint8_t *buf, uint16_t value) {
  buf[0] = value >> 8;
  buf[1] = value;
}

void ILI9XXXDisplay::set_madctl() {
  // custom x/y transform and color order
  uint8_t mad = this->color_order_ == display::COLOR_ORDER_BGR ? MADCTL_BGR : MADCTL_RGB;
  if (this->swap_xy_)
    mad |= MADCTL_MV;
  if (this->mirror_x_)
    mad |= MADCTL_MX;
  if (this->mirror_y_)
    mad |= MADCTL_MY;
  this->bus_->write_command(ILI9XXX_MADCTL, mad);
  ESP_LOGD(TAG, "Wrote MADCTL 0x%02X", mad);
}

void ILI9XXXDisplay::setup() {
  ESP_LOGD(TAG, "Setting up ILI9xxx");

  this->reset_pin_->setup();  // OUTPUT
  this->reset_pin_->digital_write(true);
  this->bus_->bus_setup();
  this->reset_pin_->digital_write(false);
  delay(20);
  this->reset_pin_->digital_write(true);
  delay(20);
  this->bus_->begin_transaction();
  this->init_lcd_();
  this->set_madctl();
  this->bus_->write_command(this->pre_invertcolors_ ? ILI9XXX_INVON : ILI9XXX_INVOFF);
  this->bus_->end_transaction();
  this->x_low_ = this->width_;
  this->y_low_ = this->height_;
  this->x_high_ = 0;
  this->y_high_ = 0;
}

void ILI9XXXDisplay::alloc_buffer_() {
  if (this->buffer_color_mode_ == BITS_16) {
    this->init_internal_(this->get_buffer_length_() * 2);
    if (this->buffer_ != nullptr) {
      return;
    }
    this->buffer_color_mode_ = BITS_8;
  }
  this->init_internal_(this->get_buffer_length_());
  if (this->buffer_ == nullptr) {
    this->mark_failed();
  }
}

void ILI9XXXDisplay::dump_config() {
  LOG_DISPLAY("", "ili9xxx", this);
  ESP_LOGCONFIG(TAG, "  Width Offset: %u", this->offset_x_);
  ESP_LOGCONFIG(TAG, "  Height Offset: %u", this->offset_y_);
  switch (this->buffer_color_mode_) {
    case BITS_8_INDEXED:
      ESP_LOGCONFIG(TAG, "  Color mode: 8bit Indexed");
      break;
    case BITS_16:
      ESP_LOGCONFIG(TAG, "  Color mode: 16bit");
      break;
    default:
      ESP_LOGCONFIG(TAG, "  Color mode: 8bit 332 mode");
      break;
  }
  if (this->is_18bitdisplay_) {
    ESP_LOGCONFIG(TAG, "  18-Bit Mode: YES");
  }

  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  ESP_LOGCONFIG(TAG, "  Color order: %s", this->color_order_ == display::COLOR_ORDER_BGR ? "BGR" : "RGB");
  ESP_LOGCONFIG(TAG, "  Swap_xy: %s", YESNO(this->swap_xy_));
  ESP_LOGCONFIG(TAG, "  Mirror_x: %s", YESNO(this->mirror_x_));
  ESP_LOGCONFIG(TAG, "  Mirror_y: %s", YESNO(this->mirror_y_));

  this->bus_->dump_config();
  if (this->is_failed()) {
    ESP_LOGCONFIG(TAG, "  => Failed to init Memory: YES!");
  }
  LOG_UPDATE_INTERVAL(this);
}

float ILI9XXXDisplay::get_setup_priority() const { return setup_priority::HARDWARE; }

void ILI9XXXDisplay::fill(Color color) {
  if (!this->check_buffer_())
    return;
  uint16_t new_color;
  this->x_low_ = 0;
  this->y_low_ = 0;
  this->x_high_ = this->get_width_internal() - 1;
  this->y_high_ = this->get_height_internal() - 1;
  switch (this->buffer_color_mode_) {
    case BITS_8_INDEXED:
      new_color = display::ColorUtil::color_to_index8_palette888(color, this->palette_);
      break;
    case BITS_16:
      new_color = display::ColorUtil::color_to_565(color);
      {
        const uint32_t buffer_length_16_bits = this->get_buffer_length_() * 2;
        if (((uint8_t) (new_color >> 8)) == ((uint8_t) new_color)) {
          // Upper and lower is equal can use quicker memset operation. Takes ~20ms.
          memset(this->buffer_, (uint8_t) new_color, buffer_length_16_bits);
        } else {
          for (uint32_t i = 0; i < buffer_length_16_bits; i = i + 2) {
            this->buffer_[i] = (uint8_t) (new_color >> 8);
            this->buffer_[i + 1] = (uint8_t) new_color;
          }
        }
      }
      return;
    default:
      new_color = display::ColorUtil::color_to_332(color, display::ColorOrder::COLOR_ORDER_RGB);
      break;
  }
  memset(this->buffer_, (uint8_t) new_color, this->get_buffer_length_());
}

void HOT ILI9XXXDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= this->get_width_internal() || x < 0 || y >= this->get_height_internal() || y < 0) {
    return;
  }
  if (!this->check_buffer_())
    return;
  uint32_t pos = (y * width_) + x;
  uint16_t new_color;
  bool updated = false;
  switch (this->buffer_color_mode_) {
    case BITS_8_INDEXED:
      new_color = display::ColorUtil::color_to_index8_palette888(color, this->palette_);
      break;
    case BITS_16:
      pos = pos * 2;
      new_color = display::ColorUtil::color_to_565(color, display::ColorOrder::COLOR_ORDER_RGB);
      if (this->buffer_[pos] != (uint8_t) (new_color >> 8)) {
        this->buffer_[pos] = (uint8_t) (new_color >> 8);
        updated = true;
      }
      pos = pos + 1;
      new_color = new_color & 0xFF;
      break;
    default:
      new_color = display::ColorUtil::color_to_332(color, display::ColorOrder::COLOR_ORDER_RGB);
      break;
  }

  if (this->buffer_[pos] != new_color) {
    this->buffer_[pos] = new_color;
    updated = true;
  }
  if (updated) {
    // low and high watermark may speed up drawing from buffer
    if (x < this->x_low_)
      this->x_low_ = x;
    if (y < this->y_low_)
      this->y_low_ = y;
    if (x > this->x_high_)
      this->x_high_ = x;
    if (y > this->y_high_)
      this->y_high_ = y;
  }
}

void ILI9XXXDisplay::update() {
  if (this->prossing_update_) {
    this->need_update_ = true;
    return;
  }
  this->prossing_update_ = true;
  do {
    this->need_update_ = false;
    this->do_update_();
  } while (this->need_update_);
  this->prossing_update_ = false;
  this->display_();
}

void ILI9XXXDisplay::display_() {
  uint8_t transfer_buffer[ILI9XXX_TRANSFER_BUFFER_SIZE];
  // check if something was displayed
  if ((this->x_high_ < this->x_low_) || (this->y_high_ < this->y_low_)) {
    return;
  }

  // we will only update the changed rows to the display
  size_t const w = this->x_high_ - this->x_low_ + 1;
  size_t const h = this->y_high_ - this->y_low_ + 1;

  // estimate time for a single write
  size_t sw_time =
      this->width_ * h * 2 / this->data_rate_ + this->width_ * h * 2 / SPI_MAX_BLOCK_SIZE * SPI_SETUP_US * 2;
  // estimate time for multiple writes
  size_t mw_time = (w * h * 2) / this->data_rate_ + w * h * 2 / ILI9XXX_TRANSFER_BUFFER_SIZE * SPI_SETUP_US;
  ESP_LOGV(TAG,
           "Start display(xlow:%d, ylow:%d, xhigh:%d, yhigh:%d, width:%d, "
           "height:%zu, mode=%d, 18bit=%d, sw_time=%zuus, mw_time=%zuus)",
           this->x_low_, this->y_low_, this->x_high_, this->y_high_, w, h, this->buffer_color_mode_,
           this->is_18bitdisplay_, sw_time, mw_time);
  auto now = millis();
  this->bus_->begin_transaction();
  if (this->buffer_color_mode_ == BITS_16 && !this->is_18bitdisplay_ && sw_time < mw_time) {
    // 16 bit mode maps directly to display format
    ESP_LOGV(TAG, "Doing single write of %zu bytes", this->width_ * h * 2);
    this->set_addr_window_(0, this->y_low_, this->width_ - 1, this->y_high_);
    this->bus_->write_array(this->buffer_ + this->y_low_ * this->width_ * 2, h * this->width_ * 2);
  } else {
    ESP_LOGV(TAG, "Doing multiple write");
    size_t rem = h * w;  // remaining number of pixels to write
    this->set_addr_window_(this->x_low_, this->y_low_, this->x_high_, this->y_high_);
    size_t idx = 0;    // index into transfer_buffer
    size_t pixel = 0;  // pixel number offset
    size_t pos = this->y_low_ * this->width_ + this->x_low_;
    while (rem-- != 0) {
      uint16_t color_val;
      switch (this->buffer_color_mode_) {
        case BITS_8:
          color_val = display::ColorUtil::color_to_565(display::ColorUtil::rgb332_to_color(this->buffer_[pos++]));
          break;
        case BITS_8_INDEXED:
          color_val = display::ColorUtil::color_to_565(
              display::ColorUtil::index8_to_color_palette888(this->buffer_[pos++], this->palette_));
          break;
        default:  // case BITS_16:
          color_val = (buffer_[pos * 2] << 8) + buffer_[pos * 2 + 1];
          pos++;
          break;
      }
      if (this->is_18bitdisplay_) {
        transfer_buffer[idx++] = (uint8_t) ((color_val & 0xF800) >> 8);  // Blue
        transfer_buffer[idx++] = (uint8_t) ((color_val & 0x7E0) >> 3);   // Green
        transfer_buffer[idx++] = (uint8_t) (color_val << 3);             // Red
      } else {
        put16_be(transfer_buffer + idx, color_val);
        idx += 2;
      }
      if (idx == ILI9XXX_TRANSFER_BUFFER_SIZE) {
        this->bus_->write_array(transfer_buffer, idx);
        idx = 0;
        App.feed_wdt();
      }
      // end of line? Skip to the next.
      if (++pixel == w) {
        pixel = 0;
        pos += this->width_ - w;
      }
    }
    // flush any balance.
    if (idx != 0) {
      this->bus_->write_array(transfer_buffer, idx);
    }
  }
  this->bus_->end_transaction();
  ESP_LOGV(TAG, "Data write took %dms", (unsigned) (millis() - now));
  // invalidate watermarks
  this->x_low_ = this->width_;
  this->y_low_ = this->height_;
  this->x_high_ = 0;
  this->y_high_ = 0;
}

// note that this bypasses the buffer and writes directly to the display.
void ILI9XXXDisplay::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                    display::ColorOrder order, display::ColorBitness bitness, bool big_endian,
                                    int x_offset, int y_offset, int x_pad) {
  if (w <= 0 || h <= 0)
    return;
  // if color mapping or software rotation is required, hand this off to the parent implementation. This will
  // do color conversion pixel-by-pixel into the buffer and draw it later. If this is happening the user has not
  // configured the renderer well.
  if (this->rotation_ != display::DISPLAY_ROTATION_0_DEGREES || bitness != display::COLOR_BITNESS_565 || !big_endian ||
      this->is_18bitdisplay_) {
    return display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset,
                                            x_pad);
  }
  this->bus_->begin_transaction();
  this->set_addr_window_(x_start, y_start, x_start + w - 1, y_start + h - 1);
  // x_ and y_offset are offsets into the source buffer, unrelated to our own offsets into the display.
  if (x_offset == 0 && x_pad == 0 && y_offset == 0) {
    // we could deal here with a non-zero y_offset, but if x_offset is zero, y_offset probably will be so don't bother
    this->bus_->write_array(ptr, w * h * 2);
  } else {
    auto stride = x_offset + w + x_pad;
    for (size_t y = 0; y != h; y++) {
      this->bus_->write_array(ptr + (y + y_offset) * stride + x_offset, w * 2);
    }
  }
  this->bus_->end_transaction();
}

// should return the total size: return this->get_width_internal() * this->get_height_internal() * 2 // 16bit color
// values per bit is huge
uint32_t ILI9XXXDisplay::get_buffer_length_() { return this->get_width_internal() * this->get_height_internal(); }

void ILI9XXXDisplay::init_lcd_() {
  uint8_t cmd, x, num_args;
  const uint8_t *addr = this->init_sequence_;
  while ((cmd = *addr++) > 0) {
    x = *addr++;
    num_args = x & 0x7F;
    this->bus_->write_command(cmd, addr, num_args);
    addr += num_args;
    if (x & 0x80)
      delay(150);  // NOLINT
  }
}

// Tell the display controller where we want to draw pixels.
void ILI9XXXDisplay::set_addr_window_(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  x1 += this->offset_x_;
  x2 += this->offset_x_;
  y1 += this->offset_y_;
  y2 += this->offset_y_;
  uint8_t buf[4];
  buf[0] = x1 >> 8;
  buf[1] = x1;
  buf[2] = x2 >> 8;
  buf[3] = x2;
  this->bus_->begin_transaction();
  this->bus_->write_command(ILI9XXX_CASET, buf, sizeof buf);
  buf[0] = y1 >> 8;
  buf[1] = y1;
  buf[2] = y2 >> 8;
  buf[3] = y2;
  this->bus_->write_command(ILI9XXX_PASET, buf, sizeof buf);
  this->bus_->write_command(ILI9XXX_RAMWR);
  this->bus_->end_transaction();
}

void ILI9XXXDisplay::invert_colors(bool invert) {
  this->pre_invertcolors_ = invert;
  if (is_ready()) {
    this->bus_->write_command(invert ? ILI9XXX_INVON : ILI9XXX_INVOFF);
  }
}

int ILI9XXXDisplay::get_width_internal() { return this->width_; }
int ILI9XXXDisplay::get_height_internal() { return this->height_; }

}  // namespace ili9xxx
}  // namespace esphome
