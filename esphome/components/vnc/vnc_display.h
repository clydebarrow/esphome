//
// Created by Clyde Stubbs on 3/1/2024.
//

#pragma once

#include <utility>
#include <string>

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/network/util.h"
#if USE_HOST
#include <pthread.h>
#endif

namespace esphome {
namespace vnc {
// this code only works on FreeRTOS or Posix host.

#if defined(USE_HOST) || defined(pdTRUE)
static const char *const TAG = "vnc";
static constexpr size_t VERSION_LEN = 12;
static constexpr size_t MAX_WRITE = 64 * 1024;
static constexpr size_t PIXEL_BYTES = 4;
static constexpr uint8_t RFB_MAGIC[VERSION_LEN] = {
    'R', 'F', 'B', ' ', '0', '0', '3', '.', '0', '0', '7', '\n',
};

static uint8_t *put16_be(uint8_t *buf, uint16_t value) {
  buf[0] = value >> 8;
  buf[1] = value;
  return buf + 2;
}

static uint8_t *put32_be(uint8_t *buf, uint32_t value) {
  buf[0] = value >> 24;
  buf[1] = value >> 16;
  buf[2] = value >> 8;
  buf[3] = value;
  return buf + 4;
}

static uint16_t get16_be(const uint8_t *buf) { return buf[1] + (buf[0] << 8); }

static uint32_t get32_be(const uint8_t *buf) {
  return (uint32_t) buf[3] + ((uint32_t) buf[2] << 8) + ((uint32_t) buf[1] << 16) + ((uint32_t) buf[0] << 24);
}

enum ClientState {
  STATE_INVALID,
  STATE_VERSION,
  STATE_AUTH,
  STATE_AUTH_RESPONSE,
  STATE_INIT,
  STATE_READY,
  STATE_CLOSING
};

static const char *const state_names[] = {
    "INVALID", "VERSION", "AUTH", "AUTH_RESPONSE", "INIT", "READY", "CLOSING",
};

enum AuthType {
  AUTH_FAILED = 0x00,
  AUTH_NONE = 0x01,
  AUTH_VNC = 0x02,
  AUTH_TIGHT = 0x10,
};

using circ_buf_t = struct circ_buf {
  uint8_t data[256];
  uint8_t inp, outp;
};

using rect_t = struct {
  ssize_t x_min;
  ssize_t y_min;
  ssize_t x_max;
  ssize_t y_max;
};

static void buf_clr(circ_buf_t &buf) {
  buf.inp = 0;
  buf.outp = 0;
}

static size_t buf_size(const circ_buf_t &buf) { return (uint8_t) (buf.inp - buf.outp); }

static uint8_t buf_peek(const circ_buf_t &buf) { return buf.data[buf.outp]; }

// get data from the buffer. It is required that len be less than the available data.
static void buf_copy(circ_buf_t &buf, uint8_t *dest, uint8_t len) {
  size_t rem = 256 - buf.outp;
  if (rem < len) {
    memcpy(dest, buf.data + buf.outp, rem);
    dest += rem;
    len -= rem;
    buf.outp = 0;
  }
  memcpy(dest, buf.data + buf.outp, len);
  buf.outp += len;
}

// add data to the buffer. Fail if not enough room for entire copy

static bool buf_add(circ_buf_t &buf, const uint8_t *src, uint8_t len) {
  if (256 - buf_size(buf) < len) {
    esph_log_w(TAG, "Could not add %d bytes to buffer", len);
    return false;
  }
  size_t rem = 256 - buf.inp;
  if (rem < len) {
    memcpy(buf.data + buf.inp, src, rem);
    src += rem;
    len -= rem;
    buf.inp = 0;
  }
  memcpy(buf.data + buf.inp, src, len);
  buf.inp += len;
  return true;
}

class VNCDisplay;

class VNCTrigger : public Trigger<>, public Parented<VNCDisplay> {};

class VNCTouchscreen : public touchscreen::Touchscreen {
 public:
  void update_pointer(bool touching, uint16_t x, uint16_t y) {
    if (touching != this->touching_ || (touching && (this->xpos_ != x || this->ypos_ != y))) {
      this->store_.touched = true;
      this->updated_ = true;
    }
    this->touching_ = touching;
    this->xpos_ = x;
    this->ypos_ = y;
  }

  void setup() override {
    this->x_raw_max_ = this->display_->get_width();
    this->y_raw_max_ = this->display_->get_height();
    this->store_.init = true;
  }

