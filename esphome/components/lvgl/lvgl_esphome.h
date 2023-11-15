#pragma once

#include "esphome/components/display/display_buffer.h"
#if LVGL_USES_IMAGE
#include "esphome/components/image/image.h"
#endif
#include "esphome/components/font/font.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "lvgl_hal.h"
#include <lvgl.h>
#include <vector>

namespace esphome {
namespace lvgl {

static const char *const TAG = "lvgl";
#if LV_COLOR_DEPTH == 16
static const display::ColorBitness LV_BITNESS = display::COLOR_BITNESS_565;
#elif LV_COLOR_DEPTH == 32
static const display::ColorBitness LV_BITNESS = display::COLOR_BITNESS_A888;
#else
static const display::ColorBitness LV_BITNESS = display::COLOR_BITNESS_332;
#endif

typedef lv_obj_t LvglObj;
typedef std::function<float(void)> value_lambda_t;

class Updater {
 public:
  virtual void update() = 0;
};

template<typename... Ts> class ObjModifyAction : public Action<Ts...> {
 public:
  explicit ObjModifyAction(lv_obj_t *obj) : obj_(obj) {}

  void play(Ts... x) override { this->lamb_(this->obj_); }

 protected:
  lv_obj_t *obj_;
  std::function<void(lv_obj_t *)> lamb_;
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

class Indicator : public Updater {
 public:
  Indicator(lv_obj_t *meter, lv_meter_indicator_t *indicator, value_lambda_t start_value, value_lambda_t end_value)
      : meter_(meter), indicator_(indicator), start_value_(start_value), end_value_(end_value) {}

 public:
  void update() override {
    float new_value;
    if (this->end_value_ != nullptr) {
      new_value = this->end_value_();
      if (new_value != this->last_end_state_) {
        lv_meter_set_indicator_end_value(this->meter_, this->indicator_, new_value);
        this->last_end_state_ = new_value;
      }
      if (this->start_value_ != nullptr) {
        new_value = this->start_value_();
        if (new_value != this->last_start_state_) {
          lv_meter_set_indicator_start_value(this->meter_, this->indicator_, this->start_value_());
          this->last_start_state_ = new_value;
        }
      }
    } else if (this->start_value_ != nullptr) {
      new_value = this->start_value_();
      if (new_value != this->last_start_state_) {
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

static lv_color_t lv_color_from(Color color) { return lv_color_make(color.red, color.green, color.blue); }

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
  // esph_log_d(TAG, "Image type %d, width %d, height %d, length %d", img->header.cf, img->header.w, img->header.h,
  //           img->data_size);
  return img;
}
#endif

class LvglComponent : public Component {
 public:
  static void static_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    reinterpret_cast<LvglComponent *>(disp_drv->user_data)->flush_cb_(disp_drv, area, color_p);
  }

  static void log_cb(const char *buf) { esp_log_printf_(ESPHOME_LOG_LEVEL_INFO, TAG, 0, "%s", buf); }

  void add_updater(Updater *updater) { this->updaters_.push_back(updater); }

  void setup() override {
    lv_log_register_print_cb(log_cb);
    size_t buf_size = this->display_->get_width() * this->display_->get_height() / 4;
    auto buf = lv_custom_mem_alloc(buf_size * LV_COLOR_DEPTH / 8);
    if (buf == nullptr) {
      ESP_LOGE(TAG, "Malloc failed to allocate %d bytes", buf_size);
      this->mark_failed();
      return;
    }
    lv_disp_draw_buf_init(&this->draw_buf_, buf, nullptr, buf_size);
    lv_disp_drv_init(&this->disp_drv_);
    this->disp_drv_.hor_res = this->display_->get_width();
    this->disp_drv_.ver_res = this->display_->get_height();
    this->disp_drv_.draw_buf = &this->draw_buf_;
    this->disp_drv_.user_data = this;
    this->disp_drv_.flush_cb = static_flush_cb;
    this->disp_ = lv_disp_drv_register(&this->disp_drv_);
    this->init_lambda_(this->disp_);
    this->display_->set_writer([](display::Display &d) {lv_timer_handler(); });
  }

  void loop() override {
    // update indicators
    for (auto updater : this->updaters_) {
      updater->update();
    }
  }

  void set_display(display::DisplayBuffer *display) { display_ = display; }
  void set_init_lambda(std::function<void(lv_disp_t *)> lamb) { init_lambda_ = lamb; }
  void dump_config() override { ESP_LOGCONFIG(TAG, "LVGL:"); }

 protected:
  void flush_cb_(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    auto now = millis();
    this->display_->draw_pixels_at(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area),
                                          (const uint8_t *) color_p, display::COLOR_ORDER_RGB, LV_BITNESS,
                                          LV_COLOR_16_SWAP);
    lv_disp_flush_ready(disp_drv);
    ESP_LOGD(TAG, "flush_cb, area=%d/%d, %d/%d took %dms", area->x1, area->y1, lv_area_get_width(area),
             lv_area_get_height(area), (int) (millis() - now));
  }

  display::DisplayBuffer *display_{};
  lv_disp_draw_buf_t draw_buf_{};
  lv_disp_drv_t disp_drv_{};
  lv_disp_t *disp_{};
  std::function<void(lv_disp_t *)> init_lambda_;
  std::vector<Updater *> updaters_;
};

}  // namespace lvgl
}  // namespace esphome
