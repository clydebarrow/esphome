#include "tug_ui.h"

#if defined(USE_LVGL) && defined(USE_ESP32)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

#include "esphome/components/network/util.h"
#include "esphome/components/runtime_image/runtime_image.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::tug_logger {

ESPHOME_LOG_TAG(TAG, "tug_ui");

static constexpr uint32_t COLOR_NORMAL = 0x202020;
static constexpr uint32_t COLOR_WARNING = 0xB02020;
// The Wi-Fi component takes a command on its next loop, so the list is read again on the second tick after one
static constexpr uint8_t WIFI_LIST_REFRESH_TICKS = 2;
// The most of a network's name that the status bar shows
static constexpr size_t STATUS_SSID_LENGTH = 16;
// How long the Refuelled button waits for its second press
static constexpr uint8_t REFUEL_ARMED_TICKS = 5;
// How long the Shut down button waits for its second press
static constexpr uint8_t SHUTDOWN_ARMED_TICKS = 5;
// How long the splash shows, from when the card has been read, and at most if it never is
static constexpr uint8_t SPLASH_SHOW_TICKS = 6;
static constexpr uint8_t SPLASH_WAIT_TICKS = 12;
// Sized for the width of the display
static constexpr int SPLASH_LOGO_WIDTH_PERCENT = 80;
static constexpr size_t LOGO_MAX_BYTES = 1024 * 1024;

static const char *const FIELD_TITLES[TOW_FIELD_COUNT] = {"Glider", "Pilot 1", "Pilot 2", "Tug pilot"};

// A short formatted text on the stack; these run every second
struct Text {
  char text[192];
  explicit Text(const char *format, ...) __attribute__((format(printf, 2, 3))) {
    va_list args;
    va_start(args, format);
    vsnprintf(this->text, sizeof(this->text), format, args);
    va_end(args);
  }
};

static std::string &field_of(TowDetails &details, TowField field) {
  switch (field) {
    case TowField::TOW_FIELD_GLIDER:
      return details.glider_registration;
    case TowField::TOW_FIELD_PILOT_1:
      return details.glider_pilot_1;
    case TowField::TOW_FIELD_PILOT_2:
      return details.glider_pilot_2;
    default:
      return details.tug_pilot;
  }
}

// The value of a field: the buttons have the field's name in a label of its own
static const char *field_text(const TowDetails &details, TowField field) {
  const std::string &value = field_of(const_cast<TowDetails &>(details), field);
  return value.empty() ? "-" : value.c_str();
}

static std::string trimmed(const std::string &text) {
  const size_t start = text.find_first_not_of(" \t");
  if (start == std::string::npos)
    return "";
  return text.substr(start, text.find_last_not_of(" \t") - start + 1);
}

// Each row owns its callbacks, for as long as the row exists
struct RowActions {
  std::function<void()> on_click;
};

static void row_event(lv_event_t *event) {
  auto *actions = static_cast<RowActions *>(lv_event_get_user_data(event));
  switch (lv_event_get_code(event)) {
    case LV_EVENT_DELETE:
      delete actions;
      break;
    case LV_EVENT_CLICKED:
      if (actions->on_click)
        actions->on_click();
      break;
    default:
      break;
  }
}

void TugUi::add_row_(lv_obj_t *list, const std::string &text, std::function<void()> on_click) {
  lv_obj_t *button = lv_button_create(list);
  lv_obj_set_width(button, lv_pct(100));
  lv_obj_set_height(button, this->w_.row_height);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text.c_str());
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(button, this->dark_mode_ ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_center(label);
  auto *actions = new RowActions{std::move(on_click)};  // NOLINT
  lv_obj_add_event_cb(button, row_event, LV_EVENT_ALL, actions);
}

void TugUi::bind(TugLogger *logger, const TugUiWidgets &widgets) {
  this->logger_ = logger;
  this->w_ = widgets;
  lv_obj_add_event_cb(
      widgets.picker_text,
      [](lv_event_t *event) { static_cast<TugUi *>(lv_event_get_user_data(event))->picker_text_changed(); },
      LV_EVENT_VALUE_CHANGED, this);
  // The rows are added at run time, so the configuration cannot give these a layout
  for (lv_obj_t *list : {widgets.picker_list, widgets.review_list, widgets.wifi_list}) {
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
  }
  this->splash_ticks_ = SPLASH_WAIT_TICKS;
  this->refresh();
}