 protected:
  void update_touches() override {
    if (!this->updated_) {
      this->skip_update_ = true;
      return;
    }
    this->updated_ = false;
    if (this->touching_) {
      esph_log_v(TAG, "Sending touch %d/%d", xpos_, ypos_);
      add_raw_touch_position_(0, xpos_, ypos_);
    }
  }

  bool touching_{};
  bool updated_{true};
  uint16_t xpos_{};
  uint16_t ypos_{};
};

class VNCDisplay : public display::Display {
 public:
  void add_touchscreen(VNCTouchscreen *tp) {
    this->touchscreens_.add([=](bool touching, uint16_t x, uint16_t y) { tp->update_pointer(touching, x, y); });
  }

  void set_username(const std::string &u) { this->username_ = u; }
  void set_password(const std::string &p) { this->password_ = p; }

  void end_socket_() {
    this->disconnect_();
    this->listen_sock_->close();
    this->listen_sock_.release();
    this->listen_sock_ = nullptr;
  }

  void start_socket_() {
    if (!network::is_connected())
      return;

    this->listen_sock_ = socket::socket_ip(SOCK_STREAM, 0);
    if (this->listen_sock_ == nullptr) {
      esph_log_w(TAG, "Could not create socket.");
      this->mark_failed();
      return;
    }
    int enable = 1;
    int err = this->listen_sock_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    if (err != 0) {
      esph_log_w(TAG, "Socket unable to set reuseaddr: errno %d", err);
      // we can still continue
    }
    err = this->listen_sock_->setblocking(false);
    if (err != 0) {
      esph_log_w(TAG, "Socket unable to set nonblocking mode: errno %d", err);
      this->mark_failed();
      return;
    }

    struct sockaddr_storage server;

    socklen_t sl = socket::set_sockaddr_any((struct sockaddr *) &server, sizeof(server), this->port_);
    if (sl == 0) {
      esph_log_w(TAG, "Socket unable to set sockaddr: errno %d", errno);
      this->mark_failed();
      return;
    }

    err = this->listen_sock_->bind((struct sockaddr *) &server, sl);
    if (err != 0) {
      esph_log_w(TAG, "Socket unable to bind: errno %d", errno);
      this->mark_failed();
      return;
    }

    err = this->listen_sock_->listen(1);
    if (err != 0) {
      esph_log_w(TAG, "Socket unable to listen: errno %d", errno);
      this->mark_failed();
      return;
    }
  }

  void setup() override {
    size_t buffer_length = this->height_ * this->width_ * PIXEL_BYTES;
    esph_log_config(TAG, "Setting up VNC server...");
    ExternalRAMAllocator<uint8_t> allocator(ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);
    this->display_buffer_ = allocator.allocate(buffer_length);
    if (this->display_buffer_ == nullptr) {
      esph_log_e(TAG, "Could not allocate buffer for display!");
      this->mark_failed();
      return;
    }
    // clear to grey
    memset(this->display_buffer_, 0x80, buffer_length);
#if USE_HOST
    auto err = pthread_mutex_init(&this->mutex_, nullptr);
    if (err != 0) {
      esph_log_e(TAG, "Failed to init mutex: err=%d, errno=%d", err, errno);
      this->mark_failed();
      return;
    }
    pthread_t tid;
    err = pthread_create(
        &tid, nullptr,
        [](void *arg) -> void * {
          ((VNCDisplay *) arg)->tx_task_();
          return nullptr;
        },
        this);
    if (err != 0) {
      esph_log_e(TAG, "Failed to init mutex: err=%d, errno=%d", err, errno);
      this->mark_failed();
    }
#else
    this->queue_ = xQueueCreate(200, sizeof(rect_t));
    TaskHandle_t handle;
    xTaskCreatePinnedToCore([](void *arg) { ((VNCDisplay *) arg)->tx_task_(); }, "VNC", 8192, this, 2, &handle, 0);
#endif
  }

  void dump_config() override {
    esph_log_config(TAG, "VNC Display:");
    esph_log_config(TAG, "  Dimensions: %dpx x %dpx", this->get_width(), this->get_height());
    esph_log_config(TAG, "  Touchscreens: %zu", this->touchscreens_.size());
  }

