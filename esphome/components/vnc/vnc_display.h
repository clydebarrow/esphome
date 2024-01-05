//
// Created by Clyde Stubbs on 3/1/2024.
//

#pragma once

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/network/util.h"

namespace esphome {
namespace vnc {

static const char *const TAG = "vnc";
static const size_t VERSION_LEN = 12;
static const size_t MAX_WRITE = 64 * 1024;
static const size_t PIXEL_BYTES = 4;
static const uint8_t RFB_MAGIC[VERSION_LEN] = {
    'R', 'F', 'B', ' ', '0', '0', '3', '.', '0', '0', '3', '\n',
};

static inline uint8_t *put16_be(uint8_t *buf, uint16_t value) {
  buf[0] = value >> 8;
  buf[1] = value;
  return buf + 2;
}

static inline uint8_t *put32_be(uint8_t *buf, uint32_t value) {
  buf[0] = value >> 24;
  buf[1] = value >> 16;
  buf[2] = value >> 8;
  buf[3] = value;
  return buf + 4;
}

static inline uint16_t get16_be(const uint8_t *buf) { return buf[1] + (buf[0] << 8); }

static inline uint16_t get32_be(const uint8_t *buf) { return buf[3] + (buf[2] << 8) + (buf[1] << 16) + (buf[0] << 24); }

static void printhex(const char *hdr, uint8_t const *buffer, size_t cnt) {
  char strbuf[256];
  sprintf(strbuf, "%s %d bytes ", hdr, cnt);
  for (size_t i = 0; i != cnt; i++) {
    size_t len = strlen(strbuf);
    if (len >= sizeof strbuf - 4)
      break;
    snprintf(strbuf + len, sizeof strbuf - len, "%02X ", buffer[i]);
  }
  esph_log_d(TAG, "%s", strbuf);
}

enum ClientState { STATE_INVALID, STATE_VERSION, STATE_AUTH, STATE_AUTH_RESPONSE, STATE_INIT, STATE_READY };

enum AuthType {
  AUTH_FAILED = 0x00,
  AUTH_NONE = 0x01,
  AUTH_VNC = 0x02,
};

typedef struct circ_buf {
  uint8_t data[256];
  uint8_t inp, outp;
} circ_buf_t;

static inline void buf_clr(circ_buf_t &buf) {
  buf.inp = 0;
  buf.outp = 0;
}
static inline uint8_t buf_size(const circ_buf_t &buf) { return (uint8_t) (buf.inp - buf.outp); }

static inline uint8_t buf_peek(const circ_buf_t &buf) { return buf.data[buf.outp]; }

// get data from the buffer. It is required that len be less than the available data.
static inline void buf_copy(circ_buf_t &buf, uint8_t *dest, uint8_t len) {
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

static inline bool buf_add(circ_buf_t &buf, const uint8_t *src, uint8_t len) {
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
  memcpy(buf.data + buf.inp, src, rem);
  buf.inp += len;
  return true;
}

class VNCDisplay;

class VNCTouchscreen : public touchscreen::Touchscreen {
 public:
  void update_pointer(bool touching, uint16_t x, uint16_t y) {
    if (touching != this->touching_ || (touching && (this->xpos != x || this->ypos != y))) {
      this->store_.touched = true;
      this->updated_ = true;
    }
    this->touching_ = touching;
    this->xpos = x;
    this->ypos = y;
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
      esph_log_d(TAG, "Sending touch %d/%d", xpos, ypos);
      add_raw_touch_position_(0, xpos, ypos);
    }
  }

  bool touching_{};
  bool updated_{true};
  uint16_t xpos{};
  uint16_t ypos{};
};
class VNCClient {
  friend VNCDisplay;

