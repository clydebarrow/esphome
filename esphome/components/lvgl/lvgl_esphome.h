#pragma once
#include "esphome/core/defines.h"
#ifdef USE_LVGL

// required for clang-tidy
#ifndef LV_CONF_H
#define LV_CONF_SKIP 1  // NOLINT
#endif

#include "esphome/components/display/display.h"
#include "esphome/components/key_provider/key_provider.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "lvgl_hal.h"
#include <lvgl.h>
#include <vector>
#include <map>

#ifdef LVGL_USES_IMAGE
#include "esphome/components/image/image.h"
#endif
#ifdef LVGL_USES_FONT
#include "esphome/components/font/font.h"
#endif
#ifdef LV_USE_TOUCHSCREEN
#include "esphome/components/touchscreen/touchscreen.h"
#endif
namespace esphome {
namespace lvgl {
static const char *const TAG = "lvgl";

#ifdef LVGL_USES_COLOR
static lv_color_t lv_color_from(Color color) { return lv_color_make(color.red, color.green, color.blue); }
#endif
#if LV_COLOR_DEPTH == 16
static const display::ColorBitness LV_BITNESS = display::ColorBitness::COLOR_BITNESS_565;
#elif LV_COLOR_DEPTH == 32
static const display::ColorBitness LV_BITNESS = display::ColorBitness::COLOR_BITNESS_888;
#else
static const display::ColorBitness LV_BITNESS = display::ColorBitness::COLOR_BITNESS_332;
#endif

#ifdef LVGL_USES_IMAGE
static lv_img_dsc_t *lv_img_from(image::Image *src, lv_img_dsc_t *img_dsc = nullptr) {
  if (img_dsc == nullptr)
    img_dsc = new lv_img_dsc_t();  // NOLINT
  img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  img_dsc->header.w = src->get_width();
  img_dsc->header.h = src->get_height();
  img_dsc->data = src->get_data_start();
  img_dsc->data_size = image::image_type_to_width_stride(img_dsc->header.w * img_dsc->header.h, src->get_type());
  switch (src->get_type()) {
    case image::IMAGE_TYPE_BINARY:
      img_dsc->header.cf = LV_COLOR_FORMAT_A1;
      break;

    case image::IMAGE_TYPE_GRAYSCALE:
      img_dsc->header.cf = LV_COLOR_FORMAT_A8;
      break;

    case image::IMAGE_TYPE_RGB24:
      img_dsc->header.cf = LV_COLOR_FORMAT_RGB888;
      break;

    case image::IMAGE_TYPE_RGB565:
      if (src->has_transparency())
        esph_log_e(TAG, "Chroma keyed image not supported");
#if LV_COLOR_DEPTH == 16
      img_dsc->header.cf = LV_COLOR_FORMAT_NATIVE;
#else
      img_dsc->header.cf = LV_COLOR_FORMAT_RGB565;
#endif
      break;

    case image::IMAGE_TYPE_RGBA:
#if LV_COLOR_DEPTH == 32
      img_dsc->header.cf = LV_COLOR_FORMAT_NATIVE;
#else
      img_dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
#endif
      break;
  }
  return img_dsc;
}
#endif

// Parent class for things that wrap an LVGL object
class LvCompound {
 public:
  virtual void set_obj(lv_obj_t *lv_obj) { this->obj = lv_obj; }
  lv_obj_t *obj{};
};

#if LV_USE_KEYBOARD
static const char *const kb_special_keys[] = {
    "abc", "ABC", "1#",
    // maybe add other special keys here
};
class LvKeyboardType : public key_provider::KeyProvider, public LvCompound {
 public:
  void set_obj(lv_obj_t *lv_obj) override {
    LvCompound::set_obj(lv_obj);
    lv_obj_add_event_cb(
        lv_obj,
        [](lv_event_t *event) {
          auto *self = (LvKeyboardType *) event->user_data;
          if (self->key_callback_.size() == 0)
            return;

          auto key_idx = lv_btnmatrix_get_selected_btn(self->obj);
          if (key_idx == LV_BTNMATRIX_BTN_NONE)
            return;
          const char *txt = lv_btnmatrix_get_btn_text(self->obj, key_idx);
          if (txt == NULL)
            return;
          for (auto i = 0; i != sizeof(kb_special_keys) / sizeof(kb_special_keys[0]); i++) {
            if (strcmp(txt, kb_special_keys[i]) == 0)
              return;
          }
          while (*txt != 0)
            self->send_key_(*txt++);
        },
        LV_EVENT_PRESSED, this);
  }
};
#endif
#if LV_USE_IMG
class LvImgType : public LvCompound {
 public:
  void set_src(image::Image *src) {
    lv_img_from(src, &this->img_);
    lv_img_set_src(this->obj, &this->img_);
  }

