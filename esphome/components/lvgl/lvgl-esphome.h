#pragma once

#include "../display/display_buffer.h"
#include "../../core/component.h"
#if !LV_CONF_SKIP
#include "../../../.pio/libdeps/esp32-idf/lvgl/src/lvgl.h"
#endif
#include "../../core/log.h"
#include <lvgl.h>

namespace esphome {
namespace lvgl {

static const char *const TAG = "lvgl";

typedef lv_obj_t LvglObj;

class LvglComponent : public PollingComponent {
 public:
  static void static_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    reinterpret_cast<LvglComponent *>(disp_drv->user_data)->flush_cb_(disp_drv, area, color_p);
  }

  void setup() override {
    return;
    lv_init();
    this->draw_buf_ = static_cast<lv_disp_draw_buf_t *>(lv_mem_alloc(10 * this->display_->get_width()));  // NOLINT
    lv_disp_drv_init(&this->disp_drv_);
    this->disp_drv_.hor_res = this->display_->get_width();
    this->disp_drv_.ver_res = this->display_->get_height();
    this->disp_drv_.draw_buf = this->draw_buf_;
    this->disp_drv_.user_data = this;
    this->disp_drv_.flush_cb = static_flush_cb;
    this->disp_ = lv_disp_drv_register(&this->disp_drv_);

    set_timeout(30000, [] {
      return;
      static lv_point_t line_points[] = {{5, 5}, {70, 70}, {120, 10}, {180, 60}, {240, 10}};
      ESP_LOGI(TAG, "addng line");
      static lv_style_t style_line;
      lv_style_init(&style_line);
      lv_style_set_line_width(&style_line, 8);
      lv_style_set_line_color(&style_line, lv_palette_main(LV_PALETTE_BLUE));
      lv_style_set_line_rounded(&style_line, true);


      lv_obj_t *line1;
      line1 = lv_line_create(lv_scr_act());
      lv_line_set_points(line1, line_points, 5); /*Set the points*/
      lv_obj_add_style(line1, &style_line, 0);
      lv_obj_center(line1);
    });
    //this->set_interval(10, [] { lv_tick_inc(10); });
  }

  void update() override { lv_timer_handler(); }

  void set_display(display::DisplayBuffer *display) { display_ = display; }

 protected:
  void flush_cb_(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    if (area->y2 >= area->y1)
      ESP_LOGI(TAG, "flush_cb called, %d/%d,%d/%d", area->x1, area->y1, area->x2, area->y2);
    lv_disp_flush_ready(disp_drv);
  }
  display::DisplayBuffer *display_{};
  lv_disp_draw_buf_t *draw_buf_;
  lv_disp_drv_t disp_drv_;
  lv_disp_t *disp_;
};

}  // namespace lvgl
}  // namespace esphome