 public:
  VNCClient(std::unique_ptr<socket::Socket> socket, VNCDisplay *parent) : socket_(std::move(socket)), parent_(parent) {
    int err = socket_->setblocking(false);
    if (err != 0) {
      this->remove_ = true;
      esph_log_e(TAG, "Setting nonblocking failed with errno %d", errno);
      return;
    }
    int enable = 1;
    err = socket_->setsockopt(IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
    if (err != 0) {
      this->remove_ = true;
      esph_log_e(TAG, "Setting nodelay failed with errno %d", errno);
      return;
    }
  }

  void close() {
    this->socket_->close();
    this->socket_.release();
  }

  void start() {
    int err = this->write_(RFB_MAGIC, sizeof RFB_MAGIC);
    if (err < 0)
      return;
    this->state_ = STATE_VERSION;
  }

  void write_pixels(size_t x, size_t y, size_t width, size_t height, const uint8_t *buffer) {
    uint8_t header[16];
    header[0] = 0;
    header[1] = 0;
    put16_be(header + 2, 1);
    put16_be(header + 4, x);
    put16_be(header + 6, y);
    put16_be(header + 8, width);
    put16_be(header + 10, height);
    put32_be(header + 12, 0);  // raw encoding
    this->write_(header, sizeof header);
    this->write_(buffer, width * height * sizeof(int32_t));
    esph_log_d(TAG, "Wrote buffer at %d/%d %d/%d", x, y, width, height);
  }

 protected:
  void disconnect_() {
    this->socket_->close();
    this->remove_ = true;
  }

  ssize_t read_(uint8_t *buffer, size_t len) {
    ssize_t res = this->socket_->read(buffer, len);
    if (res < 0) {
      if (errno == EAGAIN)
        return 0;
      esph_log_e(TAG, "socket read failed: %d", errno);
      if (errno == ENOTCONN) {
        this->disconnect_();
      }
    } else {
      printhex("Read", buffer, res);
    }
    return res;
  }

  ssize_t write_(const uint8_t *buffer, size_t len) {
    const uint8_t *ptr = buffer;
    ssize_t res = 0;
    size_t total = 0;
    while (len != 0) {
      res = this->socket_->write(ptr, len);
      if (res < 0) {
        if (errno == EAGAIN) {
          delay(1);
          App.feed_wdt();
          continue;
        }
        esph_log_e(TAG, "socket write failed: %d", errno);
        return res;
      }
      total += res;
      len -= res;
      ptr += res;
    }
    // printhex("Wrote", buffer, total);
    return total;
  }

  std::unique_ptr<socket::Socket> socket_{};
  VNCDisplay *parent_{};
  bool remove_{};
  ClientState state_;
  circ_buf_t inq_{};
};

class VNCDisplay : public display::Display {
 public:
  void add_touchscreen(VNCTouchscreen *tp) {
    this->touchscreens_.add([=](bool touching, uint16_t x, uint16_t y) { tp->update_pointer(touching, x, y); });
  }

  void end_socket_() {
    if (this->socket_ != nullptr) {
      for (auto &client : clients_)
        client->close();
      this->clients_.clear();
      this->socket_->close();
      this->socket_ = nullptr;
    }
  }

  void start_socket_() {
    if (!network::is_connected())
      return;

    this->socket_ = socket::socket_ip(SOCK_STREAM, 0);
    if (this->socket_ == nullptr) {
      ESP_LOGW(TAG, "Could not create socket.");
      this->mark_failed();
      return;
    }
    int enable = 1;
    int err = socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    if (err != 0) {
      ESP_LOGW(TAG, "Socket unable to set reuseaddr: errno %d", err);
      // we can still continue
    }
    err = socket_->setblocking(false);
    if (err != 0) {
      ESP_LOGW(TAG, "Socket unable to set nonblocking mode: errno %d", err);
      this->mark_failed();
      return;
    }

    struct sockaddr_storage server;

    socklen_t sl = socket::set_sockaddr_any((struct sockaddr *) &server, sizeof(server), this->port_);
    if (sl == 0) {
      ESP_LOGW(TAG, "Socket unable to set sockaddr: errno %d", errno);
      this->mark_failed();
      return;
    }

    err = this->socket_->bind((struct sockaddr *) &server, sl);
    if (err != 0) {
      ESP_LOGW(TAG, "Socket unable to bind: errno %d", errno);
      this->mark_failed();
      return;
    }

    err = this->socket_->listen(4);
    if (err != 0) {
      ESP_LOGW(TAG, "Socket unable to listen: errno %d", errno);
      this->mark_failed();
      return;
    }
  }
  void setup() override {
    size_t buffer_length = this->height_ * this->width_ * PIXEL_BYTES;
    ESP_LOGCONFIG(TAG, "Setting up VNC server...");
    ExternalRAMAllocator<uint8_t> allocator(ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);
    this->display_buffer_ = allocator.allocate(buffer_length);
    if (this->display_buffer_ == nullptr) {
      ESP_LOGE(TAG, "Could not allocate buffer for display!");
      this->mark_failed();
      return;
    }
    // clear to grey
    memset(this->display_buffer_, 0x80, buffer_length);
  }