 protected:
  lv_img_dsc_t img_{};
};
#endif
#if LV_USE_BTNMATRIX
class LvBtnmatrixType : public key_provider::KeyProvider, public LvCompound {
 public:
  void set_obj(lv_obj_t *lv_obj) override {
    LvCompound::set_obj(lv_obj);
    lv_obj_add_event_cb(
        lv_obj,
        [](lv_event_t *event) {
          LvBtnmatrixType *self = (LvBtnmatrixType *) event->user_data;
          if (self->key_callback_.size() == 0)
            return;
          auto key_idx = lv_btnmatrix_get_selected_btn(self->obj);
          if (key_idx == LV_BTNMATRIX_BTN_NONE)
            return;
          if (self->key_map_.count(key_idx) != 0) {
            self->send_key_(self->key_map_[key_idx]);
            return;
          }
          auto str = lv_btnmatrix_get_btn_text(self->obj, key_idx);
          auto len = strlen(str);
          while (len--)
            self->send_key_(*str++);
        },
        LV_EVENT_PRESSED, this);
  }

  uint16_t *get_selected() { return this->get_btn(lv_btnmatrix_get_selected_btn(this->obj)); }

  uint16_t *get_btn(uint16_t index) {
    if (index >= this->btn_ids_.size())
      return nullptr;
    return this->btn_ids_[index];
  }

  void set_key(size_t idx, uint8_t key) { this->key_map_[idx] = key; }
  void add_btn(uint16_t *id) { this->btn_ids_.push_back(id); }

 protected:
  std::map<size_t, uint8_t> key_map_{};
  std::vector<uint16_t *> btn_ids_{};
};
#endif

typedef struct {
  lv_obj_t *page;
  size_t index;
  bool skip;
} LvPageType;

typedef std::function<void(lv_obj_t *)> LvLambdaType;
typedef std::function<void(float)> set_value_lambda_t;
typedef void(event_callback_t)(_lv_event_t *);
typedef std::function<const char *(void)> text_lambda_t;

template<typename... Ts> class ObjUpdateAction : public Action<Ts...> {
 public:
  explicit ObjUpdateAction(std::function<void(Ts...)> &&lamb) : lamb_(std::move(lamb)) {}

  void play(Ts... x) override { this->lamb_(x...); }

 protected:
  std::function<void(Ts...)> lamb_;
};

#ifdef LVGL_USES_FONT
class FontEngine {
 public:
  FontEngine(font::Font *esp_font) : font_(esp_font) {
    this->lv_font_.line_height = this->height_ = esp_font->get_height();
    this->lv_font_.base_line = this->baseline_ = this->lv_font_.line_height - esp_font->get_baseline();
    this->lv_font_.get_glyph_dsc = get_glyph_dsc_cb;
    this->lv_font_.get_glyph_bitmap = get_glyph_bitmap;
    this->lv_font_.dsc = this;
    this->lv_font_.subpx = LV_FONT_SUBPX_NONE;
    this->lv_font_.underline_position = -1;
    this->lv_font_.underline_thickness = 1;
    this->bpp_ = esp_font->get_bpp();
  }

  const lv_font_t *get_lv_font() { return &this->lv_font_; }

  static bool get_glyph_dsc_cb(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t unicode_letter,
                               uint32_t next) {
    FontEngine *fe = (FontEngine *) font->dsc;
    const font::GlyphData *gd = fe->get_glyph_data(unicode_letter);
    if (gd == nullptr)
      return false;
    dsc->adv_w = gd->offset_x + gd->width;
    dsc->ofs_x = gd->offset_x;
    dsc->ofs_y = fe->height_ - gd->height - gd->offset_y - fe->baseline_;
    dsc->box_w = gd->width;
    dsc->box_h = gd->height;
    dsc->is_placeholder = 0;
    dsc->format = fe->bpp_;
    return true;
  }

