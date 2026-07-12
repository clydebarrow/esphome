#include "traffic_widget.h"
#if defined(USE_LVGL) && defined(USE_IMAGE)

#include "gdl90.h"
#include "riemann.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace esphome::skyecho {

static const char *const TAG = "skyecho.widget";

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

  lv_timer_create(refresh_timer_cb, 1000, this);
  this->update();
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
    for (Target &t : this->targets_) {
      lv_obj_add_flag(t.image, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(t.label, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  // Cache the track-up rotation for this frame.
  this->cos_t_ = cosf(toRadians(-ownship.report.track));
  this->sin_t_ = sinf(toRadians(-ownship.report.track));

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

    const gdl90PositionReport_t &report = traffic[i].report;
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
