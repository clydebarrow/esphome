#include "tug_ui.h"

#if defined(USE_LVGL) && defined(USE_ESP32)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <utility>

#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"

namespace esphome::tug_logger {

static constexpr uint32_t COLOR_NORMAL = 0x202020;
static constexpr uint32_t COLOR_WARNING = 0xB02020;

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

static std::string field_text(const TowDetails &details, TowField field) {
  const std::string &value = field_of(const_cast<TowDetails &>(details), field);
  return std::string(FIELD_TITLES[size_t(field)]) + ": " + (value.empty() ? "-" : value);
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
  std::function<void()> on_long_press;
  bool long_pressed{false};
};

static void row_event(lv_event_t *event) {
  auto *actions = static_cast<RowActions *>(lv_event_get_user_data(event));
  switch (lv_event_get_code(event)) {
    case LV_EVENT_DELETE:
      delete actions;
      break;
    case LV_EVENT_PRESSED:
      actions->long_pressed = false;
      break;
    case LV_EVENT_LONG_PRESSED:
      if (actions->on_long_press) {
        actions->long_pressed = true;
        actions->on_long_press();
      }
      break;
    case LV_EVENT_CLICKED:
      if (!actions->long_pressed && actions->on_click)
        actions->on_click();
      break;
    default:
      break;
  }
}

void TugUi::add_row_(lv_obj_t *list, const std::string &text, std::function<void()> on_click,
                     std::function<void()> on_long_press) {
  lv_obj_t *button = lv_button_create(list);
  lv_obj_set_width(button, lv_pct(100));
  lv_obj_set_height(button, 72);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text.c_str());
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, lv_pct(100));
  lv_obj_center(label);
  auto *actions = new RowActions{std::move(on_click), std::move(on_long_press)};  // NOLINT
  lv_obj_add_event_cb(button, row_event, LV_EVENT_ALL, actions);
}

void TugUi::bind(TugLogger *logger, const TugUiWidgets &widgets) {
  this->logger_ = logger;
  this->w_ = widgets;
  // The rows are added at run time, so the configuration cannot give these a layout
  for (lv_obj_t *list : {widgets.picker_list, widgets.review_list, widgets.wifi_list}) {
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
  }
  this->refresh();
}

void TugUi::show_(lvgl::LvPageType *page) { this->w_.lvgl->show_page(page->index, LV_SCREEN_LOAD_ANIM_NONE, 0); }

void TugUi::set_status_() {
  auto *logger = this->logger_;
  std::string status = logger->clock_text();
  switch (logger->gps_source()) {
    case GpsSource::GPS_SOURCE_SKYECHO:
      status += "   GPS SkyEcho";
      break;
    case GpsSource::GPS_SOURCE_NMEA:
      status += "   GPS NMEA";
      break;
    default:
      status += "   NO GPS";
      break;
  }
  status +=
      logger->on_skyecho_network() ? "   Wi-Fi: SkyEcho" : (network::is_connected() ? "   Wi-Fi: on" : "   Wi-Fi: off");
  status += Text("   Queued %zu", logger->queued_count()).text;
  const float battery = logger->battery_voltage();
  if (!std::isnan(battery))
    status += Text("   %.1f V", battery).text;
  bool warning = false;
  if (!logger->card_ok()) {
    status += "   NO SD CARD";
    warning = true;
  } else if (!logger->has_provisioning()) {
    status += "   NO TUG FILE ON CARD";
    warning = true;
  }
  if (logger->failed_count() != 0) {
    status += Text("   %zu REFUSED", logger->failed_count()).text;
    warning = true;
  }
  if (logger->flight_data_lost()) {
    status += "   GPS LOST";
    warning = true;
  }
  lv_label_set_text(this->w_.status, status.c_str());
  lv_obj_set_style_bg_color(this->w_.status, lv_color_hex(warning ? COLOR_WARNING : COLOR_NORMAL), LV_PART_MAIN);
}

void TugUi::refresh_next_() {
  for (size_t i = 0; i != TOW_FIELD_COUNT; i++)
    lv_label_set_text(this->w_.next_fields[i], field_text(this->logger_->next_details(), TowField(i)).c_str());
}

void TugUi::refresh() {
  if (this->logger_ == nullptr)
    return;
  auto *logger = this->logger_;
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
  this->show_(this->w_.picker_page);
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
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
    if (auto *networks = this->logger_->get_wifi_networks(); networks != nullptr) {
      networks->connect(this->wifi_ssid_.c_str(), entered.c_str());
      // Saved, but the choice of network stays automatic, so a SkyEcho still comes first
      networks->set_automatic();
    }
#endif
    lv_textarea_set_text(this->w_.picker_text, "");
    this->show_settings();
    return;
  }
  std::string value = trimmed(entered);
  if (this->field_ == TowField::TOW_FIELD_GLIDER)
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
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
    lv_label_set_text(this->w_.tow_fields[i], field_text(record.details, TowField(i)).c_str());
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
  this->logger_->store().discard(this->tow_id_);
  this->show_review();
}

void TugUi::back_to_ground() {
  this->refresh();
  this->show_(this->w_.ground_page);
}

void TugUi::show_settings() {
  const Provisioning &settings = this->logger_->provisioning();
  std::string info;
  if (this->logger_->has_provisioning()) {
    info = settings.tug_registration + "   " + settings.organisation +
           Text("\nField elevation %.0f ft", settings.field_elevation_ft).text;
  } else {
    info = "No tug.json on the SD card";
  }
  info += Text("\nQueued %zu, refused %zu", this->logger_->queued_count(), this->logger_->failed_count()).text;
  char build_time[Application::BUILD_TIME_STR_SIZE];
  App.get_build_time_string(build_time);
  info += std::string("\nFirmware ") + build_time;
  lv_label_set_text(this->w_.settings_info, info.c_str());
  this->show_wifi_list();
  this->show_(this->w_.settings_page);
}

void TugUi::show_wifi_list() {
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  auto *networks = this->logger_->get_wifi_networks();
  if (networks == nullptr)
    return;
  networks->request_scan();
  lv_obj_clean(this->w_.wifi_list);
  static wifi_networks::NetworkInfo list[wifi_networks::MAX_LISTED];
  const size_t count = networks->get_networks(list, wifi_networks::MAX_LISTED);
  for (size_t i = 0; i != count; i++) {
    const std::string ssid = list[i].ssid;
    const bool known = list[i].known, secured = list[i].secured;
    std::string text = ssid;
    if (list[i].connected)
      text += " (in use)";
    if (list[i].skyecho)
      text += " SkyEcho";
    else if (list[i].saved)
      text += " saved";
    this->add_row_(
        this->w_.wifi_list, text,
        [this, networks, ssid, known, secured]() {
          if (known || !secured) {
            networks->connect(ssid.c_str(), known ? nullptr : "");
            networks->set_automatic();
            return;
          }
          this->wifi_ssid_ = ssid;
          this->open_picker_(PickerMode::PICKER_MODE_WIFI_PASSWORD, TowField::TOW_FIELD_GLIDER);
        },
        // Long press forgets a saved network
        list[i].saved ? std::function<void()>([networks, ssid]() { networks->forget(ssid.c_str()); }) : nullptr);
  }
#endif
}

}  // namespace esphome::tug_logger

#endif