  static const void *get_glyph_bitmap(lv_font_glyph_dsc_t *dsc, uint32_t unicode_letter, lv_draw_buf_t *) {
    FontEngine *fe = (FontEngine *) dsc->resolved_font->dsc;
    const font::GlyphData *gd = fe->get_glyph_data(unicode_letter);
    if (gd == nullptr)
      return nullptr;
    // esph_log_d(TAG, "Returning bitmap @  %X", (uint32_t)gd->data);

    return gd->data;
  }

 protected:
  font::Font *font_{};
  uint32_t last_letter_{};
  const font::GlyphData *last_data_{};
  lv_font_t lv_font_{};
  uint16_t baseline_{};
  uint16_t height_{};
  uint8_t bpp_{};

  const font::GlyphData *get_glyph_data(uint32_t unicode_letter) {
    if (unicode_letter == last_letter_)
      return this->last_data_;
    uint8_t unicode[5];
    memset(unicode, 0, sizeof unicode);
    if (unicode_letter > 0xFFFF) {
      unicode[0] = 0xF0 + ((unicode_letter >> 18) & 0x7);
      unicode[1] = 0x80 + ((unicode_letter >> 12) & 0x3F);
      unicode[2] = 0x80 + ((unicode_letter >> 6) & 0x3F);
      unicode[3] = 0x80 + (unicode_letter & 0x3F);
    } else if (unicode_letter > 0x7FF) {
      unicode[0] = 0xE0 + ((unicode_letter >> 12) & 0xF);
      unicode[1] = 0x80 + ((unicode_letter >> 6) & 0x3F);
      unicode[2] = 0x80 + (unicode_letter & 0x3F);
    } else if (unicode_letter > 0x7F) {
      unicode[0] = 0xC0 + ((unicode_letter >> 6) & 0x1F);
      unicode[1] = 0x80 + (unicode_letter & 0x3F);
    } else {
      unicode[0] = unicode_letter;
    }
    int match_length;
    int glyph_n = this->font_->match_next_glyph(unicode, &match_length);
    if (glyph_n < 0)
      return nullptr;
    this->last_data_ = this->font_->get_glyphs()[glyph_n].get_glyph_data();
    this->last_letter_ = unicode_letter;
    return this->last_data_;
  }
};
#endif  // LVGL_USES_FONT

#if LV_USE_ANIMIMG

static void lv_animimg_stop(lv_obj_t *obj) {
  lv_animimg_t *animg = (lv_animimg_t *) obj;
  int32_t duration = animg->anim.duration;
  lv_animimg_set_duration(obj, 0);
  lv_animimg_start(obj);
  lv_animimg_set_duration(obj, duration);
}
#endif

class LvglComponent : public PollingComponent {
 public:
  static void static_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map) {
    reinterpret_cast<LvglComponent *>(lv_display_get_user_data(disp_drv))->flush_cb_(disp_drv, area, px_map);
  }

  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  constexpr const static uint8_t log_levels[] = {
      ESPHOME_LOG_LEVEL_VERBOSE, ESPHOME_LOG_LEVEL_INFO,   ESPHOME_LOG_LEVEL_WARN,
      ESPHOME_LOG_LEVEL_ERROR,   ESPHOME_LOG_LEVEL_CONFIG, ESPHOME_LOG_LEVEL_NONE,
  };
  static void log_cb(lv_log_level_t level, const char *buf) {
    esp_log_printf_(ESPHOME_LOG_LEVEL_DEBUG /*log_levels[level]*/, TAG, 0, "%.*s", (int) strlen(buf) - 1, buf);
  }
  static void rounder_cb(lv_event_t *e) {
    lv_area_t *area = (lv_area_t *) lv_event_get_param(e);
    // make sure all coordinates are even
    if (area->x1 & 1)
      area->x1--;
    if (!(area->x2 & 1))
      area->x2++;
    if (area->y1 & 1)
      area->y1--;
    if (!(area->y2 & 1))
      area->y2++;
    esph_log_v(TAG, "Rounder returns");
  }

