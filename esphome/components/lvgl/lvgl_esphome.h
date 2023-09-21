#pragma once

#include "../display/display_buffer.h"
#include "../../core/component.h"
#if !LV_CONF_SKIP
#include "../../../.pio/libdeps/esp32-idf/lvgl/src/lvgl.h"
#endif
#include "../../core/log.h"
#include "../../core/helpers.h"
#include "lvgl_hal.h"
#include <lvgl.h>


namespace esphome {
namespace lvgl {

static const char *const TAG = "lvgl";

typedef lv_obj_t LvglObj;

static lv_color_t lv_color_from(Color color) {
  return lv_color_make(color.red, color.green, color.blue);
}

class LvglComponent : public Component {
 public:
  static void static_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    reinterpret_cast<LvglComponent *>(disp_drv->user_data)->flush_cb_(disp_drv, area, color_p);
  }

  void setup() override {
    size_t buf_size = this->display_->get_width() * this->display_->get_height() / 4;
    auto buf = lv_custom_mem_alloc(buf_size);
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
    this->display_->set_writer([](display::Display &d) { lv_timer_handler(); });
  }

  void set_display(display::DisplayBuffer *display) { display_ = display; }
  void set_init_lambda(std::function<void(lv_disp_t *)>lamb) { init_lambda_ = lamb; }
  void dump_config() override { ESP_LOGCONFIG(TAG, "LVGL:"); }

 protected:
  void flush_cb_(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    auto now = millis();
    for (auto y = area->y1; y <= area->y2; y++) {
      for (auto x = area->x1; x <= area->x2; x++) {
        auto color = lv_color_to32(*color_p++);
        this->display_->draw_pixel_at(x, y, Color(color));
      }
    }
    lv_disp_flush_ready(disp_drv);
    ESP_LOGD(TAG, "flush_cb, area=%d/%d, %d/%d took %dms", area->x1, area->y1, area->x2, area->y2, (int)(millis()-now));
  }


  display::DisplayBuffer *display_{};
  lv_disp_draw_buf_t draw_buf_{};
  lv_disp_drv_t disp_drv_{};
  lv_disp_t *disp_{};
  std::function<void(lv_disp_t *)> init_lambda_;

};

}  // namespace lvgl
}  // namespace esphome