  void loop() override {
    if (this->listen_sock_ == nullptr) {
      if (network::is_connected())
        this->start_socket_();
      return;
    }

    if (!network::is_connected()) {
      this->end_socket_();
      return;
    }
    // Accept new clients
    if (this->client_sock_ == nullptr) {
      struct sockaddr_storage source_addr;
      socklen_t addr_len = sizeof(source_addr);
      this->client_sock_ = listen_sock_->accept((struct sockaddr *) &source_addr, &addr_len);
      if (this->client_sock_) {
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        // this->client_sock_->setblocking(false);
        this->client_sock_->setsockopt(SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        esph_log_d(TAG, "Accepted %s", this->client_sock_->getpeername().c_str());
        int err = this->write_(RFB_MAGIC, sizeof RFB_MAGIC);
        if (err < 0)
          this->disconnect_();
        else
          this->state_ = STATE_VERSION;
      }
    }
    this->client_loop_();
  }

  void update() override {
    this->do_update_();
    this->update_frame_();
  }

  void set_dimensions(size_t width, size_t height) {
    this->width_ = width;
    this->height_ = height;
  }

  int get_height() override { return this->height_; }
  int get_width() override { return this->width_; }

  void draw_pixel_at(int x, int y, Color color) override {
    if (x < 0 || y < 0 || x >= this->width_ || y >= this->height_)
      return;
    size_t offs = (x + y * this->width_) * PIXEL_BYTES;
    this->display_buffer_[offs++] = color.b;
    this->display_buffer_[offs++] = color.g;
    this->display_buffer_[offs++] = color.r;
    this->display_buffer_[offs] = 0;
    this->mark_dirty_(x, y, 1, 1);
  }

  void update_frame_() {
    if (this->is_dirty()) {
      if (this->state_ == STATE_READY) {
        if (this->sendRect(this->dirty_rect_))
          this->mark_clean_();
      }
    }
  }

  bool sendRect(rect_t r) {
#if USE_HOST
    auto err = pthread_mutex_trylock(&this->mutex_);
    if (err == 0) {
      this->queue_.push_back(r);
      pthread_mutex_unlock(&this->mutex_);
      return true;
    } else
      esph_log_v(TAG, "trylock failed - erro %d", err);
    return false;
#else
    return xQueueSend(this->queue_, &r, 0) == pdTRUE;
#endif
  }

  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override {
    if (w <= 0 || h <= 0 || x_start < 0 || y_start < 0 || x_start + w > this->width_ || y_start + h > this->height_)
      return;
    // if color mapping or software rotation is required, hand this off to the parent implementation. This will
    // do color conversion pixel-by-pixel into the buffer and draw it later. If this is happening the user has not
    // configured the renderer well.
    if (this->rotation_ != display::DISPLAY_ROTATION_0_DEGREES || bitness != display::COLOR_BITNESS_888 || big_endian) {
      Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset, x_pad);
    } else if (x_offset == 0 && x_pad == 0 && w == this->width_) {
      memcpy(this->display_buffer_ + y_start * w * PIXEL_BYTES, ptr + y_offset * w * PIXEL_BYTES, h * w * PIXEL_BYTES);
    } else {
      auto stride = w + x_offset + x_pad;
      for (size_t y = 0; y != h; y++) {
        auto dest = this->display_buffer_ + ((y + y_start) * this->width_ + x_start) * PIXEL_BYTES;
        auto src = ptr + ((y + y_offset) * stride + x_offset) * PIXEL_BYTES;
        memcpy(dest, src, w * PIXEL_BYTES);
      }
    }
    if (this->state_ == STATE_READY) {
      rect_t r;
      r.x_min = x_start;
      r.y_min = y_start;
      r.x_max = x_start + w - 1;
      r.y_max = y_start + h - 1;
      if (!sendRect(r))
        this->mark_dirty_(x_start, y_start, w, h);
    }
  }

  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_COLOR; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_on_connect(std::function<void()> on_connect) { this->on_connect_ = std::move(on_connect); }
  void set_on_disconnect(std::function<void()> on_disconnect) { this->on_disconnect_ = std::move(on_disconnect); }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  uint8_t tx_buf_[4096]{};
  size_t tx_buflen_{};

  int get_height_internal() override { return this->get_height(); }
  int get_width_internal() override { return this->get_width(); }

  size_t tx_rem_() const { return sizeof(this->tx_buf_) - this->tx_buflen_; }

  void set_state_(ClientState state) {
    this->state_ = state;
    ESP_LOGD(TAG, "Set state to %s", state_names[state]);
  }
  void tx_16(uint16_t value) {
    put16_be(this->tx_buf_ + this->tx_buflen_, value);
    this->tx_buflen_ += 2;
  }

  void tx_8(uint8_t value) { this->tx_buf_[this->tx_buflen_++] = value; }

  void tx_flush_() {
    if (this->tx_buflen_ != 0) {
      this->write_(this->tx_buf_, this->tx_buflen_);
      this->tx_buflen_ = 0;
    }
  }

