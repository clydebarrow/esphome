#pragma once

// required for clang-tidy
#ifndef LV_CONF_SKIP
#define LV_CONF_SKIP 1  // NOLINT
#endif
#include "esphome/components/display/display.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#if LVGL_USES_IMAGE
#include "esphome/components/image/image.h"
#endif
#if LV_USE_FONT
#include "esphome/components/font/font.h"
#endif
#if LV_USE_TOUCHSCREEN
#include "esphome/components/touchscreen/touchscreen.h"
#endif
#if LV_USE_ROTARY_ENCODER
#include "esphome/components/rotary_encoder/rotary_encoder.h"
#endif
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "lvgl_hal.h"
#include <lvgl.h>
#include <vector>

static const char *const TAG = "lvgl";
namespace esphome {
namespace lvgl {

static lv_color_t lv_color_from(Color color) { return lv_color_make(color.red, color.green, color.blue); }
#if LV_COLOR_DEPTH == 16
static const display::ColorBitness LV_BITNESS = display::COLOR_BITNESS_565;
#elif LV_COLOR_DEPTH == 32
static const display::ColorBitness LV_BITNESS = display::COLOR_BITNESS_888;
#else
static const display::ColorBitness LV_BITNESS = display::COLOR_BITNESS_332;
#endif

// The ESPHome name munging does not work well with the lv_ types, and will produce variable names
// that are the same as the type.
// to work-around this these typedefs are used.
typedef lv_obj_t LvScreenType;
typedef lv_obj_t LvObjType;
typedef lv_style_t LvStyleType;
typedef lv_point_t LvPointType;
typedef lv_label_t LvLabelType;
typedef lv_meter_t LvMeterType;
typedef lv_meter_indicator_t LvMeterIndicatorType;
typedef lv_slider_t LvSliderType;
typedef lv_btn_t LvBtnType;
typedef lv_line_t LvLineType;
typedef lv_img_t LvImgType;
typedef lv_arc_t LvArcType;
typedef lv_bar_t LvBarType;
typedef lv_theme_t LvThemeType;
typedef lv_checkbox_t LvCheckboxType;
typedef lv_btnmatrix_t LvBtnmatrixType;
typedef lv_canvas_t LvCanvasType;
typedef lv_dropdown_t LvDropdownType;
typedef lv_dropdown_list_t LvDropdownListType;
typedef lv_roller_t LvRollerType;
typedef lv_led_t LvLedType;
typedef lv_switch_t LvSwitchType;
typedef lv_table_t LvTableType;
typedef lv_textarea_t LvTextareaType;
typedef struct {
  lv_obj_t **btnm;
  uint16_t index;
} LvBtnmBtn;

typedef std::function<float(void)> value_lambda_t;
typedef std::function<void(float)> set_value_lambda_t;
typedef void(event_callback_t)(_lv_event_t *);
typedef std::function<const char *(void)> text_lambda_t;

class Updater {
 public:
  virtual void update() = 0;
};

template<typename... Ts> class ObjUpdateAction : public Action<Ts...> {
 public:
  explicit ObjUpdateAction(std::function<void()> lamb) : lamb_(lamb) {}

  void play(Ts... x) override { this->lamb_(); }

 protected:
  std::function<void()> lamb_;
};

class Arc : public Updater {
 public:
  Arc(lv_obj_t *arc, value_lambda_t lamb) : arc_(arc), value_(lamb) {}

  void update() override {
    float new_value = this->value_();
    if (new_value != this->last_value_) {
      this->last_value_ = new_value;
      lv_arc_set_value(this->arc_, new_value);
    }
  }

 protected:
  lv_obj_t *arc_{};
  value_lambda_t value_{};
  float last_value_{NAN};
};

class Slider : public Updater {
 public:
  Slider(lv_obj_t *slider, value_lambda_t lamb, bool anim) : slider_(slider), value_(lamb), anim_{anim} {}

  void update() override {
    float new_value = this->value_();
    if (new_value != this->last_value_) {
      this->last_value_ = new_value;
      lv_slider_set_value(this->slider_, new_value, this->anim_ ? LV_ANIM_ON : LV_ANIM_OFF);
    }
  }

 protected:
  lv_obj_t *slider_{};
  value_lambda_t value_{};
  float last_value_{NAN};
  bool anim_{};
};

class Bar : public Updater {
 public:
  Bar(lv_obj_t *bar, value_lambda_t lamb, bool anim) : bar_(bar), value_(lamb), anim_{anim} {}

  void update() override {
    float new_value = this->value_();
    if (new_value != this->last_value_) {
      this->last_value_ = new_value;
      lv_bar_set_value(this->bar_, new_value, this->anim_ ? LV_ANIM_ON : LV_ANIM_OFF);
    }
  }