  void dump_config() override {
    esph_log_config(TAG, "VNC Display:");
    esph_log_config(TAG, "  Dimensions: %dpx x %dpx", this->get_width(), this->get_height());
    esph_log_config(TAG, "  Touchscreens: %d", this->touchscreens_.size());
  }

  void loop() override {
    if (this->socket_ == nullptr) {
      if (network::is_connected())
        this->start_socket_();
      return;
    }

    if (!network::is_connected()) {
      this->end_socket_();
      return;
    }
    // Accept new clients
    while (true) {
      struct sockaddr_storage source_addr;
      socklen_t addr_len = sizeof(source_addr);
      auto sock = socket_->accept((struct sockaddr *) &source_addr, &addr_len);
      if (!sock)
        break;
      ESP_LOGD(TAG, "Accepted %s", sock->getpeername().c_str());

      auto *conn = new VNCClient(std::move(sock), this);
      clients_.emplace_back(conn);
      conn->start();
    }

    // Partition clients into remove and active
    auto new_end = std::partition(this->clients_.begin(), this->clients_.end(),
                                  [](const std::unique_ptr<VNCClient> &conn) { return !conn->remove_; });
    // print disconnection messages
    for (auto it = new_end; it != this->clients_.end(); ++it) {
      (*it)->close();
    }
    // resize vector
    this->clients_.erase(new_end, this->clients_.end());

    for (auto &client : this->clients_) {
      client_loop_(*client);
    }
    this->update_frame_();
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
    if (this->display_buffer_ == nullptr)
      return;
    if (x < 0 || y < 0 || x >= this->width_ || y >= this->width_)
      return;
    size_t offs = (x + y * this->width_) * PIXEL_BYTES;
    this->display_buffer_[offs++] = color.b;
    this->display_buffer_[offs++] = color.g;
    this->display_buffer_[offs++] = color.r;
    this->display_buffer_[offs] = 0;
    this->mark_dirty_(x, y, 1, 1);
  }

  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override {
    if (this->display_buffer_ == nullptr)
      return;
    if (w <= 0 || h <= 0 || x_start < 0 || y_start < 0 || x_start + w > this->width_ || y_start + h > this->height_)
      return;
    // if color mapping or software rotation is required, hand this off to the parent implementation. This will
    // do color conversion pixel-by-pixel into the buffer and draw it later. If this is happening the user has not
    // configured the renderer well.
    if (this->rotation_ != display::DISPLAY_ROTATION_0_DEGREES || bitness != display::COLOR_BITNESS_888 || big_endian) {
      return display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset,
                                              y_offset, x_pad);
    }
    esph_log_d(TAG, "direct write");
    if (x_offset == 0 && x_pad == 0) {
      for (auto &client : this->clients_) {
        client->write_pixels(x_start, y_start, w, h, ptr);
      }
    } else {
      this->mark_dirty_(x_start, x_start, w, h);
    }
    return;
    if (x_offset == 0 && x_pad == 0 && w == this->width_) {
      size_t offset = y_start * w * PIXEL_BYTES;
      memcpy(this->display_buffer_ + offset, ptr, w * h * PIXEL_BYTES);
    } else {
      auto stride = (x_offset + w + x_pad);  // source buffer stride in pixels
      for (size_t y = y_offset; y != y_offset + h; y++) {
        size_t offset = (x_start + y * w);
        auto np = ptr + (y * stride + x_offset) * PIXEL_BYTES;
        memcpy(this->display_buffer_ + offset * PIXEL_BYTES, np, w * PIXEL_BYTES);
      }
    }
  }

  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_COLOR; }
  void set_port(uint16_t port) { this->port_ = port; }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  void send_framebuffer(VNCClient &client, size_t x_start, size_t y_start, size_t w, size_t h) {
    esph_log_d(TAG, "Send framebuffer %d/%d %d/%d", x_start, y_start, w, h);
    return;
    if (w == this->width_) {
      client.write_pixels(x_start, y_start, w, h, this->display_buffer_ + y_start * w * PIXEL_BYTES);
    } else {
      for (size_t y = y_start; y != y_start + h; y++) {
        client.write_pixels(x_start, y, w, 1, this->display_buffer_ + (y * this->width_ + x_start) * PIXEL_BYTES);
      }
    }
  }