  void tx_task_() {
    rect_t rp;
    struct timespec ts {};
    std::vector<rect_t> vec{};
    for (;;) {  // NOLINT
#if USE_HOST
      if (pthread_mutex_lock(&this->mutex_) == 0) {
        for (auto r : this->queue_)
          vec.push_back(r);
        this->queue_.clear();
        pthread_mutex_unlock(&this->mutex_);
      }
      if (!vec.empty()) {
        this->tx_8(0);
        this->tx_8(0);
        this->tx_16(vec.size());
        for (auto r : vec) {
          this->send_framebuffer(r.x_min, r.y_min, r.x_max - r.x_min + 1, r.y_max - r.y_min + 1);
        }
        this->tx_flush_();
        vec.clear();
      }
      ts.tv_sec = 0;
      ts.tv_nsec = 10000000;
      nanosleep(&ts, nullptr);

#else
      if (xQueuePeek(this->queue_, &rp, 1000) == pdPASS) {
        auto count = uxQueueMessagesWaiting(this->queue_);
        esph_log_v(TAG, "Send %d windows", count);
        auto now = millis();
        this->tx_8(0);
        this->tx_8(0);
        this->tx_16(count);
        while (count-- != 0) {
          xQueueReceive(this->queue_, &rp, 1000);
          this->send_framebuffer(rp.x_min, rp.y_min, rp.x_max - rp.x_min + 1, rp.y_max - rp.y_min + 1);
        }
        this->tx_flush_();
        esph_log_v(TAG, "Send_framebuffer took %dms", (int) (millis() - now));
      }
#endif
    }
  }

  // pack the given window into the framebuffer. Flush if required.
  void send_framebuffer(size_t x_start, size_t y_start, size_t w, size_t h) {
    esph_log_v(TAG, "Send_framebuffer w/h %zu/%zu, txbuflen %zu", w, h, this->tx_buflen_);
    if (this->tx_rem_() < 12)
      this->tx_flush_();
    tx_16(x_start);
    tx_16(y_start);
    tx_16(w);
    tx_16(h);
    tx_16(0);  // raw encoding
    tx_16(0);
    for (size_t y = 0; y != h; y++) {
      auto src = this->display_buffer_ + ((y + y_start) * this->width_ + x_start) * PIXEL_BYTES;
      if (!this->big_endian_pixels_) {
        auto bytes = w * PIXEL_BYTES;
        if (this->tx_rem_() < bytes)
          this->tx_flush_();
        memcpy(this->tx_buf_ + this->tx_buflen_, src, bytes);
        this->tx_buflen_ += bytes;
      } else {
        // Convert B,G,R,0 -> 0,R,G,B for big-endian pixel order
        for (size_t x = 0; x != w; x++) {
          if (this->tx_rem_() < 4)
            this->tx_flush_();
          const uint8_t b = src[0];
          const uint8_t g = src[1];
          const uint8_t r = src[2];
          this->tx_buf_[this->tx_buflen_ + 0] = 0;
          this->tx_buf_[this->tx_buflen_ + 1] = r;
          this->tx_buf_[this->tx_buflen_ + 2] = g;
          this->tx_buf_[this->tx_buflen_ + 3] = b;
          this->tx_buflen_ += 4;
          src += 4;
        }
      }
    }
  }

  void mark_dirty_(ssize_t x, ssize_t y, ssize_t w, ssize_t h) {
    this->dirty_rect_.x_min = clamp_at_most(this->dirty_rect_.x_min, x);
    this->dirty_rect_.y_min = clamp_at_most(this->dirty_rect_.y_min, y);
    this->dirty_rect_.x_max = clamp_at_least(this->dirty_rect_.x_max, x + w - 1);
    this->dirty_rect_.y_max = clamp_at_least(this->dirty_rect_.y_max, y + h - 1);
  }

  void mark_clean_() {
    this->dirty_rect_.x_max = 0;
    this->dirty_rect_.y_max = 0;
    this->dirty_rect_.x_min = this->width_;
    this->dirty_rect_.y_min = this->height_;
  }

  bool is_dirty() const {
    return this->dirty_rect_.x_max >= this->dirty_rect_.x_min && this->dirty_rect_.y_max >= this->dirty_rect_.y_min;
  }

