#include "traffic_widget.h"
#if defined(USE_LVGL) && defined(USE_IMAGE)

#include "gdl90.h"
#include "riemann.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace esphome::skyecho {

// How often the display is repositioned. Between the ~1 Hz data updates the
// targets are dead-reckoned so their motion looks smooth.
static constexpr uint32_t REFRESH_INTERVAL_MS = 50;
// Cap on how far a position is extrapolated, so stale targets do not run away.
static constexpr float MAX_EXTRAPOLATE_S = 2.0f;
// Meters per degree of latitude (approx), used for the extrapolation.
static constexpr float METERS_PER_DEGREE = 111111.0f;

static void refresh_timer_cb(lv_timer_t *timer) {
  static_cast<TrafficWidget *>(lv_timer_get_user_data(timer))->update();
}

void TrafficWidget::build() {
  lv_obj_t *root = this->obj;
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // Ownship symbol at the centre. The scene is heading-up so it never rotates.
  this->ownship_obj_ = lv_image_create(root);
  lv_obj_align(this->ownship_obj_, LV_ALIGN_CENTER, 0, 0);
  if (this->ownship_image_ != nullptr) {
    lvgl::lv_image_set_src(this->ownship_obj_, this->ownship_image_);
  }

  // North indicator: orbits the edge and rotates to always point at true north.
  if (this->north_image_ != nullptr) {
    this->north_obj_ = lv_image_create(root);
    lvgl::lv_image_set_src(this->north_obj_, this->north_image_);
    lv_obj_add_flag(this->north_obj_, LV_OBJ_FLAG_HIDDEN);
  }

  // Pre-create the fixed pool of target image/label pairs, hidden until used.
  this->targets_.init(this->max_targets_);
  for (size_t i = 0; i != this->max_targets_; i++) {
    Target t{};
    t.image = lv_image_create(root);
    lv_obj_add_flag(t.image, LV_OBJ_FLAG_HIDDEN);
    if (this->target_image_ != nullptr) {
      lvgl::lv_image_set_src(t.image, this->target_image_);
    }
    t.label = lv_label_create(root);
    lv_obj_add_flag(t.label, LV_OBJ_FLAG_HIDDEN);
    this->targets_.push_back(t);
  }

  lv_timer_create(refresh_timer_cb, REFRESH_INTERVAL_MS, this);
  this->update();
}

void TrafficWidget::extrapolate_(gdl90PositionReport_t &report, float dt) {
  // Only the horizontal position is dead-reckoned so the image and label move
  // smoothly; the altitude (and hence the relative-altitude label) is left at
  // its last reported value.
  if (dt <= 0.0f || report.groundSpeed <= 0.0f) {
    return;
  }
  float distance = report.groundSpeed * dt;  // meters travelled since the report
  float track = toRadians(report.track);
  float delta_north = distance * cosf(track);
  float delta_east = distance * sinf(track);
  report.latitude += delta_north / METERS_PER_DEGREE;
  report.longitude += delta_east / (METERS_PER_DEGREE * cosf(toRadians(report.latitude)));
}

void TrafficWidget::translate_(lv_obj_t *obj, float delta_north, float delta_east) {
  // Rotate the (east, north) offset into screen space (track-up) and place the
  // object relative to the display centre. Screen y grows downward, so north
  // maps to negative y.
  auto x = static_cast<lv_coord_t>(delta_east * this->cos_t_ + delta_north * this->sin_t_);
  auto y = static_cast<lv_coord_t>(delta_east * this->sin_t_ - delta_north * this->cos_t_);
  lv_obj_align(obj, LV_ALIGN_CENTER, x, y);
}

void TrafficWidget::update() {
  lv_obj_t *root = this->obj;
  float radius = std::min(lv_obj_get_width(root), lv_obj_get_height(root)) / 2.0f;
  float scale = radius / this->range_;  // pixels per meter

  OwnshipT ownship;
  bool pos_valid = this->parent_->get_ownship_position(&ownship);
  if (!pos_valid) {
    if (this->north_obj_ != nullptr) {
      lv_obj_add_flag(this->north_obj_, LV_OBJ_FLAG_HIDDEN);
    }
    for (Target &t : this->targets_) {
      lv_obj_add_flag(t.image, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(t.label, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  uint32_t now = millis();

  // Dead-reckon the ownship forward so the relative motion of traffic is smooth.
  float own_dt = std::min((now - ownship.timestampMs) / 1000.0f, MAX_EXTRAPOLATE_S);
  this->extrapolate_(ownship.report, own_dt);

  // Cache the track-up rotation for this frame.
  this->cos_t_ = cosf(toRadians(-ownship.report.track));
  this->sin_t_ = sinf(toRadians(-ownship.report.track));

  // Position the north indicator at the edge in the direction of true north.
  if (this->north_obj_ != nullptr) {
    lv_obj_remove_flag(this->north_obj_, LV_OBJ_FLAG_HIDDEN);
    int north_angle = static_cast<int>(-ownship.report.track) % 360;
    if (north_angle < 0) {
      north_angle += 360;
    }
    lv_image_set_rotation(this->north_obj_, static_cast<int16_t>(north_angle * 10));
    float edge = radius - this->north_image_->get_height() / 2.0f;
    this->translate_(this->north_obj_, edge, 0.0f);
  }

  TrafficT traffic[MAX_TRAFFIC_TRACKED];
  size_t cnt = std::min(this->targets_.size(), static_cast<size_t>(MAX_TRAFFIC_TRACKED));
  this->parent_->get_traffic(traffic, cnt);

  for (size_t i = 0; i != this->targets_.size(); i++) {
    Target &t = this->targets_[i];
    if (i >= cnt || !traffic[i].active) {
      lv_obj_add_flag(t.image, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(t.label, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    // Copy so the position can be dead-reckoned without touching the source.
    gdl90PositionReport_t report = traffic[i].report;
    float target_dt = std::min((now - traffic[i].timestampMs) / 1000.0f, MAX_EXTRAPOLATE_S);
    this->extrapolate_(report, target_dt);

    float delta_east = trafficEasting(&ownship.report, &report) * scale;
    float delta_north = trafficNorthing(&ownship.report, &report) * scale;

    lv_obj_remove_flag(t.image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(t.label, LV_OBJ_FLAG_HIDDEN);
    this->translate_(t.image, delta_north, delta_east);

    // Rotate the icon to the target's track relative to the ownship heading.
    int angle = static_cast<int>(report.track - ownship.report.track) % 360;
    if (angle < 0) {
      angle += 360;
    }
    lv_image_set_rotation(t.image, static_cast<int16_t>(angle * 10));

    // Label: relative altitude in hundreds of feet, plus a short callsign.
    char buf[24];
    float alt_diff_hundreds_ft = (report.altitude - ownship.report.altitude) / 0.3048f / 100.0f;
    snprintf(buf, sizeof(buf), "%+04.0f\n%.4s", alt_diff_hundreds_ft, report.callsign);
    lv_label_set_text(t.label, buf);
    lv_obj_align_to(t.label, t.image, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
  }
}

}  // namespace esphome::skyecho

#endif  // USE_LVGL && USE_IMAGE