// Black on the buttons in dark mode, which the theme leaves white
static lv_obj_tree_walk_res_t set_button_text(lv_obj_t *obj, void *user_data) {
  if (lv_obj_check_type(obj, &lv_button_class))
    lv_obj_set_style_text_color(obj, *static_cast<lv_color_t *>(user_data), LV_PART_MAIN);
  return LV_OBJ_TREE_WALK_NEXT;
}

void TugUi::set_dark_mode(bool dark) {
  this->dark_mode_ = dark;
  lv_obj_t *screen = lv_screen_active();
  // Initialising the default theme again rebuilds its styles, and refreshes every object that uses them
  lv_theme_default_init(lv_obj_get_display(screen), lv_theme_get_color_primary(screen),
                        lv_theme_get_color_secondary(screen), dark, lv_theme_get_font_normal(screen));
#ifdef USE_TUG_LOGGER_SKYECHO
  if (this->w_.traffic != nullptr)
    this->w_.traffic->refresh_colors();
#endif
  lv_color_t button_text = dark ? lv_color_black() : lv_color_white();
  for (lvgl::LvPageType *page :
       {this->w_.ground_page, this->w_.picker_page, this->w_.review_page, this->w_.tow_page, this->w_.settings_page})
    lv_obj_tree_walk(page->obj, set_button_text, &button_text);
  lv_label_set_text(this->w_.theme_label, dark ? "Dark mode: on" : "Dark mode: off");
}

void TugUi::show_(lvgl::LvPageType *page) { this->w_.lvgl->show_page(page->index, LV_SCREEN_LOAD_ANIM_NONE, 0); }

void TugUi::set_status_() {
  auto *logger = this->logger_;
  // Three rows, which the label is sized for: the links, with the time beside them, then the day's counts, then
  // anything wrong
  lv_label_set_text(this->w_.status_time, logger->clock_text().c_str());
  std::string status;
  switch (logger->gps_source()) {
    case GpsSource::GPS_SOURCE_SKYECHO:
      status += "GPS SkyEcho";
      break;
    case GpsSource::GPS_SOURCE_NMEA:
      status += "GPS NMEA";
      break;
    default:
      status += "NO GPS";
      break;
  }
  // The network in use, by name; a long name is cut so the row still fits
  std::string ssid;
  bool joining = false;
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  if (auto *networks = logger->get_wifi_networks(); networks != nullptr) {
    const wifi_networks::Status wifi = networks->get_status();
    ssid = wifi.ssid;
    joining = wifi.state == wifi_networks::WifiState::WIFI_STATE_CONNECTING;
  }
#endif
  if (ssid.size() > STATUS_SSID_LENGTH)
    ssid = ssid.substr(0, STATUS_SSID_LENGTH - 1) + "~";
  if (network::is_connected()) {
    status += "   Wi-Fi: " + (ssid.empty() ? std::string(logger->on_skyecho_network() ? "SkyEcho" : "on") : ssid);
  } else {
    status += joining ? "   Wi-Fi: joining" : "   Wi-Fi: off";
  }
  const float battery = logger->battery_voltage();
  if (!std::isnan(battery))
    status += Text("   %.1f V", battery).text;
  const uint32_t flight = logger->flight_today_s(), fuel = logger->since_refuel_s();
  status += Text("\nTows %u   Flight %u:%02u   Fuel %u:%02u   Queued %zu", unsigned(logger->tows_today()),
                 unsigned(flight / 3600), unsigned(flight % 3600 / 60), unsigned(fuel / 3600),
                 unsigned(fuel % 3600 / 60), logger->queued_count())
                .text;
  std::string problems;
  if (!logger->card_ok()) {
    problems += "NO SD CARD   ";
  } else if (!logger->has_provisioning()) {
    problems += "NO TUG FILE   ";
  }
  if (logger->failed_count() != 0)
    problems += Text("%zu REFUSED   ", logger->failed_count()).text;
  if (logger->flight_data_lost())
    problems += "GPS LOST   ";
  const bool warning = !problems.empty();
  status += "\n" + problems;
  lv_label_set_text(this->w_.status, status.c_str());
  lv_obj_set_style_bg_color(this->w_.status, lv_color_hex(warning ? COLOR_WARNING : COLOR_NORMAL), LV_PART_MAIN);
}