  size_t build_init(uint8_t *buffer) const {
    uint8_t *sp = buffer;
    sp = put16_be(sp, this->width_);
    sp = put16_be(sp, this->height_);
    //    *sp++ = 16;
    //    *sp++ = 16;
    //    *sp++ = 1;                        // big-endian
    //    *sp++ = 1;                        // true colour
    //    sp = put16_be(sp, (1 << 5) - 1);  // red max
    //    sp = put16_be(sp, (1 << 6) - 1);  // green max
    //    sp = put16_be(sp, (1 << 5) - 1);  // blue max
    //    *sp++ = 5+6;                       // red shift
    //    *sp++ = 5;                        // green shift
    //    *sp++ = 0;                        // blue shift
    *sp++ = 32;                       // bits per pixel
    *sp++ = 24;                       // colour depth
    *sp++ = 0;                        // little-endian
    *sp++ = 1;                        // true colour
    sp = put16_be(sp, (1 << 8) - 1);  // red max
    sp = put16_be(sp, (1 << 8) - 1);  // green max
    sp = put16_be(sp, (1 << 8) - 1);  // blue max
    *sp++ = 16;                       // red shift
    *sp++ = 8;                        // green shift
    *sp++ = 0;                        // blue shift
    *sp++ = 0;                        // padding
    *sp++ = 0;
    *sp++ = 0;
    const auto &name = App.get_name();
    auto len = name.size();
    len = std::min<size_t>(len, 64);
    sp = put32_be(sp, len);
    memcpy(sp, name.c_str(), len);
    sp += len;
    return sp - buffer;
  }

  bool process_() {
    uint8_t buffer[256];
    if (buf_size(this->inq_) == 0)
      return false;
    if (this->skip_bytes_ != 0) {
      if (buf_size(this->inq_) <= this->skip_bytes_) {
        esph_log_v(TAG, "Skipping %zu bytes from %zu", buf_size(this->inq_), this->skip_bytes_);
        this->skip_bytes_ -= buf_size(this->inq_);
        buf_clr(this->inq_);
        return false;
      }
      esph_log_v(TAG, "Skipping %zu bytes", this->skip_bytes_);
      buf_copy(this->inq_, buffer, this->skip_bytes_);
      this->skip_bytes_ = 0;
    }
    switch (buf_peek(this->inq_)) {
      case 0:  // set pixel format
        if (buf_size(this->inq_) >= 20) {
          buf_copy(this->inq_, buffer, 20);
          uint8_t bits_per_pixel = buffer[4];
          uint8_t depth = buffer[5];
          bool big_endian = buffer[6] != 0;
          bool true_color = buffer[7] != 0;
          uint16_t rmax = get16_be(buffer + 8);
          uint16_t gmax = get16_be(buffer + 10);
          uint16_t bmax = get16_be(buffer + 12);
          uint8_t rshift = buffer[14];
          uint8_t gshift = buffer[15];
          uint8_t bshift = buffer[16];
          esph_log_v(TAG,
                     "pixel format: bits %d, depth %d, %s endian, true_color: %s, rmax=%u gmax=%u bmax=%u rsh=%u "
                     "gsh=%u bsh=%u",
                     bits_per_pixel, depth, big_endian ? "Big" : "Little", true_color ? "Yes" : "No", (unsigned) rmax,
                     (unsigned) gmax, (unsigned) bmax, rshift, gshift, bshift);
          bool supported = (bits_per_pixel == 32 && depth == 24 && true_color && rmax == 255 && gmax == 255 &&
                            bmax == 255 && rshift == 16 && gshift == 8 && bshift == 0);
          if (supported) {
            this->big_endian_pixels_ = big_endian;
          } else {
            esph_log_w(TAG,
                       "Requested color format not supported; keeping server format 32bpp 24b depth little-endian.");
          }
          return true;
        }
        break;

      case 2:  // setencodings
        if (buf_size(this->inq_) >= 4) {
          buf_copy(this->inq_, buffer, 4);
          size_t len = get16_be(buffer + 2);
          if (buf_size(this->inq_) >= len * 4) {
            buf_copy(this->inq_, buffer, len * 4);
            esph_log_d(TAG, "Read %zu encoding types", len);
          } else {
            esph_log_w(TAG, "Command 2 data truncated.");
          }
          return true;
        }
        break;

      case 3:  // framebuffer update request
        if (buf_size(this->inq_) >= 10) {
          buf_copy(this->inq_, buffer, 10);
          uint8_t incremental = buffer[1];
          uint16_t xpos = get16_be(buffer + 2);
          uint16_t ypos = get16_be(buffer + 4);
          uint16_t width = get16_be(buffer + 6);
          uint16_t height = get16_be(buffer + 8);
          esph_log_v(TAG, "Framebuffer %s update request %d/%d %d/%d", incremental ? "Incremental" : "Immediate", xpos,
                     ypos, width, height);
          if (xpos > this->width_)
            xpos = this->width_ - 1;
          if (ypos > this->height_)
            ypos = this->height_ - 1;
          if (xpos + width > this->width_)
            width = this->width_ - xpos;
          if (ypos + height > this->height_)
            height = this->height_ - ypos;
          if (!incremental && width != 0 && height != 0) {
            this->mark_dirty_(xpos, ypos, width, height);
            this->update_frame_();
          }
          return true;
        }
        break;
      case 4:  // key event
        if (buf_size(this->inq_) >= 8) {
          buf_copy(this->inq_, buffer, 8);
          uint8_t down_flag = buffer[1];
          uint32_t key = get32_be(buffer + 4);
          esph_log_v(TAG, "Key event %X %s", (unsigned) key, down_flag ? "Down" : "Up");
          return true;
        }
        break;
      case 5:  // pointer event
        if (buf_size(this->inq_) >= 6) {
          buf_copy(this->inq_, buffer, 6);
          uint8_t mask = buffer[1];
          uint32_t xpos = get16_be(buffer + 2);
          uint32_t ypos = get16_be(buffer + 4);
          this->touchscreens_.call((mask & 1) != 0, xpos, ypos);
          esph_log_vv(TAG, "Pointer event %X %d/%d", mask, (unsigned) xpos, (unsigned) ypos);
          return true;
        }
        break;

      case 6:  // cut buffer message
        if (buf_size(this->inq_) > 8) {
          buf_copy(this->inq_, buffer, 8);
          size_t textlen = get32_be(buffer + 4);
          if (textlen < 256 && buf_size(this->inq_) >= textlen) {
            buf_copy(this->inq_, buffer, textlen);
            esph_log_d(TAG, "Received cut buffer %.*s", (unsigned) textlen, (const char *) buffer);
          } else {
            esph_log_d(TAG, "Skipping cut buffer length %zu", textlen);
            this->skip_bytes_ = textlen;
          }
          return true;
        }
        break;

      default:
        esph_log_w(TAG, "Unknown command %d", buf_peek(this->inq_));
        buf_clr(this->inq_);
        break;
    }
    if (buf_size(this->inq_) != 0) {
      esph_log_w(TAG, "Still %zu bytes in queue", buf_size(this->inq_));
    }

    return false;
  }

