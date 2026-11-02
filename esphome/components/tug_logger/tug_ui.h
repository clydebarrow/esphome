#pragma once

#include "esphome/core/defines.h"

#if defined(USE_LVGL) && defined(USE_ESP32)

#include <functional>
#include <string>

#include "esphome/components/lvgl/lvgl_esphome.h"

#include "tug_logger.h"

namespace esphome::tug_logger {

enum class TowField : uint8_t {
  TOW_FIELD_GLIDER,
  TOW_FIELD_PILOT_1,
  TOW_FIELD_PILOT_2,
  TOW_FIELD_TUG_PILOT,
};
static constexpr size_t TOW_FIELD_COUNT = 4;

/// The widgets of the pages in the configuration, which this fills in. All on the main loop.
struct TugUiWidgets {
  lvgl::LvglComponent *lvgl;
  lvgl::LvPageType *ground_page;
  lvgl::LvPageType *picker_page;
  lvgl::LvPageType *review_page;
  lvgl::LvPageType *tow_page;
  lvgl::LvPageType *settings_page;
  lv_obj_t *status;
  lv_obj_t *next_fields[TOW_FIELD_COUNT];
  lv_obj_t *review_count;
  lv_obj_t *flight_height;
  lv_obj_t *flight_elapsed;
  lv_obj_t *flight_info;
  lv_obj_t *picker_title;
  lv_obj_t *picker_text;
  lv_obj_t *picker_list;
  lv_obj_t *review_list;
  lv_obj_t *tow_info;
  lv_obj_t *tow_fields[TOW_FIELD_COUNT];
  lv_obj_t *tow_message;
  lv_obj_t *discard_label;
  lv_obj_t *settings_info;
  lv_obj_t *wifi_list;
};

class TugUi {
 public:
  void bind(TugLogger *logger, const TugUiWidgets &widgets);
  /// Updates what changes by itself; call once a second.
  void refresh();

  /// Edits a field of the next tow.
  void edit_next(TowField field);
  /// Edits a field of the tow open on the tow page.
  void edit_tow(TowField field);
  /// Accepts what is in the picker's text box.
  void picker_done();
  void picker_cancel();

  void show_review();
  void show_settings();
  void show_wifi_list();
  void confirm_tow();
  /// Asks for a second press before the tow is taken off the list.
  void discard_tow();
  void back_to_ground();

 protected:
  enum class PickerMode : uint8_t {
    PICKER_MODE_NEXT,
    PICKER_MODE_TOW,
    PICKER_MODE_WIFI_PASSWORD,
  };

  void show_(lvgl::LvPageType *page);
  void open_picker_(PickerMode mode, TowField field);
  void accept_(const std::string &value);
  void open_tow_(uint32_t id);
  void refresh_next_();
  void refresh_tow_();
  void set_status_();
  void add_row_(lv_obj_t *list, const std::string &text, std::function<void()> on_click,
                std::function<void()> on_long_press = nullptr);
  bool current_tow_(TowRecord &record);

  TugLogger *logger_{nullptr};
  TugUiWidgets w_{};
  PickerMode mode_{PickerMode::PICKER_MODE_NEXT};
  TowField field_{TowField::TOW_FIELD_GLIDER};
  uint32_t tow_id_{0};
  bool discard_armed_{false};
  std::string wifi_ssid_;
};

}  // namespace esphome::tug_logger

#endif