  void setup() override {
    esph_log_config(TAG, "LVGL Setup starts");
    lv_tick_set_cb([] {
      esph_log_d(TAG, "tick cb millis=%u", millis());
      return millis();
    });
#if LV_USE_LOG
    lv_log_register_print_cb(log_cb);
#endif
    auto display = this->displays_[0];
    this->hor_res_ = display->get_native_width();
    this->ver_res_ = display->get_native_height();
    size_t buffer_pixels = this->hor_res_ * this->ver_res_ / this->buffer_frac_;
    auto buf_bytes = buffer_pixels * LV_COLOR_DEPTH / 8;
    this->draw_buf_ = (uint8_t *) lv_custom_mem_alloc(buf_bytes);
    if (this->draw_buf_ == nullptr) {
      esph_log_e(TAG, "Malloc failed to allocate %zu bytes", buf_bytes);
      this->mark_failed();
      this->status_set_error("Memory allocation failure");
      return;
    }
    this->disp_ = lv_display_create(this->hor_res_, this->ver_res_);
    lv_display_set_color_format(this->disp_, LV_COLOR_FORMAT_RGB565);
    switch (display->get_rotation()) {
      case display::DISPLAY_ROTATION_0_DEGREES:
        break;
      case display::DISPLAY_ROTATION_90_DEGREES:
        lv_display_set_rotation(this->disp_, LV_DISPLAY_ROTATION_90);
        break;
      case display::DISPLAY_ROTATION_270_DEGREES:
        lv_display_set_rotation(this->disp_, LV_DISPLAY_ROTATION_270);
        break;
      case display::DISPLAY_ROTATION_180_DEGREES:
        lv_display_set_rotation(this->disp_, LV_DISPLAY_ROTATION_180);
        break;
    }
    display->set_rotation(display::DISPLAY_ROTATION_0_DEGREES);
    lv_display_set_buffers(this->disp_, this->draw_buf_, nullptr, buf_bytes,
                           this->full_refresh_ ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(this->disp_, static_flush_cb);
    lv_display_set_user_data(this->disp_, this);
    lv_display_add_event_cb(this->disp_, rounder_cb, LV_EVENT_INVALIDATE_AREA, nullptr);
    this->custom_change_event_ = (lv_event_code_t) lv_event_register_id();
    for (auto v : this->init_lambdas_)
      v(this->disp_);
    this->show_page(0, LV_SCR_LOAD_ANIM_NONE, 0);
    // this->display_->set_writer([](display::Display &d) { lv_timer_handler(); });
    lv_display_trigger_activity(this->disp_);
    esph_log_config(TAG, "LVGL Setup complete");
  }

  void update() override {
    // update indicators
    if (this->paused_) {
      return;
    }
    this->idle_callbacks_.call(lv_display_get_inactive_time(this->disp_));
  }

  void loop() override {
    if (this->paused_) {
      if (this->show_snow_)
        this->write_random();
    }
    esph_log_v(TAG, "running handler");
    lv_timer_handler_run_in_period(5);
    esph_log_v(TAG, "timer handler returns");
  }

  void add_on_idle_callback(std::function<void(uint32_t)> &&callback) {
    this->idle_callbacks_.add(std::move(callback));
  }

  void add_display(display::Display *display) { this->displays_.push_back(display); }
  void add_init_lambda(std::function<void(lv_display_t *)> lamb) { this->init_lambdas_.push_back(lamb); }
  void dump_config() override { esph_log_config(TAG, "LVGL:"); }
  lv_event_code_t get_custom_change_event() { return this->custom_change_event_; }
  void set_full_refresh(bool full_refresh) { this->full_refresh_ = full_refresh; }
  void set_paused(bool paused, bool show_snow) {
    this->paused_ = paused;
    this->show_snow_ = show_snow;
    this->snow_line_ = 0;
    if (!paused && lv_screen_active() != nullptr) {
      lv_display_trigger_activity(this->disp_);  // resets the inactivity time
      lv_obj_invalidate(lv_screen_active());
    }
  }
  void set_big_endian(bool endian) { this->big_endian_ = endian; }
  bool is_paused() { return this->paused_; }
  void set_page_wrap(bool page_wrap) { this->page_wrap_ = page_wrap; }
  bool is_idle(uint32_t idle_ms) { return lv_display_get_inactive_time(this->disp_) > idle_ms; }
  void set_buffer_frac(size_t frac) { this->buffer_frac_ = frac; }
  void add_page(LvPageType *page) { this->pages_.push_back(page); }
  void show_page(size_t index, lv_screen_load_anim_t anim, uint32_t time) {
    if (index >= this->pages_.size())
      return;
    this->page_index_ = index;
    lv_screen_load_anim(this->pages_[index]->page, anim, time, 0, false);
  }
  void show_next_page(bool reverse, lv_screen_load_anim_t anim, uint32_t time) {
    if (this->pages_.empty())
      return;
    int next = this->page_index_;
    do {
      if (reverse) {
        if (next == 0) {
          if (!this->page_wrap_)
            return;
          next = this->pages_.size();
        }
        next--;
      } else {
        if (++next == this->pages_.size()) {
          if (!this->page_wrap_)
            return;
          next = 0;
        }
      }
    } while (this->pages_[next]->skip && next != this->page_index_);
    this->show_page(next, anim, time);
  }

  ssize_t get_page_index() { return this->page_index_; }
  lv_display_t *get_disp() { return this->disp_; }

 protected:
  void write_random() {
    // length of 2 lines in 32 bit units
    // we write 2 lines for the benefit of displays that won't write one line at a time.
    size_t line_len = this->hor_res_ * LV_COLOR_DEPTH / 8 / 4 * 2;
    for (size_t i = 0; i != line_len; i++) {
      ((uint32_t *) (this->draw_buf_))[i] = random_uint32();
    }
    lv_area_t area;
    area.x1 = 0;
    area.x2 = this->hor_res_ - 1;
    if (this->snow_line_ == this->ver_res_ / 2) {
      area.y1 = random_uint32() % (this->ver_res_ / 2) * 2;
    } else {
      area.y1 = this->snow_line_++ * 2;
    }
    // write 2 lines
    area.y2 = area.y1 + 1;
    this->draw_buffer_(&area, this->draw_buf_);
  }

  void draw_buffer_(const lv_area_t *area, uint8_t *ptr) {
#if LV_COLOR_DEPTH == 16
    if (this->big_endian_)
      lv_draw_sw_rgb565_swap(ptr, lv_area_get_size(area));
#endif
    for (auto display : this->displays_) {
      display->draw_pixels_at(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area), ptr,
                              display::COLOR_ORDER_RGB, LV_BITNESS, this->big_endian_);
    }
  }