  void mark_dirty_(ssize_t x, ssize_t y, ssize_t w, ssize_t h) {
    if (x < this->x_min_)
      this->x_min_ = x;
    if (y < this->y_min_)
      this->y_min_ = y;
    if (x + w - 1 > this->x_max_)
      this->x_max_ = x + w - 1;
    if (y + h - 1 > this->y_max_)
      this->y_max_ = y + h - 1;
  }

  void update_frame_() {
    ssize_t w = this->x_max_ - this->x_min_ + 1;
    ssize_t h = this->y_max_ - this->y_min_ + 1;
    if (w > 0 && h > 0) {
      high_freq_.start();
      size_t maxlines = MAX_WRITE / w / PIXEL_BYTES;
      if (h > maxlines)
        h = maxlines;
      for (auto &client : this->clients_) {
        this->send_framebuffer(*client, this->x_min_, this->y_min_, w, h);
      }
      this->y_min_ += h;
      if (this->y_min_ > this->y_max_) {
        this->x_max_ = 0;
        this->y_max_ = 0;
        this->x_min_ = this->width_;
        this->y_min_ = this->height_;
        high_freq_.stop();
      }
    }
  }

  size_t build_init(uint8_t *buffer) {
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
    *sp++ = 24;                       // bit depth
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
    auto name = App.get_name();
    auto len = name.size();
    if (len > 64)
      len = 64;
    sp = put32_be(sp, len);
    memcpy(sp, name.c_str(), len);
    sp += len;
    return sp - buffer;
  }