  void client_loop_() {
    int err;
    uint8_t buffer[128];
    switch (this->state_) {
      default:
        break;
      case STATE_VERSION: {
        err = this->read_(buffer, VERSION_LEN);
        if (err <= 0)
          break;
        esph_log_d(TAG, "Read %.*s as version", err, (const char *) buffer);
        // RFB 3.7 security types list
        uint8_t types[3];
        size_t ntypes = 0;
        bool creds_both = (!this->username_.empty() && !this->password_.empty());
        // If both username and password are configured and non-empty, do NOT offer NoAuth
        if (!creds_both) {
          types[ntypes++] = AUTH_NONE;
        }
        // Offer Tight if any credential is configured (allows testing with empty fields if desired)
        if (!this->username_.empty() || !this->password_.empty()) {
          types[ntypes++] = AUTH_TIGHT;  // Tight/Unix-Login
        }
        // Ensure we always offer at least one type
        if (ntypes == 0) {
          types[ntypes++] = AUTH_NONE;
        }
        uint8_t out[8];
        out[0] = (uint8_t) ntypes;
        memcpy(out + 1, types, ntypes);
        err = this->write_(out, 1 + ntypes);
        if (err < 0)
          break;
        this->state_ = STATE_AUTH;
        break;
      }

      case STATE_AUTH: {
        // 3.7: client selects one security type (1 byte)
        if (this->auth_progress_ == 0) {
          err = this->read_(buffer, 1);
          if (err <= 0)
            break;
          this->selected_sec_type_ = buffer[0];
          esph_log_i(TAG, "Client selected security type %d", (int) this->selected_sec_type_);
          if (this->selected_sec_type_ == AUTH_NONE) {
            // RFB 3.7: server must send SecurityResult=OK before ClientInit
            uint8_t secres[4];
            put32_be(secres, 0);
            err = this->write_(secres, sizeof secres);
            if (err < 0)
              break;
            this->state_ = STATE_INIT;  // wait for ClientInit
            break;
          } else if (this->selected_sec_type_ == AUTH_TIGHT) {
            // Send tunneling caps (none) and auth caps (Unix Login)
            uint8_t caps[4 + 4 + 16];
            put32_be(caps + 0, 0);             // nTunnelTypes = 0
            put32_be(caps + 4, 1);             // nAuthTypes = 1
            put32_be(caps + 8, 129);           // code = Unix Login
            memcpy(caps + 12, "TGHT", 4);      // vendor
            memcpy(caps + 16, "ULGNAUTH", 8);  // signature
            err = this->write_(caps, sizeof caps);
            if (err < 0)
              break;
            this->auth_progress_ = 1;  // wait for auth scheme request
            this->auth_expected_ = 4;
            this->auth_buflen_ = 0;
            break;
          } else {
            esph_log_w(TAG, "Unsupported security type %d", (int) this->selected_sec_type_);
            this->disconnect_();
            break;
          }
        }
        if (this->selected_sec_type_ == AUTH_TIGHT) {
          // Tight sub-negotiation state machine
          if (this->auth_progress_ == 1) {
            // read 4-byte auth scheme code
            int got = this->read_(this->auth_buf_ + this->auth_buflen_, this->auth_expected_ - this->auth_buflen_);
            if (got <= 0)
              break;
            this->auth_buflen_ += got;
            if (this->auth_buflen_ < this->auth_expected_)
              break;
            uint32_t code = get32_be(this->auth_buf_);
            if (code != 129) {
              esph_log_w(TAG, "Client requested unsupported Tight auth code %u", (unsigned) code);
              this->disconnect_();
              break;
            }
            this->auth_progress_ = 2;  // read username length
            this->auth_buflen_ = 0;
            this->auth_expected_ = 4;
          }
          if (this->auth_progress_ == 2) {
            int got = this->read_(this->auth_buf_ + this->auth_buflen_, this->auth_expected_ - this->auth_buflen_);
            if (got <= 0)
              break;
            this->auth_buflen_ += got;
            if (this->auth_buflen_ < this->auth_expected_)
              break;
            this->tight_user_.clear();
            this->tight_pass_.clear();
            this->tight_next_len_ = get32_be(this->auth_buf_);
            this->auth_progress_ = 3;  // read username string
            this->auth_buflen_ = 0;
            this->auth_expected_ = this->tight_next_len_;
          }
          if (this->auth_progress_ == 3) {
            int got = this->read_(this->auth_tmp_,
                                  std::min<size_t>(sizeof this->auth_tmp_, this->auth_expected_ - this->auth_buflen_));
            if (got <= 0)
              break;
            this->tight_user_.append((const char *) this->auth_tmp_, got);
            this->auth_buflen_ += got;
            if (this->auth_buflen_ < this->auth_expected_)
              break;
            this->auth_progress_ = 4;  // read password length
            this->auth_buflen_ = 0;
            this->auth_expected_ = 4;
          }
          if (this->auth_progress_ == 4) {
            int got = this->read_(this->auth_buf_ + this->auth_buflen_, this->auth_expected_ - this->auth_buflen_);
            if (got <= 0)
              break;
            this->auth_buflen_ += got;
            if (this->auth_buflen_ < this->auth_expected_)
              break;
            this->tight_next_len_ = get32_be(this->auth_buf_);
            this->auth_progress_ = 5;  // read password string
            this->auth_buflen_ = 0;
            this->auth_expected_ = this->tight_next_len_;
          }
          if (this->auth_progress_ == 5) {
            int got = this->read_(this->auth_tmp_,
                                  std::min<size_t>(sizeof this->auth_tmp_, this->auth_expected_ - this->auth_buflen_));
            if (got <= 0)
              break;
            this->tight_pass_.append((const char *) this->auth_tmp_, got);
            this->auth_buflen_ += got;
            if (this->auth_buflen_ < this->auth_expected_)
              break;

            esph_log_d(TAG, "Tight auth received user '%.*s' (%zu bytes)",
                       (int) std::min<size_t>(this->tight_user_.size(), 32), this->tight_user_.c_str(),
                       this->tight_user_.size());
            bool ok = (this->tight_user_ == this->username_) && (this->tight_pass_ == this->password_);
            if (!ok) {
              esph_log_w(TAG, "Authentication failed for user '%s'", this->tight_user_.c_str());
              // Send SecurityResult failure (non-zero) then disconnect
              uint8_t secres[8];
              put32_be(secres, 1);
              // Optionally send reason length 0 (no message)
              put32_be(secres + 4, 0);
              this->write_(secres, sizeof secres);
              this->disconnect_();
              break;
            }
            // Success -> send SecurityResult=OK and proceed to ClientInit
            {
              uint8_t secres[4];
              put32_be(secres, 0);
              err = this->write_(secres, sizeof secres);
              if (err < 0)
                break;
            }
            this->state_ = STATE_INIT;
            this->auth_progress_ = 0;
            this->auth_buflen_ = 0;
            this->auth_expected_ = 0;
          }
        }
        break;
      }

      case STATE_INIT: {
        // Wait for ClientInit (1 byte shared-flag), then send ServerInit
        err = this->read_(buffer, 1);
        if (err <= 0)
          break;
        size_t init_len = build_init(buffer);
        // Send ServerInit (no Tight extras appended)
        err = this->write_(buffer, init_len);
        if (err < 0)
          break;
        break;
        this->state_ = STATE_READY;
        buf_clr(this->inq_);
        if (this->on_connect_ != nullptr) {
          this->defer([this]() { this->on_connect_(); });
          this->mark_dirty_(0, 0, this->width_, this->height_);
          this->update_frame_();
        }
        break;
      }

      case STATE_READY:
        do {
          while (this->process_())
            continue;
          err = this->read_(buffer, sizeof buffer);
          if (err < 0)
            break;
          if (err == 0 && buf_size(this->inq_) == 0)
            break;
          buf_add(this->inq_, buffer, err);
        } while (true);
        break;
    }
  };