  void flush_cb_(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map) {
    if (!this->paused_) {
      auto now = millis();
      this->draw_buffer_(area, px_map);
      esph_log_d(TAG, "flush_cb, area=%d/%d, %d/%d took %dms", area->x1, area->y1, lv_area_get_width(area),
                 lv_area_get_height(area), (int) (millis() - now));
    }
    lv_display_flush_ready(disp_drv);
  }

  std::vector<display::Display *> displays_{};
  lv_display_t *disp_{};
  lv_event_code_t custom_change_event_{};

  CallbackManager<void(uint32_t)> idle_callbacks_{};
  std::vector<std::function<void(lv_display_t *)>> init_lambdas_;
  std::vector<LvPageType *> pages_{};
  bool page_wrap_{true};
  ssize_t page_index_{-1};
  size_t buffer_frac_{1};
  bool paused_{};
  bool show_snow_{};
  bool big_endian_{};
  uint32_t snow_line_{};
  bool full_refresh_{};
  uint8_t *draw_buf_{};
  uint32_t hor_res_{};
  uint32_t ver_res_{};
};

class IdleTrigger : public Trigger<> {
 public:
  explicit IdleTrigger(LvglComponent *parent, TemplatableValue<uint32_t> timeout) : timeout_(timeout) {
    parent->add_on_idle_callback([this](uint32_t idle_time) {
      if (!this->is_idle_ && idle_time > this->timeout_.value()) {
        this->is_idle_ = true;
        this->trigger();
      } else if (this->is_idle_ && idle_time < this->timeout_.value()) {
        this->is_idle_ = false;
      }
    });
  }

