#include "traffic_widget.h"
#if defined(USE_LVGL) && defined(USE_IMAGE)

#include "gdl90.h"
#include "riemann.h"
#include "threat.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

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
// Gap between the info box and the edges of the widget, clear of rounded screen corners.
static constexpr lv_coord_t INFO_MARGIN = 12;
// The ownship sits this share of the widget height below the centre, leaving more
// room for traffic ahead.
static constexpr float OWNSHIP_OFFSET = 0.25f;
// The info box swaps its colors at this interval while the alarm is raised.
static constexpr uint32_t ALARM_FLASH_MS = 500;
// The info box shows the time to the closest approach in this format.
static constexpr const char *APPROACH_TIME_FORMAT = "%.0f s";
// The longest time to the closest approach that is shown; the threat look-ahead.
static constexpr float MAX_APPROACH_TIME = 120.0f;

// The width of a line of text when each of its digits may be any digit: the font's
// digits are not all the same width.
static lv_coord_t widest_number_text(const char *text, const lv_font_t *font) {
  char buf[32];
  lv_coord_t widest = 0;
  for (char digit = '0'; digit <= '9'; digit++) {
    size_t i = 0;
    for (; text[i] != 0 && i != sizeof(buf) - 1; i++)
      buf[i] = isdigit(static_cast<unsigned char>(text[i])) ? digit : text[i];
    buf[i] = 0;
    lv_point_t size;
    lv_text_get_size(&size, buf, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    widest = std::max(widest, static_cast<lv_coord_t>(size.x));
  }
  return widest;
}

// LVGL redraws an object whenever one of its styles or flags is set, even to the value
// it already has. The display updates several times a second, so these helpers only
// make the calls that change something.
static void set_hidden(lv_obj_t *obj, bool hidden) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden)
    return;
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

static bool set_text_if_changed(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text) == 0)
    return false;
  lv_label_set_text(label, text);
  return true;
}

static bool align_if_moved(lv_obj_t *obj, lv_align_t align, int32_t x, int32_t y) {
  if (lv_obj_get_style_align(obj, LV_PART_MAIN) == align && lv_obj_get_style_x(obj, LV_PART_MAIN) == x &&
      lv_obj_get_style_y(obj, LV_PART_MAIN) == y)
    return false;
  lv_obj_align(obj, align, x, y);
  return true;
}

static void set_state(lv_obj_t *obj, lv_state_t state, bool on) {
  if (lv_obj_has_state(obj, state) != on)
    lv_obj_set_state(obj, state, on);
}

static void refresh_timer_cb(lv_timer_t *timer) {
  static_cast<TrafficWidget *>(lv_timer_get_user_data(timer))->update();
}