void TugUi::refresh_next_() {
  for (size_t i = 0; i != TOW_FIELD_COUNT; i++)
    lv_label_set_text(this->w_.next_fields[i], field_text(this->logger_->next_details(), TowField(i)));
}

void TugUi::refresh() {
  if (this->logger_ == nullptr)
    return;
  auto *logger = this->logger_;
  if (this->splash_active_)
    this->refresh_splash_();
  this->set_status_();
  this->refresh_next_();
  lv_label_set_text(this->w_.review_count, Text("To review: %zu", logger->review_count()).text);
  if (logger->is_flying()) {
    const uint32_t elapsed = logger->flight_elapsed_s();
    lv_label_set_text(this->w_.flight_height, Text("%d ft", int(logger->current_height_ft())).text);
    lv_label_set_text(this->w_.flight_elapsed, Text("%u:%02u", unsigned(elapsed / 60), unsigned(elapsed % 60)).text);
    const std::string &glider = logger->flight_details().glider_registration;
    lv_label_set_text(this->w_.flight_info, glider.empty() ? "" : glider.c_str());
  }
  if (this->wifi_list_refresh_ticks_ != 0 && --this->wifi_list_refresh_ticks_ == 0)
    this->fill_wifi_list_();
  if (lv_screen_active() == this->w_.settings_page->obj)
    this->refresh_settings_();
  if (this->refuel_armed_ticks_ != 0 && --this->refuel_armed_ticks_ == 0)
    lv_label_set_text(this->w_.refuel_label, "Refuelled");
  if (this->shutdown_armed_ticks_ != 0 && --this->shutdown_armed_ticks_ == 0)
    lv_label_set_text(this->w_.shutdown_label, "Shut down");
}