 protected:
  TemplatableValue<uint32_t> timeout_;
  bool is_idle_{};
};

template<typename... Ts> class LvglAction : public Action<Ts...>, public Parented<LvglComponent> {
 public:
  void play(Ts... x) override { this->action_(this->parent_); }

  void set_action(std::function<void(LvglComponent *)> action) { this->action_ = action; }

 protected:
  std::function<void(LvglComponent *)> action_{};
};

template<typename... Ts> class LvglCondition : public Condition<Ts...>, public Parented<LvglComponent> {
 public:
  bool check(Ts... x) override { return this->condition_lambda_(this->parent_); }
  void set_condition_lambda(std::function<bool(LvglComponent *)> condition_lambda) {
    this->condition_lambda_ = condition_lambda;
  }

 protected:
  std::function<bool(LvglComponent *)> condition_lambda_{};
};

class LVKeyListener {
 public:
  LVKeyListener(uint32_t long_press_time, uint32_t long_press_repeat_time)
      : long_press_time_(long_press_time), long_press_repeat_time_(long_press_repeat_time) {}

 protected:
  uint32_t long_press_time_;
  uint32_t long_press_repeat_time_;
};

#ifdef LV_USE_TOUCHSCREEN
class LVTouchListener : public touchscreen::TouchListener, public Parented<LvglComponent>, public LVKeyListener {
 public:
  LVTouchListener(uint32_t long_press_time, uint32_t long_press_repeat_time)
      : LVKeyListener(long_press_time, long_press_repeat_time) {}
  void setup() {
    this->drv = lv_indev_create();
    lv_indev_set_type(this->drv, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(this->drv, this);
    // this->drv->long_press_repeat_time = long_press_repeat_time;
    // this->drv.long_press_time = long_press_time;
    lv_indev_set_read_cb(this->drv, [](lv_indev_t *d, lv_indev_data_t *data) {
      LVTouchListener *l = (LVTouchListener *) lv_indev_get_user_data(d);
      if (l->touch_pressed_) {
        data->point.x = l->touch_point_.x;
        data->point.y = l->touch_point_.y;
        data->state = LV_INDEV_STATE_PRESSED;
      } else {
        data->state = LV_INDEV_STATE_RELEASED;
      }
    });
  }
  void update(const touchscreen::TouchPoints_t &tpoints) override {
    this->touch_pressed_ = !this->parent_->is_paused() && !tpoints.empty();
    if (this->touch_pressed_)
      this->touch_point_ = tpoints[0];
  }
  void release() override { touch_pressed_ = false; }
  lv_indev_t *drv{};

 protected:
  touchscreen::TouchPoint touch_point_{};
  bool touch_pressed_{};
};
#endif

#ifdef LV_USE_KEY_LISTENER
class LVEncoderListener : public Parented<LvglComponent>, public LVKeyListener {
 public:
  LVEncoderListener(lv_indev_type_t type, uint32_t long_press_time, uint32_t long_press_repeat_time)
      : LVKeyListener(long_press_time, long_press_repeat_time), type_(type) {}
  void setup() {
    this->drv = lv_indev_create();
    lv_indev_set_type(this->drv, this->type_);
    lv_indev_set_user_data(this->drv, this);
    // this->drv.long_press_time = lpt;
    // this->drv.long_press_repeat_time = lprt;
    this->drv.read_cb = [](lv_indev_t *d, lv_indev_data_t *data) {
      auto *l = (LVEncoderListener *) lv_indev_get_user_data(d);
      data->state = l->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      data->key = l->key_;
      data->enc_diff = l->count_ - l->last_count_;
      l->last_count_ = l->count_;
      data->continue_reading = false;
    };
  }

  void event(int key, bool pressed) {
    if (!this->parent_->is_paused()) {
      this->pressed_ = pressed;
      this->key_ = key;
    }
  }

  void set_count(int32_t count) {
    if (!this->parent_->is_paused())
      this->count_ = count;
  }

  lv_indev_t *drv{};

 protected:
  lv_indev_type_t type_;
  bool pressed_{};
  int32_t count_{};
  int32_t last_count_{};
  int key_{};
};
#endif

}  // namespace lvgl
}  // namespace esphome

#endif