 protected:
  lv_obj_t *bar_{};
  value_lambda_t value_{};
  float last_value_{NAN};
  bool anim_{};
};

class Checkbox : public Updater {
 public:
  Checkbox(lv_obj_t *checkbox, text_lambda_t value) : checkbox_(checkbox), value_(value) {}

  void update() override {
    const char *t = this->value_();
    if (this->data_ != nullptr && strcmp(t, this->data_) == 0)
      return;
    // this jiggery-pokery seems necessary - in theory using set_text() should make the checkbox copy the data
    // internally, but in practice this does not seem to work right.
    if (this->data_len_ <= strlen(t)) {
      this->data_len_ = strlen(t) + 10;
      this->data_ = (char *) realloc(this->data_, this->data_len_);
    }
    strcpy(this->data_, t);
    lv_checkbox_set_text_static(this->checkbox_, this->data_);
  }

 protected:
  lv_obj_t *checkbox_{};
  text_lambda_t value_{};
  char *data_{};
  size_t data_len_{};
};
class Label : public Updater {
 public:
  Label(lv_obj_t *label, text_lambda_t value) : label_(label), value_(value) {}

  void update() override {
    const char *t = this->value_();
    if (this->data_ != nullptr && strcmp(t, this->data_) == 0)
      return;
    // this jiggery-pokery seems necessary - in theory using set_text() should make the label copy the data
    // internally, but in practice this does not seem to work right.
    if (this->data_len_ <= strlen(t)) {
      this->data_len_ = strlen(t) + 10;
      this->data_ = (char *) realloc(this->data_, this->data_len_);
    }
    strcpy(this->data_, t);
    lv_label_set_text_static(this->label_, this->data_);
  }

 protected:
  lv_obj_t *label_{};
  text_lambda_t value_{};
  char *data_{};
  size_t data_len_{};
};

#if LV_USE_METER
class Indicator : public Updater {
 public:
  Indicator(lv_obj_t *meter, lv_meter_indicator_t *indicator, value_lambda_t start_value, value_lambda_t end_value)
      : meter_(meter), indicator_(indicator), start_value_(start_value), end_value_(end_value) {}

 public:
  void update() override {
    float new_value;
    if (this->end_value_ != nullptr) {
      new_value = this->end_value_();
      if (!std::isnan(new_value) && new_value != this->last_end_state_) {
        lv_meter_set_indicator_end_value(this->meter_, this->indicator_, new_value);
        this->last_end_state_ = new_value;
      }
      if (this->start_value_ != nullptr) {
        new_value = this->start_value_();
        if (!std::isnan(new_value) && new_value != this->last_start_state_) {
          lv_meter_set_indicator_start_value(this->meter_, this->indicator_, this->start_value_());
          this->last_start_state_ = new_value;
        }
      }
    } else if (this->start_value_ != nullptr) {
      new_value = this->start_value_();
      if (!std::isnan(new_value) && new_value != this->last_start_state_) {
        lv_meter_set_indicator_value(this->meter_, this->indicator_, this->start_value_());
        this->last_start_state_ = new_value;
      }
    }
  }

 protected:
  float last_start_state_{NAN};
  float last_end_state_{NAN};
  lv_obj_t *meter_{};
  lv_meter_indicator_t *indicator_{};
  value_lambda_t start_value_{};
  value_lambda_t end_value_{};
};
#endif  // LV_USE_METER

#if LV_USE_FONT
class FontEngine {
 public:
  FontEngine(font::Font *esp_font) : font_(esp_font) {
    this->lv_font_.base_line = esp_font->get_baseline();
    this->lv_font_.line_height = esp_font->get_height();
    this->lv_font_.get_glyph_dsc = get_glyph_dsc_cb;
    this->lv_font_.get_glyph_bitmap = get_glyph_bitmap;
    this->lv_font_.dsc = this;
    this->lv_font_.subpx = LV_FONT_SUBPX_NONE;
    this->lv_font_.underline_position = -1;
    this->lv_font_.underline_thickness = 1;
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
    dsc->ofs_y = gd->offset_y;
    dsc->box_w = gd->width;
    dsc->box_h = gd->height;
    dsc->is_placeholder = 0;
    dsc->bpp = 1;
    // esph_log_d(TAG, "Returning dsc x/y %d/%d w/h %d/%d", dsc->ofs_x, dsc->ofs_y, dsc->box_w, dsc->box_h);
    return true;
  }