void TugUi::refresh_splash_() {
  if (!this->splash_loaded_ && this->logger_->has_provisioning()) {
    this->splash_loaded_ = true;
    lv_label_set_text(this->w_.splash_name, this->logger_->provisioning().organisation.c_str());
    this->load_logo_();
    this->splash_ticks_ = SPLASH_SHOW_TICKS;
  }
  if (this->splash_ticks_ != 0 && --this->splash_ticks_ != 0 && !this->logger_->is_flying())
    return;
  this->splash_active_ = false;
  lv_obj_remove_flag(this->w_.status, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(this->w_.status_time, LV_OBJ_FLAG_HIDDEN);
  // A takeoff has already shown its page
  if (!this->logger_->is_flying())
    this->show_(this->w_.ground_page);
}

void TugUi::load_logo_() {
  const std::string path = this->logger_->card_path() + "/logo.png";
  FILE *file = fopen(path.c_str(), "rb");
  if (file == nullptr) {
    ESP_LOGI(TAG, "No %s for the splash screen", path.c_str());
    return;
  }
  fseek(file, 0, SEEK_END);
  const long length = ftell(file);
  rewind(file);
  RAMAllocator<uint8_t> allocator;
  uint8_t *data = length > 0 && size_t(length) <= LOGO_MAX_BYTES ? allocator.allocate(length) : nullptr;
  if (data == nullptr) {
    ESP_LOGW(TAG, "%s is too big, or empty", path.c_str());
    fclose(file);
    return;
  }
  const size_t size = fread(data, 1, length, file);
  fclose(file);
  auto *logo = new runtime_image::RuntimeImage(runtime_image::PNG, image::IMAGE_TYPE_RGB565,  // NOLINT
                                               image::TRANSPARENCY_ALPHA_CHANNEL);
  bool good = logo->begin_decode(size, runtime_image::PNG);
  size_t offset = 0;
  // The decoder works for a limited time per call
  for (int stuck = 0; good && offset != size && stuck != 100;) {
    const int consumed = logo->feed_data(data + offset, size - offset);
    if (consumed < 0) {
      good = false;
    } else if (consumed == 0) {
      stuck++;
    } else {
      offset += consumed;
      stuck = 0;
    }
  }
  good = good && offset == size && logo->end_decode() && logo->get_width() > 0;
  allocator.deallocate(data, length);
  if (!good) {
    ESP_LOGW(TAG, "%s could not be read as a PNG", path.c_str());
    logo->abort_decode();
    delete logo;  // NOLINT
    return;
  }
  this->logo_ = logo;
  lv_obj_t *image = lv_image_create(this->w_.splash_box);
  lv_obj_move_to_index(image, 0);
  lv_image_set_src(image, logo->get_lv_image_dsc());
  const int width = lv_display_get_horizontal_resolution(nullptr) * SPLASH_LOGO_WIDTH_PERCENT / 100;
  lv_obj_set_size(image, width, width * logo->get_height() / logo->get_width());
  lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
}

void TugUi::edit_next(TowField field) { this->open_picker_(PickerMode::PICKER_MODE_NEXT, field); }
void TugUi::edit_tow(TowField field) { this->open_picker_(PickerMode::PICKER_MODE_TOW, field); }

void TugUi::open_picker_(PickerMode mode, TowField field) {
  this->mode_ = mode;
  this->field_ = field;
  std::string value;
  if (mode == PickerMode::PICKER_MODE_NEXT) {
    value = field_of(this->logger_->next_details(), field);
  } else if (mode == PickerMode::PICKER_MODE_TOW) {
    TowRecord record;
    if (!this->current_tow_(record))
      return;
    value = field_of(record.details, field);
  }
  lv_obj_clean(this->w_.picker_list);
  if (mode == PickerMode::PICKER_MODE_WIFI_PASSWORD) {
    lv_label_set_text(this->w_.picker_title, ("Password for " + this->wifi_ssid_).c_str());
  } else {
    lv_label_set_text(this->w_.picker_title, FIELD_TITLES[size_t(field)]);
    // Blank clears an optional field, which the keyboard alone makes hard to reach
    const bool required = field == TowField::TOW_FIELD_GLIDER;
    if (!required)
      this->add_row_(this->w_.picker_list, "(blank)", [this]() { this->accept_(""); });
    const auto names = field == TowField::TOW_FIELD_GLIDER ? this->logger_->store().recent_gliders()
                                                           : this->logger_->store().recent_pilots();
    for (const std::string &name : names)
      this->add_row_(this->w_.picker_list, name, [this, name]() { this->accept_(name); });
  }
  lv_textarea_set_text(this->w_.picker_text, value.c_str());
  const bool registration = mode != PickerMode::PICKER_MODE_WIFI_PASSWORD && field == TowField::TOW_FIELD_GLIDER;
  lv_keyboard_set_mode(this->w_.picker_keyboard,
                       registration ? LV_KEYBOARD_MODE_TEXT_UPPER : LV_KEYBOARD_MODE_TEXT_LOWER);
  this->picker_text_changed();
  this->show_(this->w_.picker_page);
}

void TugUi::picker_text_changed() {
  if (this->mode_ == PickerMode::PICKER_MODE_WIFI_PASSWORD || this->field_ == TowField::TOW_FIELD_GLIDER)
    return;
  // Leave the keyboard alone on the number and symbol pages
  const lv_keyboard_mode_t mode = lv_keyboard_get_mode(this->w_.picker_keyboard);
  if (mode != LV_KEYBOARD_MODE_TEXT_UPPER && mode != LV_KEYBOARD_MODE_TEXT_LOWER)
    return;
  // A name's capitals, as capital_case() gives them when the name is accepted
  const char *text = lv_textarea_get_text(this->w_.picker_text);
  const size_t length = strlen(text);
  const char last = length != 0 ? text[length - 1] : ' ';
  const lv_keyboard_mode_t wanted =
      last == ' ' || last == '-' || last == '\'' ? LV_KEYBOARD_MODE_TEXT_UPPER : LV_KEYBOARD_MODE_TEXT_LOWER;
  if (mode != wanted)
    lv_keyboard_set_mode(this->w_.picker_keyboard, wanted);
}

void TugUi::picker_done() { this->accept_(lv_textarea_get_text(this->w_.picker_text)); }

void TugUi::picker_cancel() {
  if (this->mode_ == PickerMode::PICKER_MODE_TOW) {
    this->open_tow_(this->tow_id_);
  } else if (this->mode_ == PickerMode::PICKER_MODE_WIFI_PASSWORD) {
    this->show_(this->w_.settings_page);
  } else {
    this->back_to_ground();
  }
}

void TugUi::accept_(const std::string &entered) {
  if (this->mode_ == PickerMode::PICKER_MODE_WIFI_PASSWORD) {
    // Done on an empty box must not replace a saved password
    if (entered.empty()) {
      this->picker_cancel();
      return;
    }
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
    if (auto *networks = this->logger_->get_wifi_networks(); networks != nullptr) {
      networks->connect(this->wifi_ssid_.c_str(), entered.c_str());
      // Saved, but the choice of network stays automatic, so a SkyEcho still comes first
      networks->set_automatic();
    }
#endif
    lv_textarea_set_text(this->w_.picker_text, "");
    this->show_settings();
    this->wifi_list_refresh_ticks_ = WIFI_LIST_REFRESH_TICKS;
    return;
  }
  std::string value = trimmed(entered);
  if (this->field_ == TowField::TOW_FIELD_GLIDER) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
  } else {
    value = capital_case(value);
  }
  if (this->mode_ == PickerMode::PICKER_MODE_NEXT) {
    field_of(this->logger_->next_details(), this->field_) = value;
    this->logger_->next_details_changed();
    this->back_to_ground();
    return;
  }
  TowRecord record;
  if (this->current_tow_(record)) {
    field_of(record.details, this->field_) = value;
    this->logger_->store().update_details(record.id, record.details);
  }
  this->open_tow_(this->tow_id_);
}