  bool process_(VNCClient &client) {
    uint8_t buffer[256];
    size_t len;
    switch (buf_peek(client.inq_)) {
      case 0:  // set pixel format
        if (buf_size(client.inq_) >= 20) {
          buf_copy(client.inq_, buffer, 20);
          uint8_t bits_per_pixel = buffer[4];
          uint8_t depth = buffer[5];
          bool big_endian = buffer[6] != 0;
          bool true_color = buffer[7] != 0;
          esph_log_d(TAG, "pixel format: bits %d, depth %d, %s endian, true_color: %s", bits_per_pixel, depth,
                     big_endian ? "Big" : "Little", true_color ? "Yes" : "No");
          if (buffer[4] != 32 || buffer[5] != 24 || buffer[6] != 0 || buffer[7] == 0) {
            esph_log_w(TAG, "Requested color format is not compatible.");
            return false;
          }
          return true;
        }
        break;

      case 2:  // setencodings
        if (buf_size(client.inq_) >= 4) {
          buf_copy(client.inq_, buffer, 4);
          len = get16_be(buffer + 2);
          if (buf_size(client.inq_) >= len * 4) {
            buf_copy(client.inq_, buffer, len * 4);
            esph_log_d(TAG, "Read %d encoding types", len);
          } else {
            esph_log_w(TAG, "Command 2 data truncated.");
          }
          return true;
        }
        break;

      case 3:  // framebuffer update request
        if (buf_size(client.inq_) >= 10) {
          buf_copy(client.inq_, buffer, 10);
          uint8_t incremental = buffer[1];
          uint16_t xpos = get16_be(buffer + 2);
          uint16_t ypos = get16_be(buffer + 4);
          uint16_t width = get16_be(buffer + 6);
          uint16_t height = get16_be(buffer + 8);
          esph_log_d(TAG, "Framebuffer %s update request %d/%d %d/%d", incremental ? "Incremental" : "Immediate", xpos,
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
          }
          return true;
        }
        break;
      case 4:  // key event
        if (buf_size(client.inq_) >= 8) {
          buf_copy(client.inq_, buffer, 8);
          uint8_t down_flag = buffer[1];
          uint32_t key = get32_be(buffer + 4);
          esph_log_d(TAG, "Key event %X %s", (unsigned) key, down_flag ? "Down" : "Up");
          return true;
        }
        break;
      case 5:  // pointer event
        if (buf_size(client.inq_) >= 6) {
          buf_copy(client.inq_, buffer, 6);
          uint8_t mask = buffer[1];
          uint32_t xpos = get16_be(buffer + 2);
          uint32_t ypos = get16_be(buffer + 4);
          this->touchscreens_.call((mask & 1) != 0, xpos, ypos);
          // esph_log_d(TAG, "Pointer event %X %d/%d", mask, (unsigned) xpos, (unsigned) ypos);
          return true;
        }
        break;

      case 6:  // cut buffer message
        if (buf_size(client.inq_) > 8) {
          buf_copy(client.inq_, buffer, 8);
          uint32_t textlen = get32_be(buffer + 4);
          if (textlen < 256 && buf_size(client.inq_) >= textlen) {
            buf_copy(client.inq_, buffer, textlen);
            esph_log_d(TAG, "Received cut buffer %.*s", (unsigned) textlen, buffer);
            return true;
          } else {
            buf_clr(client.inq_);
          }
        }
        break;

      default:
        esph_log_w(TAG, "Unknown command %d", buf_peek(client.inq_));
        buf_clr(client.inq_);
        break;
    }
    return false;
  }
  void client_loop_(VNCClient &client) {
    int err;
    size_t len;
    uint8_t buffer[128];
    switch (client.state_) {
      default:
        break;
      case STATE_VERSION:
        err = client.read_(buffer, VERSION_LEN);
        if (err <= 0)
          break;
        esph_log_d(TAG, "Read %.*s as version", err, buffer);
        // this does not match the RFC, but seems to work
        buffer[0] = 0;
        buffer[1] = 0;
        buffer[2] = 0;
        buffer[3] = AUTH_NONE;
        err = client.write_(buffer, 4);
        if (err < 0)
          break;
        client.state_ = STATE_AUTH;
        break;

      case STATE_AUTH:
        err = client.read_(buffer, 1);
        if (err <= 0)
          break;
        esph_log_i(TAG, "Client requested authentication %d", buffer[0]);
        len = build_init(buffer);
        err = client.write_(buffer, len);
        if (err < 0)
          break;
        client.state_ = STATE_READY;
        buf_clr(client.inq_);
        break;

      case STATE_READY:
        err = client.read_(buffer, sizeof buffer);
        if (err > 0)
          buf_add(client.inq_, buffer, err);
        while (buf_size(client.inq_) != 0) {
          if (!this->process_(client))
            break;
        }
        break;
    }
  };
  size_t width_{};
  size_t height_{};
  ssize_t x_max_{0u};
  ssize_t x_min_{1};
  ssize_t y_max_{0u};
  ssize_t y_min_{1};
  std::unique_ptr<socket::Socket> socket_ = nullptr;
  uint16_t port_{5900};
  std::vector<std::unique_ptr<VNCClient>> clients_;
  uint8_t *display_buffer_{};
  bool listening_{};
  CallbackManager<void(bool, uint16_t, uint16_t)> touchscreens_;
  HighFrequencyLoopRequester high_freq_;
};

}  // namespace vnc
}  // namespace esphome