  void disconnect_() {
    buf_clr(this->inq_);
    if (this->client_sock_ != nullptr) {
      this->client_sock_->close();
      this->client_sock_ = nullptr;
      this->state_ = STATE_INVALID;
    }
  }

  void write_pixels(size_t x, size_t y, size_t width, size_t height, const uint8_t *buffer) {
    uint8_t header[12];
    put16_be(header + 0, x);
    put16_be(header + 2, y);
    put16_be(header + 4, width);
    put16_be(header + 6, height);
    put32_be(header + 8, 0);  // raw encoding
    this->write_(header, sizeof header);
    this->write_(buffer, width * height * PIXEL_BYTES);
  }

  ssize_t read_(uint8_t *buffer, size_t len) {
    ssize_t res = this->client_sock_->read(buffer, len);
    if (res < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
        return 0;
      esph_log_e(TAG, "socket read failed: %d", errno);
      this->disconnect_();
    } else {
      esph_log_vv(TAG, "Read data %s", format_hex_pretty(buffer, len).c_str());
    }
    return res;
  }

  ssize_t write_(const uint8_t *buffer, size_t len) {
    const uint8_t *ptr = buffer;
    size_t total = 0;
    if (len < 50) {
      ESP_LOGV(TAG, "Write data %s", format_hex_pretty(buffer, len).c_str());
    }
    while (len != 0) {
      ssize_t res = this->client_sock_->write(ptr, len);
      if (res < 0) {
        if (errno == EAGAIN) {
          esph_log_v(TAG, "Socket write delayed, writing %zu bytes", len);
          delay(5);
          continue;
        }
        esph_log_e(TAG, "socket write failed: %d", errno);
        this->disconnect_();
        return res;
      }
      total += res;
      len -= res;
      ptr += res;
    }
    if (total < 100) {
      esph_log_v(TAG, "Wrote data %s", format_hex_pretty(buffer, total).c_str());
    } else {
      esph_log_v(TAG, "Wrote %zu bytes", total);
    }
    return total;
  }