bool TugUi::current_tow_(TowRecord &record) {
  for (const TowRecord &candidate : this->logger_->store().review_list()) {
    if (candidate.id == this->tow_id_) {
      record = candidate;
      return true;
    }
  }
  return false;
}

void TugUi::show_review() {
  lv_obj_clean(this->w_.review_list);
  for (const TowRecord &record : this->logger_->store().review_list()) {
    const std::string when = record.launch != 0 ? format_utc(record.launch).substr(11, 5) + " UTC" : "time unknown";
    const std::string glider =
        record.details.glider_registration.empty() ? "no glider" : record.details.glider_registration;
    const uint32_t id = record.id;
    this->add_row_(this->w_.review_list,
                   Text("%s   %s   %d ft%s   %u min", when.c_str(), glider.c_str(), int(record.max_height_ft),
                        record.approximate ? " approx" : "", unsigned(record.duration_s / 60))
                       .text,
                   [this, id]() { this->open_tow_(id); });
  }
  this->show_(this->w_.review_page);
}

void TugUi::open_tow_(uint32_t id) {
  this->tow_id_ = id;
  this->discard_armed_ = false;
  lv_label_set_text(this->w_.discard_label, "Not a tow");
  lv_label_set_text(this->w_.tow_message, "");
  this->refresh_tow_();
  this->show_(this->w_.tow_page);
}

void TugUi::refresh_tow_() {
  TowRecord record;
  if (!this->current_tow_(record))
    return;
  const std::string when = record.launch != 0 ? format_utc(record.launch) : "time unknown";
  lv_label_set_text(this->w_.tow_info, Text("%s\nHeight %d ft%s   Climb %u:%02u   Flight %u:%02u", when.c_str(),
                                            int(record.max_height_ft), record.approximate ? " (approximate)" : "",
                                            unsigned(record.climb_s / 60), unsigned(record.climb_s % 60),
                                            unsigned(record.duration_s / 60), unsigned(record.duration_s % 60))
                                           .text);
  for (size_t i = 0; i != TOW_FIELD_COUNT; i++)
    lv_label_set_text(this->w_.tow_fields[i], field_text(record.details, TowField(i)));
}

void TugUi::confirm_tow() {
  TowRecord record;
  const bool found = this->current_tow_(record);
  switch (this->logger_->confirm(this->tow_id_)) {
    case ConfirmResult::CONFIRM_RESULT_OK:
      if (found)
        this->logger_->store().remember(record.details);
      this->show_review();
      return;
    case ConfirmResult::CONFIRM_RESULT_NO_GLIDER:
      lv_label_set_text(this->w_.tow_message, "Enter the glider first");
      return;
    case ConfirmResult::CONFIRM_RESULT_NO_TIME:
      lv_label_set_text(this->w_.tow_message, "The launch time is unknown, so this tow can't be sent");
      return;
    case ConfirmResult::CONFIRM_RESULT_NO_TUG:
      lv_label_set_text(this->w_.tow_message, "No tug file on the SD card");
      return;
    default:
      lv_label_set_text(this->w_.tow_message, "Could not write to the SD card");
      return;
  }
}