  static const uint8_t *get_glyph_bitmap(const lv_font_t *font, uint32_t unicode_letter) {
    FontEngine *fe = (FontEngine *) font->dsc;
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

  const font::GlyphData *get_glyph_data(uint32_t unicode_letter) {
    if (unicode_letter == last_letter_)
      return this->last_data_;
    char unicode[4];
    if (unicode_letter > 0x7FF) {
      unicode[0] = 0xE0 + ((unicode_letter >> 12) & 0xF);
      unicode[1] = 0x80 + ((unicode_letter >> 6) & 0x3F);
      unicode[2] = 0x80 + (unicode_letter & 0x3F);
    } else if (unicode_letter > 0x7F) {
      unicode[0] = 0xC0 + ((unicode_letter >> 6) & 0x1F);
      unicode[1] = 0x80 + (unicode_letter & 0x3F);
    } else
      unicode[0] = unicode_letter & 0x7F;
    int match_length;
    int glyph_n = this->font_->match_next_glyph(unicode, &match_length);
    if (glyph_n < 0)
      return nullptr;
    this->last_data_ = this->font_->get_glyphs()[glyph_n].get_glyph_data();
    this->last_letter_ = unicode_letter;
    return this->last_data_;
  }
};
#endif  // LV_USE_FONT

#if LVGL_USES_IMAGE
static lv_img_dsc_t *lv_img_from(image::Image *src) {
  auto img = new lv_img_dsc_t();  // NOLINT
  img->header.always_zero = 0;
  img->header.reserved = 0;
  img->header.w = src->get_width();
  img->header.h = src->get_height();
  img->data = src->get_data_start();
  img->data_size = image::image_type_to_width_stride(img->header.w * img->header.h, src->get_type());
  switch (src->get_type()) {
    case image::IMAGE_TYPE_BINARY:
      img->header.cf = LV_IMG_CF_ALPHA_1BIT;
      break;

    case image::IMAGE_TYPE_GRAYSCALE:
      img->header.cf = LV_IMG_CF_ALPHA_8BIT;
      break;

    case image::IMAGE_TYPE_RGB24:
      img->header.cf = LV_IMG_CF_RGB888;
      break;

    case image::IMAGE_TYPE_RGB565:
#if LV_COLOR_DEPTH == 16
      img->header.cf = LV_IMG_CF_TRUE_COLOR;
#else
      img->header.cf = LV_IMG_CF_RGB565;
#endif
      break;

    case image::IMAGE_TYPE_RGBA:
#if LV_COLOR_DEPTH == 32
      img->header.cf = LV_IMG_CF_TRUE_COLOR;
#else
      img->header.cf = LV_IMG_CF_RGBA8888;
#endif
      break;
  }
  return img;
}
#endif

class LvglComponent : public PollingComponent {
 public:
  static void static_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    reinterpret_cast<LvglComponent *>(disp_drv->user_data)->flush_cb_(disp_drv, area, color_p);
  }

  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  static void log_cb(const char *buf) { esp_log_printf_(ESPHOME_LOG_LEVEL_INFO, TAG, 0, "%s", buf); }

  void add_updater(Updater *updater) { this->updaters_.push_back(updater); }

  void setup() override {
    esph_log_config(TAG, "LVGL Setup starts");
    lv_log_register_print_cb(log_cb);
    size_t bytes_per_pixel = LV_COLOR_DEPTH / 8;
    size_t buffer_pixels = this->displays_[0]->get_width() * this->displays_[0]->get_height() / this->buffer_frac_;
    auto buf = lv_custom_mem_alloc(buffer_pixels * bytes_per_pixel);
    if (buf == nullptr) {
      esph_log_e(TAG, "Malloc failed to allocate %d bytes", buffer_pixels * bytes_per_pixel);
      this->mark_failed();
      return;
    }
    lv_disp_draw_buf_init(&this->draw_buf_, buf, nullptr, buffer_pixels);
    lv_disp_drv_init(&this->disp_drv_);
    this->disp_drv_.hor_res = this->displays_[0]->get_width();
    this->disp_drv_.ver_res = this->displays_[0]->get_height();
    this->disp_drv_.draw_buf = &this->draw_buf_;
    this->disp_drv_.user_data = this;
    this->disp_drv_.flush_cb = static_flush_cb;
    this->disp_ = lv_disp_drv_register(&this->disp_drv_);
    this->custom_change_event_ = (lv_event_code_t) lv_event_register_id();
    for (auto v : this->init_lambdas_)
      v(this->disp_);
    // this->display_->set_writer([](display::Display &d) { lv_timer_handler(); });
    esph_log_config(TAG, "LVGL Setup complete");
  }

