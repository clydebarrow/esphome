#pragma once

#include "lvgl_esphome.h"
#include "esphome/core/color.h"

#ifdef USE_LVGL_COLOR_PICKER

namespace esphome {
namespace lvgl {

class LvColorPickerType : public LvCompound {
  constexpr static const char *const TAG = "lvgl.color_picker";

 public:
  void set_color(lv_color_t color) {
    this->color_ = color;
    if (this->obj != nullptr)
      this->update_color_();
  }
  Color get_color() {
    return {static_cast<uint8_t>(lv_slider_get_value(red_slider)),
            static_cast<uint8_t>(lv_slider_get_value(green_slider)),
            static_cast<uint8_t>(lv_slider_get_value(blue_slider))};
  }
  void set_obj(lv_obj_t *outer) override {
    LvCompound::set_obj(outer);
    color_bar.dir = LV_GRAD_DIR_VER;
    color_bar.dither = LV_DITHER_NONE;
    color_bar.stops_count = 7;
    color_bar.stops[0].color = lv_color_hex(0xFF0000);
    color_bar.stops[0].frac = 0;
    color_bar.stops[1].color = lv_color_hex(0xFF00FF);
    color_bar.stops[1].frac = 42;
    color_bar.stops[2].color = lv_color_hex(0xFF);
    color_bar.stops[2].frac = 84;
    color_bar.stops[3].color = lv_color_hex(0xFFFF);
    color_bar.stops[3].frac = 127;
    color_bar.stops[4].color = lv_color_hex(0xFF00);
    color_bar.stops[4].frac = 169;
    color_bar.stops[5].color = lv_color_hex(0xFFFF00);
    color_bar.stops[5].frac = 212;
    color_bar.stops[6].color = lv_color_hex(0xFF0000);
    color_bar.stops[6].frac = 255;
    brightness_bar.dir = LV_GRAD_DIR_HOR;
    brightness_bar.dither = LV_DITHER_NONE;
    brightness_bar.stops_count = 2;
    brightness_bar.stops[0].color = lv_color_hex(0x00);
    brightness_bar.stops[0].frac = 0;
    brightness_bar.stops[1].color = lv_color_hex(0xFFFFFF);
    brightness_bar.stops[1].frac = 255;
    saturation_bar.dir = LV_GRAD_DIR_HOR;
    saturation_bar.dither = LV_DITHER_NONE;
    saturation_bar.stops_count = 2;
    saturation_bar.stops[0].color = lv_color_hex(0xFFFFFF);
    saturation_bar.stops[0].frac = 0;
    saturation_bar.stops[1].color = lv_color_hex(0xFF0000);
    saturation_bar.stops[1].frac = 255;
    blue_bar.dir = LV_GRAD_DIR_VER;
    blue_bar.dither = LV_DITHER_NONE;
    blue_bar.stops_count = 2;
    blue_bar.stops[0].color = lv_color_hex(0xFF);
    blue_bar.stops[0].frac = 0;
    blue_bar.stops[1].color = lv_color_hex(0xFFFFFF);
    blue_bar.stops[1].frac = 255;
    green_bar.dir = LV_GRAD_DIR_VER;
    green_bar.dither = LV_DITHER_NONE;
    green_bar.stops_count = 2;
    green_bar.stops[0].color = lv_color_hex(0xFF00);
    green_bar.stops[0].frac = 0;
    green_bar.stops[1].color = lv_color_hex(0xFFFFFF);
    green_bar.stops[1].frac = 255;
    red_bar.dir = LV_GRAD_DIR_VER;
    red_bar.dither = LV_DITHER_NONE;
    red_bar.stops_count = 2;
    red_bar.stops[0].color = lv_color_hex(0xFF0000);
    red_bar.stops[0].frac = 0;
    red_bar.stops[1].color = lv_color_hex(0xFFFFFF);
    red_bar.stops[1].frac = 255;

    lv_style_init(&slider_vert);
    lv_style_set_align(&slider_vert, LV_ALIGN_CENTER);
    lv_style_set_bg_opa(&slider_vert, LV_OPA_COVER);
    lv_style_set_height(&slider_vert, lv_pct(90));
    lv_style_set_pad_all(&slider_vert, 0);
    lv_style_set_radius(&slider_vert, 0);
    lv_style_set_width(&slider_vert, lv_pct(33));
    lv_style_init(&slider_vert_knob);
    lv_style_set_bg_color(&slider_vert_knob, lv_color_hex(0xFFFFFF));
    lv_style_set_border_width(&slider_vert_knob, 1);
    lv_style_set_outline_color(&slider_vert_knob, lv_color_hex(0x00));
    lv_style_set_pad_bottom(&slider_vert_knob, -4);
    lv_style_set_pad_top(&slider_vert_knob, -4);
    lv_style_set_radius(&slider_vert_knob, 0);
    lv_style_init(&slider_horz);
    lv_style_set_align(&slider_horz, LV_ALIGN_CENTER);
    lv_style_set_bg_opa(&slider_horz, LV_OPA_COVER);
    lv_style_set_height(&slider_horz, lv_pct(33));
    lv_style_set_pad_all(&slider_horz, 0);
    lv_style_set_radius(&slider_horz, 0);
    lv_style_set_width(&slider_horz, lv_pct(90));
    lv_style_init(&slider_horz_knob);
    lv_style_set_bg_color(&slider_horz_knob, lv_color_hex(0xFFFFFF));
    lv_style_set_border_width(&slider_horz_knob, 1);
    lv_style_set_outline_color(&slider_horz_knob, lv_color_hex(0x00));
    lv_style_set_pad_left(&slider_horz_knob, -4);
    lv_style_set_pad_right(&slider_horz_knob, -4);
    lv_style_set_radius(&slider_horz_knob, 0);

    lv_obj_set_layout(outer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(outer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(outer, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(outer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(outer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(outer, 0, LV_PART_MAIN);
    lv_obj_set_style_width(outer, 400, LV_PART_MAIN);

    auto *hue_container = lv_obj_create(outer);
    lv_obj_set_style_border_width(hue_container, 0, LV_PART_MAIN);
    lv_obj_set_style_height(hue_container, lv_pct(100), LV_PART_MAIN);
    lv_obj_set_style_pad_all(hue_container, 0, LV_PART_MAIN);
    lv_obj_set_style_width(hue_container, lv_pct(15), LV_PART_MAIN);

    hue_slider_ = lv_slider_create(hue_container);
    lv_obj_add_style(hue_slider_, &slider_vert, LV_PART_MAIN);
    lv_obj_set_style_align(hue_slider_, LV_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad(hue_slider_, &color_bar, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hue_slider_, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_style(hue_slider_, &slider_vert_knob, LV_PART_KNOB);
    lv_slider_set_range(hue_slider_, 0, 360);
    lv_slider_set_mode(hue_slider_, LV_BAR_MODE_NORMAL);
    auto *middle = lv_obj_create(outer);
    lv_obj_set_layout(middle, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(middle, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(middle, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_border_width(middle, 0, LV_PART_MAIN);
    lv_obj_set_style_height(middle, lv_pct(100), LV_PART_MAIN);
    lv_obj_set_style_pad_all(middle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(middle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(middle, 0, LV_PART_MAIN);
    lv_obj_set_style_width(middle, lv_pct(85), LV_PART_MAIN);

    auto *brightness_container = lv_obj_create(middle);
    lv_obj_set_style_border_width(brightness_container, 0, LV_PART_MAIN);
    lv_obj_set_style_height(brightness_container, lv_pct(15), LV_PART_MAIN);
    lv_obj_set_style_pad_all(brightness_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(brightness_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(brightness_container, 0, LV_PART_MAIN);
    lv_obj_set_style_width(brightness_container, lv_pct(100), LV_PART_MAIN);

    brightness_slider_ = lv_slider_create(brightness_container);
    lv_obj_add_style(brightness_slider_, &slider_horz, LV_PART_MAIN);
    lv_obj_set_style_bg_grad(brightness_slider_, &brightness_bar, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(brightness_slider_, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_style(brightness_slider_, &slider_horz_knob, LV_PART_KNOB);
    lv_slider_set_range(brightness_slider_, 0, 100);
    lv_slider_set_mode(brightness_slider_, LV_BAR_MODE_NORMAL);

    auto *inner = lv_obj_create(middle);
    lv_obj_set_layout(inner, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(inner, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(inner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_border_width(inner, 0, LV_PART_MAIN);
    lv_obj_set_style_height(inner, lv_pct(70), LV_PART_MAIN);
    lv_obj_set_style_pad_all(inner, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(inner, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(inner, 0, LV_PART_MAIN);
    lv_obj_set_style_width(inner, lv_pct(100), LV_PART_MAIN);

    auto *indicator_container = lv_obj_create(inner);
    lv_obj_set_style_border_width(indicator_container, 0, LV_PART_MAIN);
    lv_obj_set_style_height(indicator_container, lv_pct(100), LV_PART_MAIN);
    lv_obj_set_style_pad_all(indicator_container, 0, LV_PART_MAIN);
    lv_obj_set_style_width(indicator_container, lv_pct(42), LV_PART_MAIN);

    color_text = lv_label_create(indicator_container);
    lv_obj_set_style_align(color_text, LV_ALIGN_TOP_MID, LV_PART_MAIN);
    lv_obj_set_style_text_align(color_text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_y(color_text, lv_pct(10), LV_PART_MAIN);
    lv_label_set_text(color_text, "#working");

    color_indicator = lv_obj_create(indicator_container);
    lv_obj_set_style_align(color_indicator, LV_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(color_indicator, lv_color_hex(0xFFFF00), LV_PART_MAIN);
    lv_obj_set_style_border_color(color_indicator, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_set_style_border_width(color_indicator, 1, LV_PART_MAIN);
    lv_obj_set_style_height(color_indicator, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_radius(color_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_width(color_indicator, lv_pct(90), LV_PART_MAIN);

    auto *rgb_container = lv_obj_create(inner);
    lv_obj_set_layout(rgb_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rgb_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rgb_container, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_border_width(rgb_container, 0, LV_PART_MAIN);
    lv_obj_set_style_height(rgb_container, lv_pct(100), LV_PART_MAIN);
    lv_obj_set_style_pad_all(rgb_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(rgb_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(rgb_container, 0, LV_PART_MAIN);
    lv_obj_set_style_width(rgb_container, lv_pct(57), LV_PART_MAIN);

    auto *red_container = lv_obj_create(rgb_container);
    lv_obj_set_style_border_width(red_container, 0, LV_PART_MAIN);
    lv_obj_set_style_height(red_container, lv_pct(100), LV_PART_MAIN);
    lv_obj_set_style_pad_all(red_container, 0, LV_PART_MAIN);
    lv_obj_set_style_width(red_container, lv_pct(33), LV_PART_MAIN);

    red_slider = lv_slider_create(red_container);
    lv_obj_add_style(red_slider, &slider_vert, LV_PART_MAIN);
    lv_obj_set_style_bg_grad(red_slider, &red_bar, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(red_slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_style(red_slider, &slider_vert_knob, LV_PART_KNOB);
    lv_slider_set_range(red_slider, 0, 255);
    lv_slider_set_mode(red_slider, LV_BAR_MODE_NORMAL);

    auto *green_container = lv_obj_create(rgb_container);
    lv_obj_set_style_border_width(green_container, 0, LV_PART_MAIN);
    lv_obj_set_style_height(green_container, lv_pct(100), LV_PART_MAIN);
    lv_obj_set_style_pad_all(green_container, 0, LV_PART_MAIN);
    lv_obj_set_style_width(green_container, lv_pct(33), LV_PART_MAIN);

    green_slider = lv_slider_create(green_container);
    lv_obj_add_style(green_slider, &slider_vert, LV_PART_MAIN);
    lv_obj_set_style_bg_grad(green_slider, &green_bar, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(green_slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_style(green_slider, &slider_vert_knob, LV_PART_KNOB);
    lv_slider_set_range(green_slider, 0, 255);
    lv_slider_set_mode(green_slider, LV_BAR_MODE_NORMAL);

    auto *blue_container = lv_obj_create(rgb_container);
    lv_obj_set_style_border_width(blue_container, 0, LV_PART_MAIN);
    lv_obj_set_style_height(blue_container, lv_pct(100), LV_PART_MAIN);
    lv_obj_set_style_pad_all(blue_container, 0, LV_PART_MAIN);
    lv_obj_set_style_width(blue_container, lv_pct(33), LV_PART_MAIN);

    blue_slider = lv_slider_create(blue_container);
    lv_obj_add_style(blue_slider, &slider_vert, LV_PART_MAIN);
    lv_obj_set_style_bg_grad(blue_slider, &blue_bar, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(blue_slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_style(blue_slider, &slider_vert_knob, LV_PART_KNOB);
    lv_slider_set_range(blue_slider, 0, 255);
    lv_slider_set_mode(blue_slider, LV_BAR_MODE_NORMAL);

    auto *saturation_container = lv_obj_create(middle);
    lv_obj_set_style_border_width(saturation_container, 0, LV_PART_MAIN);
    lv_obj_set_style_height(saturation_container, lv_pct(15), LV_PART_MAIN);
    lv_obj_set_style_pad_all(saturation_container, 0, LV_PART_MAIN);
    lv_obj_set_style_width(saturation_container, lv_pct(100), LV_PART_MAIN);

    saturation_slider = lv_slider_create(saturation_container);
    lv_obj_add_style(saturation_slider, &slider_horz, LV_PART_MAIN);
    lv_obj_set_style_bg_grad(saturation_slider, &saturation_bar, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(saturation_slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_style(saturation_slider, &slider_horz_knob, LV_PART_KNOB);
    lv_slider_set_range(saturation_slider, 0, 100);
    lv_slider_set_mode(saturation_slider, LV_BAR_MODE_NORMAL);
    this->update_color_();

    auto rgb_lambda = [](lv_event_t *event) {
      auto *self = static_cast<LvColorPickerType *>(lv_event_get_user_data(event));
      self->update_rgb_();
    };
    lv_obj_add_event_cb(red_slider, rgb_lambda, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(green_slider, rgb_lambda, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(blue_slider, rgb_lambda, LV_EVENT_VALUE_CHANGED, this);

    auto hsl_lambda = [](lv_event_t *event) {
      auto *self = static_cast<LvColorPickerType *>(lv_event_get_user_data(event));
      self->update_hsl_();
    };
    lv_obj_add_event_cb(hue_slider_, hsl_lambda, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(saturation_slider, hsl_lambda, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(brightness_slider_, hsl_lambda, LV_EVENT_VALUE_CHANGED, this);
  }

 protected:
  void update_text_() const {
    auto str = str_sprintf("#%02X%02X%02X", lv_slider_get_value(red_slider), lv_slider_get_value(green_slider),
                           lv_slider_get_value(blue_slider));
    lv_label_set_text(color_text, str.c_str());
  }

  void update_saturation_bar_(uint16_t hue) {
    saturation_bar.stops[1].color = lv_color_hsv_to_rgb(hue, 100, 100);
    lv_obj_invalidate(saturation_slider);
  }

  void update_hsl_() {
    auto hue = lv_slider_get_value(hue_slider_);
    auto brightness = lv_slider_get_value(brightness_slider_);
    auto saturation = lv_slider_get_value(saturation_slider);
    this->color_ = lv_color_hsv_to_rgb(hue, saturation, brightness);
    lv_obj_set_style_bg_color(color_indicator, this->color_, LV_PART_MAIN);
    lv_color32_t c32;
    c32.full = lv_color_to32(this->color_);
    lv_slider_set_value(red_slider, c32.ch.red, LV_ANIM_OFF);
    lv_slider_set_value(green_slider, c32.ch.green, LV_ANIM_OFF);
    lv_slider_set_value(blue_slider, c32.ch.blue, LV_ANIM_OFF);
    this->update_saturation_bar_(hue);
    this->update_text_();
  }

  void update_rgb_() {
    auto red = lv_slider_get_value(red_slider);
    auto green = lv_slider_get_value(green_slider);
    auto blue = lv_slider_get_value(blue_slider);
    this->color_ = lv_color_make(red, green, blue);
    lv_obj_set_style_bg_color(color_indicator, this->color_, LV_PART_MAIN);
    auto hsv = lv_color_rgb_to_hsv(red, green, blue);
    lv_slider_set_value(hue_slider_, hsv.h, LV_ANIM_OFF);
    lv_slider_set_value(saturation_slider, hsv.s, LV_ANIM_OFF);
    lv_slider_set_value(brightness_slider_, hsv.v, LV_ANIM_OFF);
    this->update_saturation_bar_(hsv.h);
    this->update_text_();
  }

  void update_color_() {
    lv_color32_t c32;
    c32.full = lv_color_to32(this->color_);
    ESP_LOGD(TAG, "Updating color, color=%04x", c32.full);
    lv_slider_set_value(red_slider, c32.ch.red, LV_ANIM_OFF);
    lv_slider_set_value(green_slider, c32.ch.green, LV_ANIM_OFF);
    lv_slider_set_value(blue_slider, c32.ch.blue, LV_ANIM_OFF);
    this->update_rgb_();
    lv_obj_invalidate(this->obj);
  }

  lv_grad_dsc_t color_bar{};
  lv_grad_dsc_t brightness_bar{};
  lv_grad_dsc_t saturation_bar{};
  lv_grad_dsc_t blue_bar{};
  lv_grad_dsc_t green_bar{};
  lv_grad_dsc_t red_bar{};
  lv_style_t slider_vert{};
  lv_style_t slider_vert_knob{};
  lv_style_t slider_horz{};
  lv_style_t slider_horz_knob{};
  lv_obj_t *hue_slider_{};
  lv_obj_t *brightness_slider_{};
  lv_obj_t *saturation_slider{};
  lv_obj_t *red_slider{};
  lv_obj_t *blue_slider{};
  lv_obj_t *green_slider{};
  lv_obj_t *color_indicator{};
  lv_obj_t *color_text{};
  lv_color_t color_{lv_color_hex(0x808080)};
};

}  // namespace lvgl
}  // namespace esphome

#endif  // USE_LVGL_COLOR_PICKER