void TugUi::discard_tow() {
  if (!this->discard_armed_) {
    this->discard_armed_ = true;
    lv_label_set_text(this->w_.discard_label, "Press again to remove");
    return;
  }
  TowRecord record;
  if (this->current_tow_(record))
    this->logger_->discard(record);
  this->show_review();
}

void TugUi::mark_refuelled() {
  if (this->refuel_armed_ticks_ == 0) {
    this->refuel_armed_ticks_ = REFUEL_ARMED_TICKS;
    lv_label_set_text(this->w_.refuel_label, "Confirm?");
    return;
  }
  this->refuel_armed_ticks_ = 0;
  lv_label_set_text(this->w_.refuel_label, "Refuelled");
  this->logger_->mark_refuelled();
  this->set_status_();
}

bool TugUi::confirm_shutdown() {
  if (this->shutdown_armed_ticks_ == 0) {
    this->shutdown_armed_ticks_ = SHUTDOWN_ARMED_TICKS;
    lv_label_set_text(this->w_.shutdown_label, "Confirm?");
    return false;
  }
  this->shutdown_armed_ticks_ = 0;
  lv_label_set_text(this->w_.shutdown_label, "Shut down");
  return true;
}

void TugUi::back_to_ground() {
  this->refresh();
  this->show_(this->w_.ground_page);
}

std::string TugUi::wifi_text_() {
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  if (auto *networks = this->logger_->get_wifi_networks(); networks != nullptr) {
    const wifi_networks::Status status = networks->get_status();
    switch (status.state) {
      case wifi_networks::WifiState::WIFI_STATE_CONNECTING:
        return status.ssid[0] != 0 ? std::string("Wi-Fi: joining ") + status.ssid + "..." : "Wi-Fi: joining...";
      case wifi_networks::WifiState::WIFI_STATE_CONNECTED:
        return std::string("Wi-Fi: ") + status.ssid + ", " + status.ip;
      default:
        return "Wi-Fi: off";
    }
  }
#endif
  return network::is_connected() ? "Wi-Fi: on" : "Wi-Fi: off";
}

// Kept up to date while the page shows
void TugUi::refresh_settings_() {
  const Provisioning &settings = this->logger_->provisioning();
  std::string info;
  if (this->logger_->has_provisioning()) {
    info = settings.tug_registration + "   " + settings.organisation +
           Text("\nField elevation %.0f ft", settings.field_elevation_ft).text;
  } else {
    info = "No tug.json on the SD card";
  }
  info += Text("\nQueued %zu, refused %zu", this->logger_->queued_count(), this->logger_->failed_count()).text;
  const std::string wifi = this->wifi_text_();
  info += "\n" + wifi;
  char build_time[Application::BUILD_TIME_STR_SIZE];
  App.get_build_time_string(build_time);
  info += std::string("\nFirmware ") + build_time;
  const FirmwareState state = this->logger_->firmware_state();
  const char *button = "Check for update";
  if (state == FirmwareState::FIRMWARE_STATE_CHECKING) {
    info += "\nChecking for an update...";
    button = "Checking...";
  } else if (state != FirmwareState::FIRMWARE_STATE_IDLE) {
    info += "\n" + this->logger_->firmware_offer().message;
    if (state == FirmwareState::FIRMWARE_STATE_AVAILABLE)
      button = "Install update";
  }
  if (strcmp(lv_label_get_text(this->w_.settings_info), info.c_str()) != 0)
    lv_label_set_text(this->w_.settings_info, info.c_str());
  if (strcmp(lv_label_get_text(this->w_.firmware_label), button) != 0)
    lv_label_set_text(this->w_.firmware_label, button);
  // The list shows which network is in use, so fill it again when that changes, unless a dialog is over it
  if (wifi != this->wifi_shown_ && lv_obj_has_flag(this->w_.wifi_dialog, LV_OBJ_FLAG_HIDDEN)) {
    this->wifi_shown_ = wifi;
    this->fill_wifi_list_();
  }
}