  void update() override {
    // update indicators
    if (this->paused_)
      return;
    for (auto updater : this->updaters_) {
      updater->update();
    }
    this->idle_callbacks_.call(lv_disp_get_inactive_time(this->disp_));
  }

  void loop() override {
    if (this->paused_)
      return;
    lv_timer_handler_run_in_period(5);
  }

  void add_on_idle_callback(std::function<void(uint32_t)> &&callback) {
    this->idle_callbacks_.add(std::move(callback));
  }

  void add_display(display::Display *display) { this->displays_.push_back(display); }
  void add_init_lambda(std::function<void(lv_disp_t *)> lamb) { this->init_lambdas_.push_back(lamb); }
  void dump_config() override { esph_log_config(TAG, "LVGL:"); }
  lv_event_code_t get_custom_change_event() { return this->custom_change_event_; }
  void set_paused(bool paused) {
    this->paused_ = paused;
    if (!paused)
      lv_disp_trig_activity(this->disp_);  // resets the inactivity time
  }
  bool is_paused() { return this->paused_; }
  bool is_idle(uint32_t idle_ms) { return lv_disp_get_inactive_time(this->disp_) > idle_ms; }
  void set_buffer_frac(size_t frac) { this->buffer_frac_ = frac; }

 protected:
  void flush_cb_(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    auto now = millis();
    for (auto display : this->displays_) {
      display->draw_pixels_at(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area),
                              (const uint8_t *) color_p, display::COLOR_ORDER_RGB, LV_BITNESS, LV_COLOR_16_SWAP);
    }
    lv_disp_flush_ready(disp_drv);
    esph_log_v(TAG, "flush_cb, area=%d/%d, %d/%d took %dms", area->x1, area->y1, lv_area_get_width(area),
               lv_area_get_height(area), (int) (millis() - now));
  }

  std::vector<display::Display *> displays_{};
  lv_disp_draw_buf_t draw_buf_{};
  lv_disp_drv_t disp_drv_{};
  lv_disp_t *disp_{};
  lv_event_code_t custom_change_event_{};

  CallbackManager<void(uint32_t)> idle_callbacks_{};
  std::vector<std::function<void(lv_disp_t *)>> init_lambdas_;
  std::vector<Updater *> updaters_;
  size_t buffer_frac_{1};
  bool paused_{};
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

#if LV_USE_TOUCHSCREEN
class LVTouchListener : public touchscreen::TouchListener, public Parented<LvglComponent> {
 public:
  LVTouchListener() {
    lv_indev_drv_init(&this->drv);
    this->drv.type = LV_INDEV_TYPE_POINTER;
    this->drv.user_data = this;
    this->drv.read_cb = [](lv_indev_drv_t *d, lv_indev_data_t *data) {
      LVTouchListener *l = (LVTouchListener *) d->user_data;
      if (l->touch_pressed_) {
        data->point.x = l->touch_point_.x;
        data->point.y = l->touch_point_.y;
        data->state = LV_INDEV_STATE_PRESSED;
      } else {
        data->state = LV_INDEV_STATE_RELEASED;
      }
    };
  }
  void update(const touchscreen::TouchPoints_t &tpoints) override {
    this->touch_pressed_ = !this->parent_->is_paused() && !tpoints.empty();
    if (this->touch_pressed_)
      this->touch_point_ = tpoints[0];
  }
  void release() override { touch_pressed_ = false; }
  void touch_cb(lv_indev_data_t *data) {}

  lv_indev_drv_t drv{};

 protected:
  touchscreen::TouchPoint touch_point_{};
  bool touch_pressed_{};
};
#endif

#if LV_USE_ROTARY_ENCODER
class LVRotaryEncoderListener : public Parented<LvglComponent> {
 public:
  LVRotaryEncoderListener() {
    lv_indev_drv_init(&this->drv);
    this->drv.type = LV_INDEV_TYPE_ENCODER;
    this->drv.user_data = this;
    this->drv.read_cb = [](lv_indev_drv_t *d, lv_indev_data_t *data) {
      LVRotaryEncoderListener *l = (LVRotaryEncoderListener *) d->user_data;
      data->state = l->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      data->continue_reading = false;
      data->enc_diff = l->count_ - l->last_count_;
      l->last_count_ = l->count_;
    };
  }

  void set_count(int32_t count) { this->count_ = count; }
  void set_pressed(bool pressed) { this->pressed_ = pressed && !this->parent_->is_paused(); }
  lv_indev_drv_t drv{};

 protected:
  bool pressed_{};
  int32_t count_{};
  int32_t last_count_{};
  binary_sensor::BinarySensor *binary_sensor_{};
  sensor::Sensor *sensor_{};
};
#endif

}  // namespace lvgl
}  // namespace esphome