void TrafficWidget::build() {
  lv_obj_t *root = this->obj;
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  // Kept for the info boxes and the target ring, and so an alarm can swap them.
  this->bg_color_ = lv_obj_get_style_bg_color(root, LV_PART_MAIN);
  this->text_color_ = lv_obj_get_style_text_color(root, LV_PART_MAIN);

  // Concentric range rings, created first so they sit behind everything else.
  // The spacing is snapped to a round number of distance units and rings are
  // drawn at multiples of it out to the range.
  size_t ring_count = 0;
  if (this->num_rings_ > 0 && this->range_ > 0.0f) {
    const float unit = this->units_.get_distance_unit_meters();
    this->ring_step_ = nice_number_(this->range_ / unit / this->num_rings_) * unit;
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

    ring.label = lv_label_create(root);
    this->rings_.push_back(ring);
  }

  // Ownship symbol, placed below the centre in update(). The scene is track-up so it never rotates.
  this->ownship_obj_ = lv_image_create(root);
  if (this->ownship_image_ != nullptr) {
    lvgl::lv_image_set_src(this->ownship_obj_, this->ownship_image_);
  }

  // North indicator: orbits the edge and rotates to always point at true north.
  if (this->north_image_ != nullptr) {
    this->north_obj_ = lv_image_create(root);
    lvgl::lv_image_set_src(this->north_obj_, this->north_image_);
    set_hidden(this->north_obj_, true);
  }

  // Pre-create the fixed pool of target image/label pairs, hidden until used.
  this->targets_.init(this->max_targets_);
  for (size_t i = 0; i != this->max_targets_; i++) {
    Target t{};
    t.image = lv_image_create(root);
    set_hidden(t.image, true);
    // The ring drawn round the target shown in the info box, while it is checked.
    lv_obj_set_style_radius(t.image, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_outline_pad(t.image, 3, 0);
    lv_obj_set_style_outline_color(t.image, this->text_color_, 0);
    lv_obj_set_style_outline_width(t.image, 0, 0);
    lv_obj_set_style_outline_width(t.image, 2, LV_STATE_CHECKED);
    if (this->target_image_ != nullptr) {
      lvgl::lv_image_set_src(t.image, this->target_image_);
    }
    t.label = lv_label_create(root);
    set_hidden(t.label, true);
    this->targets_.push_back(t);
  }

  // Info boxes, created last so they sit above the targets. Bottom left: the ownship.
  // Bottom right: an arrow pointing towards the most significant target, then its details.
  this->ownship_box_ = this->create_box_(LV_ALIGN_BOTTOM_LEFT, INFO_MARGIN);
  this->ownship_label_ = lv_label_create(this->ownship_box_);
  this->info_box_ = this->create_box_(LV_ALIGN_BOTTOM_RIGHT, -INFO_MARGIN);

  // The arrow is two text lines high, drawn in the text color.
  this->arrow_size_ =
      static_cast<lv_coord_t>(lv_font_get_line_height(lv_obj_get_style_text_font(root, LV_PART_MAIN)) * 2);
  this->info_arrow_ = lv_line_create(this->info_box_);
  lv_obj_set_size(this->info_arrow_, this->arrow_size_, this->arrow_size_);
  lv_obj_set_style_line_width(this->info_arrow_, 2, 0);
  lv_obj_set_style_line_rounded(this->info_arrow_, true, 0);
  lv_obj_set_style_line_color(this->info_arrow_, this->text_color_, 0);
  // Checked is the inverted phase of the alarm flash.
  lv_obj_set_style_bg_color(this->info_box_, this->text_color_, LV_STATE_CHECKED);
  lv_obj_set_style_text_color(this->info_box_, this->bg_color_, LV_STATE_CHECKED);
  lv_obj_set_style_line_color(this->info_arrow_, this->bg_color_, LV_STATE_CHECKED);
  lv_obj_align(this->info_arrow_, LV_ALIGN_LEFT_MID, 0, 0);
  this->info_label_ = lv_label_create(this->info_box_);
  lv_obj_set_style_text_align(this->info_label_, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(this->info_label_, this->arrow_size_ + 4, 0);
  this->fix_label_sizes_();

  lv_timer_create(refresh_timer_cb, REFRESH_INTERVAL_MS, this);
  this->update();
}

void TrafficWidget::check_warnings_(const gdl90PositionReport_t *report, const ThreatAssessment &assessment) {
  if (report == nullptr || !assessment.alarm) {
    this->warning_address_ = 0;
    return;
  }
  if (report->address != this->warning_address_) {
    this->warning_address_ = report->address;
    this->warning_next_ = 0;
  }
  bool due = false;
  while (this->warning_next_ != this->warning_times_.size() &&
         assessment.proximity_time <= this->warning_times_[this->warning_next_]) {
    this->warning_next_++;
    due = true;
  }
  if (due)
    this->warning_callback_.call(this->warning_times_[this->warning_next_ - 1]);
}

void TrafficWidget::fix_label_sizes_() {
  // Size the labels for the widest text they can show, so the boxes keep their size
  // as the numbers change. Sample values give each line its most digits.
  const lv_font_t *font = lv_obj_get_style_text_font(this->info_label_, LV_PART_MAIN);
  static constexpr float FEET = 0.3048f;
  const float unit = this->units_.get_distance_unit_meters();
  char buf[24];
  lv_coord_t width = 0;
  this->units_.format_altitude_difference(buf, sizeof(buf), -9900.0f * FEET);
  width = std::max(width, widest_number_text(buf, font));
  this->units_.format_distance(buf, sizeof(buf), 9.9f * unit);
  width = std::max(width, widest_number_text(buf, font));
  this->units_.format_distance(buf, sizeof(buf), 99.0f * unit);
  width = std::max(width, widest_number_text(buf, font));
  snprintf(buf, sizeof(buf), APPROACH_TIME_FORMAT, MAX_APPROACH_TIME);
  width = std::max(width, widest_number_text(buf, font));
  this->info_number_width_ = width;
  // Always four lines high, whether or not the time to the closest approach is shown.
  lv_point_t size;
  lv_text_get_size(&size, "0\n0\n0\n0", font, 0, lv_obj_get_style_text_line_space(this->info_label_, LV_PART_MAIN),
                   LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_obj_set_height(this->info_label_, size.y);

  width = 0;
  this->units_.format_altitude(buf, sizeof(buf), 45000.0f * FEET);
  width = std::max(width, widest_number_text(buf, font));
  this->units_.format_speed(buf, sizeof(buf), 999.0f * 1852.0f / 3600.0f);
  width = std::max(width, widest_number_text(buf, font));
  lv_obj_set_width(this->ownship_label_, width);
}

lv_obj_t *TrafficWidget::create_box_(lv_align_t align, lv_coord_t x_offset) {
  // The opaque background keeps the box readable when a target passes underneath.
  lv_obj_t *root = this->obj;
  lv_obj_t *box = lv_obj_create(root);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(box, this->bg_color_, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  // The theme styles the box's own text color, which would hide the widget's from the label.
  lv_obj_set_style_text_color(box, this->text_color_, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x808080), 0);
  lv_obj_set_style_radius(box, 4, 0);
  lv_obj_set_style_pad_all(box, 4, 0);
  lv_obj_align(box, align, x_offset, -INFO_MARGIN);
  set_hidden(box, true);
  return box;
}

void TrafficWidget::update_ownship_(const gdl90PositionReport_t *report) {
  if (report == nullptr) {
    set_hidden(this->ownship_box_, true);
    return;
  }
  char altitude[16];
  char speed[16];
  this->units_.format_altitude(altitude, sizeof(altitude), report->altitude);
  this->units_.format_speed(speed, sizeof(speed), report->groundSpeed);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s\n%s", altitude, speed);
  set_text_if_changed(this->ownship_label_, buf);
  set_hidden(this->ownship_box_, false);
}

void TrafficWidget::extrapolate_(gdl90PositionReport_t &report, float dt) {
  // Only the horizontal position is dead-reckoned so the image and label move
  // smoothly; the altitude (and hence the relative-altitude label) is left at
  // its last reported value.
  if (dt <= 0.0f || report.groundSpeed <= 0.0f || !report.isTrackValid) {
    return;
  }
  float distance = report.groundSpeed * dt;  // meters travelled since the report
  // Follow a turn by moving along the track half way through it, then turning.
  float track = toRadians(report.track + report.turnRate * dt / 2.0f);
  report.track = fmodf(report.track + report.turnRate * dt + 360.0f, 360.0f);
  float delta_north = distance * cosf(track);
  float delta_east = distance * sinf(track);
  report.latitude += delta_north / METERS_PER_DEGREE;
  report.longitude += delta_east / (METERS_PER_DEGREE * cosf(toRadians(report.latitude)));
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
    lv_obj_align(ring.circle, LV_ALIGN_CENTER, 0, this->ownship_y_);

    char buf[16];
    this->units_.format_range(buf, sizeof(buf), distance);
    set_text_if_changed(ring.label, buf);
    // Place the label just inside the ring, straight ahead of the ownship.
    lv_obj_align(ring.label, LV_ALIGN_CENTER, 0, this->ownship_y_ - ring_radius + 8);
  }
}

void TrafficWidget::point_arrow_(float angle) {
  // Shaft through the centre, then the two sides of the head at the tip.
  static constexpr float HEAD_ANGLE = 0.5f;  // radians either side of the shaft
  const float centre = this->arrow_size_ / 2.0f;
  const float length = centre - 2.0f;  // keep the rounded line ends inside the object
  const float head = length * 0.6f;
  const float dx = sinf(angle);
  const float dy = -cosf(angle);
  const float tip_x = centre + dx * length;
  const float tip_y = centre + dy * length;
  // lv_value_precise_t is an integer unless LVGL is built with float support.
  auto point = [](float x, float y) {
    return lv_point_precise_t{static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(y)};
  };
  const lv_point_precise_t points[5] = {
      point(centre - dx * length, centre - dy * length),
      point(tip_x, tip_y),
      point(tip_x - head * sinf(angle - HEAD_ANGLE), tip_y + head * cosf(angle - HEAD_ANGLE)),
      point(tip_x, tip_y),
      point(tip_x - head * sinf(angle + HEAD_ANGLE), tip_y + head * cosf(angle + HEAD_ANGLE)),
  };
  if (memcmp(points, this->arrow_points_, sizeof(points)) == 0)
    return;
  memcpy(this->arrow_points_, points, sizeof(points));
  lv_line_set_points(this->info_arrow_, this->arrow_points_, 5);
}

void TrafficWidget::update_info_(const gdl90PositionReport_t *report, const ThreatAssessment &assessment,
                                 float altitude_difference, float delta_north, float delta_east, uint32_t now) {
  if (report == nullptr) {
    set_hidden(this->info_box_, true);
    return;
  }
  // The bearing on screen, clockwise from straight up, after the track-up rotation.
  const float x = delta_east * this->cos_t_ + delta_north * this->sin_t_;
  const float y = delta_east * this->sin_t_ - delta_north * this->cos_t_;
  this->point_arrow_(atan2f(x, -y));
  // Callsigns are padded with trailing spaces.
  int callsign_len = static_cast<int>(strnlen(report->callsign, GDL_CALLSIGN_LENGTH));
  while (callsign_len > 0 && report->callsign[callsign_len - 1] == ' ')
    callsign_len--;
  char altitude[16];
  char range[16];
  this->units_.format_altitude_difference(altitude, sizeof(altitude), altitude_difference);
  this->units_.format_distance(range, sizeof(range), hypotf(delta_north, delta_east));
  char buf[80];
  int len = snprintf(buf, sizeof(buf), "%.*s\n%s\n%s", callsign_len, report->callsign, altitude, range);
  // While closing, add how soon the closest approach is predicted.
  if (assessment.approach_time > 0.0f && len > 0 && static_cast<size_t>(len) + 1 < sizeof(buf)) {
    buf[len++] = '\n';
    snprintf(buf + len, sizeof(buf) - len, APPROACH_TIME_FORMAT, assessment.approach_time);
  }
  set_text_if_changed(this->info_label_, buf);
  // Wide enough for any numbers, or for this callsign if that is wider.
  char callsign[GDL_CALLSIGN_LENGTH + 1];
  snprintf(callsign, sizeof(callsign), "%.*s", callsign_len, report->callsign);
  lv_point_t size;
  lv_text_get_size(&size, callsign, lv_obj_get_style_text_font(this->info_label_, LV_PART_MAIN), 0, 0, LV_COORD_MAX,
                   LV_TEXT_FLAG_NONE);
  const lv_coord_t width = std::max(this->info_number_width_, static_cast<lv_coord_t>(size.x));
  if (lv_obj_get_style_width(this->info_label_, LV_PART_MAIN) != width)
    lv_obj_set_width(this->info_label_, width);

  // Flash the box by swapping its colors while the alarm is raised.
  const bool inverted = assessment.alarm && (now / ALARM_FLASH_MS) % 2 == 0;
  set_state(this->info_box_, LV_STATE_CHECKED, inverted);
  set_state(this->info_arrow_, LV_STATE_CHECKED, inverted);
  set_hidden(this->info_box_, false);
  this->highlight_on_ = !inverted;
}

bool TrafficWidget::translate_(lv_obj_t *obj, float delta_north, float delta_east) {
  // Rotate the (east, north) offset into screen space (track-up) and place the
  // object relative to the display centre. Screen y grows downward, so north
  // maps to negative y.
  auto x = static_cast<lv_coord_t>(delta_east * this->cos_t_ + delta_north * this->sin_t_);
  auto y = static_cast<lv_coord_t>(delta_east * this->sin_t_ - delta_north * this->cos_t_);
  return align_if_moved(obj, LV_ALIGN_CENTER, x, this->ownship_y_ + y);
}

void TrafficWidget::place_north_(float margin) {
  // Screen direction of true north from the ownship (y grows downward), then the
  // furthest point along it that keeps the indicator inside the widget.
  const float dx = this->sin_t_;
  const float dy = -this->cos_t_;
  const float half_width = lv_obj_get_width(this->obj) / 2.0f - margin;
  const float half_height = lv_obj_get_height(this->obj) / 2.0f - margin;
  float reach = INFINITY;
  if (fabsf(dx) > 1e-6f)
    reach = std::min(reach, half_width / fabsf(dx));
  if (dy < -1e-6f) {
    reach = std::min(reach, (half_height + this->ownship_y_) / -dy);
  } else if (dy > 1e-6f) {
    reach = std::min(reach, (half_height - this->ownship_y_) / dy);
  }
  align_if_moved(this->north_obj_, LV_ALIGN_CENTER, static_cast<lv_coord_t>(dx * reach),
                 static_cast<lv_coord_t>(this->ownship_y_ + dy * reach));
}

void TrafficWidget::update() {
  lv_obj_t *root = this->obj;
  const float height = lv_obj_get_height(root);
  this->ownship_y_ = static_cast<lv_coord_t>(height * OWNSHIP_OFFSET);
  // The range reaches from the ownship to the top edge, straight ahead.
  const float radius = height / 2.0f + this->ownship_y_;
  const float scale = radius / this->range_;  // pixels per meter
  // The ownship and the range rings are a fixed grid, shown regardless of GPS state,
  // so they only move when the widget changes size.
  if (radius != this->layout_radius_) {
    this->layout_radius_ = radius;
    lv_obj_align(this->ownship_obj_, LV_ALIGN_CENTER, 0, this->ownship_y_);
    this->layout_rings_(radius);
  }

  OwnshipT ownship;
  bool pos_valid = this->parent_->get_ownship_position(&ownship);
  if (!pos_valid) {
    if (this->north_obj_ != nullptr) {
      set_hidden(this->north_obj_, true);
    }
    for (Target &t : this->targets_) {
      set_hidden(t.image, true);
      set_hidden(t.label, true);
    }
    this->update_info_(nullptr, {}, 0.0f, 0.0f, 0.0f, 0);
    this->update_ownship_(nullptr);
    return;
  }

  uint32_t now = millis();

  // Dead-reckon the ownship forward so the relative motion of traffic is smooth.
  float own_dt = std::min((now - ownship.timestampMs) / 1000.0f, MAX_EXTRAPOLATE_S);
  this->extrapolate_(ownship.report, own_dt);

  this->update_ownship_(&ownship.report);

  // Cache the track-up rotation for this frame. Without a valid track the display
  // stays on the last known one, so it does not swing round to north up.
  if (ownship.report.isTrackValid)
    this->display_track_ = ownship.report.track;
  this->cos_t_ = cosf(toRadians(-this->display_track_));
  this->sin_t_ = sinf(toRadians(-this->display_track_));

  // Place the north indicator at the edge in the direction of true north. It is
  // only shown while the track is valid, since otherwise north is not known.
  if (this->north_obj_ != nullptr && !ownship.report.isTrackValid) {
    set_hidden(this->north_obj_, true);
  } else if (this->north_obj_ != nullptr) {
    set_hidden(this->north_obj_, false);
    int north_angle = static_cast<int>(-this->display_track_) % 360;
    if (north_angle < 0) {
      north_angle += 360;
    }
    lv_image_set_rotation(this->north_obj_, static_cast<int16_t>(north_angle * 10));
    this->place_north_(this->north_image_->get_height() / 2.0f + 2.0f);
  }

  // Dead-reckon and assess every active target, then pick the most significant one
  // for the info box. It may be beyond the targets drawn, which are the nearest ones.
  TrafficT traffic[MAX_TRAFFIC_TRACKED];
  bool ignored[MAX_TRAFFIC_TRACKED];
  this->parent_->get_traffic(traffic, MAX_TRAFFIC_TRACKED);
  SignificantTargetSelector selector(ownship.report, this->threat_settings_);
  size_t cnt = 0;
  for (; cnt != MAX_TRAFFIC_TRACKED && traffic[cnt].active; cnt++) {
    float target_dt = std::min((now - traffic[cnt].timestampMs) / 1000.0f, MAX_EXTRAPOLATE_S);
    this->extrapolate_(traffic[cnt].report, target_dt);
    ignored[cnt] = selector.consider(cnt, traffic[cnt].report).ignored;
  }
  if (selector.get_best() < 0) {
    this->update_info_(nullptr, {}, 0.0f, 0.0f, 0.0f, 0);
    this->check_warnings_(nullptr, {});
  } else {
    // update_info_() sets highlight_on_, so the target's ring flashes with the box.
    const gdl90PositionReport_t &best = traffic[selector.get_best()].report;
    this->update_info_(&best, selector.get_best_assessment(), best.altitude - ownship.report.altitude,
                       trafficNorthing(&ownship.report, &best), trafficEasting(&ownship.report, &best), now);
    this->check_warnings_(&best, selector.get_best_assessment());
  }

  // Draw the nearest targets that are not filtered out.
  size_t next = 0;
  for (Target &t : this->targets_) {
    while (next != cnt && ignored[next])
      next++;
    if (next == cnt) {
      set_hidden(t.image, true);
      set_hidden(t.label, true);
      continue;
    }
    const size_t i = next++;

    const gdl90PositionReport_t &report = traffic[i].report;
    float delta_east = trafficEasting(&ownship.report, &report) * scale;
    float delta_north = trafficNorthing(&ownship.report, &report) * scale;

    set_hidden(t.image, false);
    set_hidden(t.label, false);
    const bool moved = this->translate_(t.image, delta_north, delta_east);
    // Ring the target shown in the info box.
    set_state(t.image, LV_STATE_CHECKED, static_cast<int>(i) == selector.get_best() && this->highlight_on_);

    // Rotate the icon to the target's track relative to the ownship heading.
    int angle = static_cast<int>(report.track - this->display_track_) % 360;
    if (angle < 0) {
      angle += 360;
    }
    lv_image_set_rotation(t.image, static_cast<int16_t>(angle * 10));

    // Label: relative altitude in hundreds of feet, plus a short callsign.
    char buf[24];
    float alt_diff_hundreds_ft = (report.altitude - ownship.report.altitude) / 0.3048f / 100.0f;
    snprintf(buf, sizeof(buf), "%+04.0f\n%.4s", alt_diff_hundreds_ft, report.callsign);
    if (set_text_if_changed(t.label, buf) || moved)
      lv_obj_align_to(t.label, t.image, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
  }
}

}  // namespace esphome::skyecho

#endif  // USE_LVGL && USE_IMAGE
