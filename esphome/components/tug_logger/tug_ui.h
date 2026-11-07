#pragma once

#include "esphome/core/defines.h"

#if defined(USE_LVGL) && defined(USE_ESP32)

#include <functional>
#include <string>

#include "esphome/components/lvgl/lvgl_esphome.h"

#include "tug_logger.h"

#ifdef USE_TUG_LOGGER_SKYECHO
#include "esphome/components/skyecho/traffic_widget.h"
#endif

namespace esphome::runtime_image {
class RuntimeImage;
}  // namespace esphome::runtime_image

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
  /// Holds the splash screen: the logo goes first, above splash_name
  lv_obj_t *splash_box;
  lv_obj_t *splash_name;
  lv_obj_t *shutdown_label;
  /// Height of a row in the lists
  int row_height;
  lv_obj_t *status;
  lv_obj_t *status_time;
  lv_obj_t *refuel_label;
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
  lv_obj_t *wifi_dialog;
  lv_obj_t *wifi_dialog_title;
  lv_obj_t *wifi_password_button;
  lv_obj_t *wifi_forget_button;
  lv_obj_t *firmware_label;
  lv_obj_t *picker_keyboard;
  lv_obj_t *theme_label;
#ifdef USE_TUG_LOGGER_SKYECHO
  skyecho::TrafficWidget *traffic;
#endif
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
  /// The dialog for the known network chosen in the Wi-Fi list.
  void wifi_connect();
  void wifi_change_password();
  void wifi_forget();
  void wifi_dialog_close();
  /// The firmware button in Settings: checks for an update, or returns true when one should be installed.
  bool firmware_button();
  /// The install could not complete.
  void firmware_install_failed();
  void confirm_tow();
  /// Asks for a second press before the tow is taken off the list.
  void discard_tow();
  void back_to_ground();
  /// Asks for a second press before the time since refuelling goes back to zero.
  void mark_refuelled();
  /// True on the second press in time: the board's configuration then shuts down.
  bool confirm_shutdown();
  /// Called as the picker's text changes.
  void picker_text_changed();
  /// Changes between the light and dark themes while running.
  void set_dark_mode(bool dark);
  bool dark_mode() const { return this->dark_mode_; }

 protected:
  enum class PickerMode : uint8_t {
    PICKER_MODE_NEXT,
    PICKER_MODE_TOW,
    PICKER_MODE_WIFI_PASSWORD,
  };

  void show_(lvgl::LvPageType *page);
  void refresh_splash_();
  void load_logo_();
  void open_picker_(PickerMode mode, TowField field);
  void accept_(const std::string &value);
  void open_tow_(uint32_t id);
  void refresh_next_();
  void refresh_tow_();
  void set_status_();
  void add_row_(lv_obj_t *list, const std::string &text, std::function<void()> on_click);
  void fill_wifi_list_();
  void refresh_settings_();
  std::string wifi_text_();
  void open_wifi_dialog_(bool saved, bool can_set_password);
  bool current_tow_(TowRecord &record);

  TugLogger *logger_{nullptr};
  TugUiWidgets w_{};
  PickerMode mode_{PickerMode::PICKER_MODE_NEXT};
  TowField field_{TowField::TOW_FIELD_GLIDER};
  uint32_t tow_id_{0};
  bool discard_armed_{false};
  uint8_t refuel_armed_ticks_{0};
  uint8_t shutdown_armed_ticks_{0};
  // Counts down while the splash shows; it stays up longer until the card has been read
  uint8_t splash_ticks_;
  bool splash_active_{true};
  bool splash_loaded_{false};
  runtime_image::RuntimeImage *logo_{nullptr};
  bool dark_mode_{false};
  uint8_t wifi_list_refresh_ticks_{0};
  std::string wifi_ssid_;
  // What the Wi-Fi list last showed, so it is filled again when the connection changes
  std::string wifi_shown_;
};

}  // namespace esphome::tug_logger

#endif