  size_t width_{};
  size_t height_{};
  rect_t dirty_rect_{};
  std::unique_ptr<socket::Socket> listen_sock_ = nullptr;
  uint16_t port_{5900};
  uint8_t *display_buffer_{};
  bool listening_{};
  CallbackManager<void(bool, uint16_t, uint16_t)> touchscreens_;
  HighFrequencyLoopRequester high_freq_;
  std::function<void()> on_connect_{};
  std::function<void()> on_disconnect_{};
  std::unique_ptr<socket::Socket> client_sock_{};
  ClientState state_{STATE_INVALID};
  circ_buf_t inq_{};
  size_t skip_bytes_{};
  // Pixel format state
  bool big_endian_pixels_{};  // default false (little-endian)
  // Auth/config
  std::string username_{};
  std::string password_{};
  uint8_t selected_sec_type_{AUTH_NONE};
  // Tight auth handshake state
  uint8_t auth_buf_[8]{};
  size_t auth_buflen_{};
  size_t auth_expected_{};
  uint8_t auth_tmp_[128]{};
  uint8_t auth_progress_{};  // 0=await select,1=await scheme,2=uname len,3=uname,4=pass len,5=pass
  size_t tight_next_len_{};
  std::string tight_user_{};
  std::string tight_pass_{};
#if USE_HOST
  pthread_mutex_t mutex_{};
  std::vector<rect_t> queue_{};
#else
  QueueHandle_t queue_{};
#endif
};
#endif
}  // namespace vnc
}  // namespace esphome