bool TugUi::firmware_button() {
  switch (this->logger_->firmware_state()) {
    case FirmwareState::FIRMWARE_STATE_CHECKING:
      return false;
    case FirmwareState::FIRMWARE_STATE_AVAILABLE:
      if (!this->logger_->is_flying())
        return true;
      lv_label_set_text(this->w_.settings_info, "Not while flying");
      return false;
    default:
      this->logger_->request_firmware_check();
      this->refresh_settings_();
      return false;
  }
}

void TugUi::firmware_install_failed() {
  this->logger_->firmware_install_failed();
  this->refresh_settings_();
}

void TugUi::show_settings() {
  lv_label_set_text(this->w_.settings_info, "");
  this->refresh_settings_();
  this->wifi_dialog_close();
  this->show_wifi_list();
  this->show_(this->w_.settings_page);
}

void TugUi::show_wifi_list() {
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  auto *networks = this->logger_->get_wifi_networks();
  if (networks == nullptr)
    return;
  networks->request_scan();
  this->fill_wifi_list_();
#endif
}

void TugUi::fill_wifi_list_() {
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  auto *networks = this->logger_->get_wifi_networks();
  if (networks == nullptr)
    return;
  lv_obj_clean(this->w_.wifi_list);
  static wifi_networks::NetworkInfo list[wifi_networks::MAX_LISTED];
  const size_t count = networks->get_networks(list, wifi_networks::MAX_LISTED);
  for (size_t i = 0; i != count; i++) {
    const std::string ssid = list[i].ssid;
    const bool known = list[i].known, secured = list[i].secured, saved = list[i].saved;
    // An open SkyEcho can't be joined with a password, so don't offer to set one
    const bool can_set_password = secured || !list[i].skyecho;
    std::string text = ssid;
    if (list[i].connected)
      text += " (in use)";
    if (list[i].skyecho)
      text += " SkyEcho";
    else if (saved)
      text += " saved";
    this->add_row_(this->w_.wifi_list, text, [this, networks, ssid, known, secured, saved, can_set_password]() {
      this->wifi_ssid_ = ssid;
      if (known) {
        this->open_wifi_dialog_(saved, can_set_password);
      } else if (secured) {
        this->open_picker_(PickerMode::PICKER_MODE_WIFI_PASSWORD, TowField::TOW_FIELD_GLIDER);
      } else {
        networks->connect(ssid.c_str(), "");
        networks->set_automatic();
      }
    });
  }
#endif
}

static void set_hidden(lv_obj_t *obj, bool hidden) {
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void TugUi::open_wifi_dialog_(bool saved, bool can_set_password) {
  lv_label_set_text(this->w_.wifi_dialog_title, this->wifi_ssid_.c_str());
  // A network from the configuration has nothing saved to forget
  set_hidden(this->w_.wifi_forget_button, !saved);
  set_hidden(this->w_.wifi_password_button, !can_set_password);
  set_hidden(this->w_.wifi_dialog, false);
}

void TugUi::wifi_dialog_close() { set_hidden(this->w_.wifi_dialog, true); }

void TugUi::wifi_connect() {
  this->wifi_dialog_close();
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  if (auto *networks = this->logger_->get_wifi_networks(); networks != nullptr) {
    // A known network: joined with its own password
    networks->connect(this->wifi_ssid_.c_str(), nullptr);
    this->wifi_list_refresh_ticks_ = WIFI_LIST_REFRESH_TICKS;
  }
#endif
}

void TugUi::wifi_change_password() {
  this->wifi_dialog_close();
  this->open_picker_(PickerMode::PICKER_MODE_WIFI_PASSWORD, TowField::TOW_FIELD_GLIDER);
}

void TugUi::wifi_forget() {
  this->wifi_dialog_close();
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  if (auto *networks = this->logger_->get_wifi_networks(); networks != nullptr) {
    networks->forget(this->wifi_ssid_.c_str());
    this->wifi_list_refresh_ticks_ = WIFI_LIST_REFRESH_TICKS;
  }
#endif
}

}  // namespace esphome::tug_logger

#endif
