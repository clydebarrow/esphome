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
// targets are dead-reckoned. LVGL only supports whole-pixel positions, so a
// high frame rate just rounds many frames to the same pixel and then jumps;
// a few larger steps per second give more even motion.
static constexpr uint32_t REFRESH_INTERVAL_MS = 250;
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

  // Concentric range rings, created first so they sit behind everything else.
  // The spacing is snapped to a round number and rings are drawn at multiples
  // of it out to the range.
  size_t ring_count = 0;
  if (this->num_rings_ > 0 && this->range_ > 0.0f) {
    this->ring_step_ = nice_number_(this->range_ / this->num_rings_);
    if (this->ring_step_ > 0.0f) {
      ring_count = static_cast<size_t>(this->range_ / this->ring_step_ + 1e-3f);
    }
    if (ring_count == 0) {
      // Range is smaller than one nice step: fall back to a single edge ring.
      this->ring_step_ = this->range_;
      ring_count = 1;
    }
  }
  this->rings_.init(ring_count);
  for (size_t i = 0; i != ring_count; i++) {
    Ring ring{};
    ring.circle = lv_obj_create(root);
    lv_obj_remove_flag(ring.circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ring.circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ring.circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring.circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ring.circle, 0, 0);
    lv_obj_set_style_shadow_width(ring.circle, 0, 0);
    lv_obj_set_style_border_width(ring.circle, 1, 0);
    lv_obj_set_style_border_color(ring.circle, lv_color_hex(0x808080), 0);
    lv_obj_align(ring.circle, LV_ALIGN_CENTER, 0, 0);

    ring.label = lv_label_create(root);
    this->rings_.push_back(ring);
  }

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

void TrafficWidget::format_range_(char *buf, size_t len, float meters) {
  if (meters >= 1000.0f) {
    float km = meters / 1000.0f;
    if (km == floorf(km)) {
      snprintf(buf, len, "%.0fkm", km);
    } else {
      snprintf(buf, len, "%.1fkm", km);
    }
  } else {
    snprintf(buf, len, "%.0fm", meters);
  }
}

float TrafficWidget::nice_number_(float value) {
  if (value <= 0.0f) {
    return 0.0f;
  }
  float power = powf(10.0f, floorf(log10f(value)));
  float base = value / power;  // in [1, 10)
  float nice;
  if (base < 1.5f) {
    nice = 1.0f;
  } else if (base < 3.0f) {
    nice = 2.0f;
  } else if (base < 7.0f) {
    nice = 5.0f;
  } else {
    nice = 10.0f;
  }
  return nice * power;
}

void TrafficWidget::layout_rings_(float radius) {
  for (size_t i = 0; i != this->rings_.size(); i++) {
    Ring &ring = this->rings_[i];
    float distance = this->ring_step_ * (i + 1);
    auto ring_radius = static_cast<lv_coord_t>(distance / this->range_ * radius);
    lv_obj_set_size(ring.circle, ring_radius * 2, ring_radius * 2);

    char buf[16];
    format_range_(buf, sizeof(buf), distance);
    lv_label_set_text(ring.label, buf);
    // Place the label just inside the ring, on the vertical below the centre.
    lv_obj_align(ring.label, LV_ALIGN_CENTER, 0, ring_radius - 8);
  }
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

  // Range rings are a fixed grid and are shown regardless of GPS state.
  this->layout_rings_(radius);

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
