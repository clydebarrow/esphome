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

class LvglComponent : public Component {
 public:
  static void static_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    reinterpret_cast<LvglComponent *>(disp_drv->user_data)->flush_cb_(disp_drv, area, color_p);
  }

  void setup() override {
    lv_init();
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
    this->display_->set_writer([this](display::Display &d) { lv_timer_handler(); });

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
    static lv_point_t line_points[] = {{5, 5}, {70, 70}, {120, 10}, {180, 60}, {240, 10}};
    ESP_LOGI(TAG, "adding line");
    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 8);
    lv_style_set_line_color(&style_line, lv_color_make(0, 0, 0xff));
    lv_style_set_line_rounded(&style_line, true);

    lv_obj_t *line1;
    line1 = lv_line_create(lv_scr_act());
    lv_line_set_points(line1, line_points, 5); /*Set the points*/
    lv_obj_add_style(line1, &style_line, 0);
    lv_obj_center(line1);
  }

  void set_display(display::DisplayBuffer *display) { display_ = display; }
  void dump_config() override { ESP_LOGCONFIG(TAG, "LVGL:"); }

 protected:
  void flush_cb_(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    ESP_LOGD(TAG, "flush_cb, area=%d/%d, %d/%d", area->x1, area->y1, area->x2, area->y2);
    for (auto y = area->y1; y <= area->y2; y++) {
      for (auto x = area->x1; x <= area->x2; x++) {
        auto color = lv_color_to32(*color_p++);
        this->display_->draw_pixel_at(x, y, Color(color));
      }
    }
    lv_disp_flush_ready(disp_drv);
  }

  display::DisplayBuffer *display_{};
  lv_disp_draw_buf_t draw_buf_{};
  lv_disp_drv_t disp_drv_{};
  lv_disp_t *disp_{};
  uint32_t last_millis_{};
};

}  // namespace lvgl
}  // namespace esphome
