#pragma once

#include "esphome/components/display/display_buffer.h"
#if LVGL_USES_IMAGE
#include "esphome/components/image/image.h"
#endif
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
typedef lv_style_t LvglStyle;
typedef lv_point_t LvglPoint;
typedef std::function<float(void)> value_lambda_t;
typedef std::function<const char *(void)> text_lambda_t;

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

class Label : public Updater {
 public:
  Label(lv_obj_t *label, text_lambda_t value) : label_(label), value_(value) {}

  void update() override {
    const char * t = this->value_();
    if (this->data_ != nullptr && strcmp(t, this->data_) == 0)
      return;
    // this jiggery-pokery seems necessary - in theory using set_text() should make the label copy the data
    // internally, but in practice this does not seem to work right.
    if (this->data_len_ <= strlen(t)) {
      this->data_len_ = strlen(t) + 10;
      this->data_ = (char *)realloc(this->data_, this->data_len_);
    }
    strcpy(this->data_, t);
    lv_label_set_text_static(this->label_, this->data_);
  }

 protected:
  lv_obj_t *label_{};
  text_lambda_t value_{};
  char * data_{};
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
        esph_log_v(TAG, "new value %f", new_value);
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
    size_t buf_size = this->display_->get_width() * this->display_->get_height() / 4;
    auto buf = lv_custom_mem_alloc(buf_size * LV_COLOR_DEPTH / 8);
    if (buf == nullptr) {
      esph_log_e(TAG, "Malloc failed to allocate %d bytes", buf_size);
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
    // this->display_->set_writer([](display::Display &d) { lv_timer_handler(); });
    esph_log_config(TAG, "LVGL Setup complete");
  }

  void update() override {
    // update indicators
    for (auto updater : this->updaters_) {
      updater->update();
    }
  }

  void loop() override { lv_timer_handler_run_in_period(5); }

  void set_display(display::DisplayBuffer *display) { display_ = display; }
  void set_init_lambda(std::function<void(lv_disp_t *)> lamb) { init_lambda_ = lamb; }
  void dump_config() override { ESP_LOGCONFIG(TAG, "LVGL:"); }

 protected:
  void flush_cb_(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    auto now = millis();
    this->display_->draw_pixels_at(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area),
                                   (const uint8_t *) color_p, display::COLOR_ORDER_RGB, LV_BITNESS, LV_COLOR_16_SWAP);
    lv_disp_flush_ready(disp_drv);
    esph_log_v(TAG, "flush_cb, area=%d/%d, %d/%d took %dms", area->x1, area->y1, lv_area_get_width(area),
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
