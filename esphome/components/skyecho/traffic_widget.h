#pragma once

#include "esphome/core/defines.h"
#if defined(USE_LVGL) && defined(USE_IMAGE)

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/image/image.h"
#include "esphome/core/helpers.h"
#include "skyecho.h"
#include "threat.h"
#include "units.h"

namespace esphome::skyecho {

/**
 * An LVGL compound widget that shows GDL90 traffic targets on a track-up
 * radar-style display. The ownship symbol sits at the centre and each target
 * is drawn as an image positioned by its distance and bearing relative to the
 * ownship. The whole scene is rotated so the ownship heading points up.
 */
class TrafficWidget : public lvgl::LvCompound {
 public:
  void set_parent(SkyEcho *parent) { this->parent_ = parent; }
  void set_ownship_image(image::Image *img) { this->ownship_image_ = img; }
  void set_target_image(image::Image *img) { this->target_image_ = img; }
  void set_north_image(image::Image *img) { this->north_image_ = img; }
  void set_range(float range_m) { this->range_ = range_m; }
  void set_max_targets(size_t max_targets) { this->max_targets_ = max_targets; }
  void set_range_rings(size_t rings) { this->num_rings_ = rings; }
  void set_alarm_distance(float meters) { this->threat_settings_.alarm_distance = meters; }
  void set_alarm_altitude(float meters) { this->threat_settings_.alarm_altitude = meters; }
  void set_glider_alarm_altitude(float meters) { this->threat_settings_.glider_alarm_altitude = meters; }
  void set_alarm_time(float seconds) { this->threat_settings_.alarm_time = seconds; }
  void set_max_distance(float meters) { this->threat_settings_.max_distance = meters; }
  void set_max_altitude_difference(float meters) { this->threat_settings_.max_altitude_difference = meters; }
  /// Seconds before close proximity at which to warn about a threat, longest first.
  void set_warning_times(std::initializer_list<uint16_t> seconds) { this->warning_times_ = seconds; }

  /// Called once at each warning time with that time in seconds, while a threat
  /// raises the alarm. A threat first seen after some warning times have passed
  /// gives one call, for the shortest of those.
  template<typename F> void add_on_threat_warning_callback(F &&callback) {
    this->warning_callback_.add(std::forward<F>(callback));
  }

  /**
   * Build the child objects and start the refresh timer. Called once from
   * generated setup code after all the setters above have run.
   */
  void build();

  /**
   * Recompute the position of every target from the current traffic list,
   * dead-reckoning each position forward for smooth motion between updates.
   */
  void update();

 protected:
  // Position an object relative to the ownship, applying the track-up rotation.
  // delta_north/delta_east are in pixels. Returns true if the object moved.
  bool translate_(lv_obj_t *obj, float delta_north, float delta_east);

  // Place the north indicator on the edge of the widget, in the direction of true
  // north from the ownship, keeping its centre margin pixels inside the edge.
  void place_north_(float margin);

  // Move a position report forward along its track by dt seconds.
  static void extrapolate_(gdl90PositionReport_t &report, float dt);

  // Size and position the concentric range rings for the given radius (pixels).
  void layout_rings_(float radius);

  // Round a value to a "nice" number (1, 2 or 5 times a power of ten).
  static float nice_number_(float value);

  // Show the callsign, relative altitude, distance, direction and closest approach
  // of the most significant target in the info box, flashing it while the alarm is
  // raised, or hide the box when report is null. The offsets from the ownship are in
  // meters; now is the current time in milliseconds.
  void update_info_(const gdl90PositionReport_t *report, const ThreatAssessment &assessment, float altitude_difference,
                    float delta_north, float delta_east, uint32_t now);

  // Point the info box arrow at an angle in radians, clockwise from straight up.
  void point_arrow_(float angle);

  // Call the warning callbacks for any warning time the threat has reached.
  void check_warnings_(const gdl90PositionReport_t *report, const ThreatAssessment &assessment);

  // Fix the sizes of the info box labels so they do not change as the numbers do.
  void fix_label_sizes_();

  // Create a hidden, content-sized box in a bottom corner of the widget.
  lv_obj_t *create_box_(lv_align_t align, lv_coord_t x_offset);

  // Show the ownship altitude and groundspeed, or hide the box when report is null.
  void update_ownship_(const gdl90PositionReport_t *report);

  struct Target {
    lv_obj_t *image;
    lv_obj_t *label;
  };

  struct Ring {
    lv_obj_t *circle;
    lv_obj_t *label;
  };

  SkyEcho *parent_{nullptr};
  image::Image *ownship_image_{nullptr};
  image::Image *target_image_{nullptr};
  image::Image *north_image_{nullptr};
  float range_{10000.0f};  // display radius in meters
  size_t max_targets_{8};
  size_t num_rings_{3};    // requested number of range rings
  float ring_step_{0.0f};  // snapped distance between rings, in meters

  lv_obj_t *ownship_obj_{nullptr};
  lv_obj_t *north_obj_{nullptr};
  lv_obj_t *ownship_box_{nullptr};
  lv_obj_t *ownship_label_{nullptr};
  lv_obj_t *info_box_{nullptr};
  lv_obj_t *info_arrow_{nullptr};
  lv_obj_t *info_label_{nullptr};
  lv_coord_t arrow_size_{0};
  lv_coord_t info_number_width_{0};       // widest line of numbers the info label can show
  bool highlight_on_{true};               // ring the info box target: off in the flash's inverted phase
  lv_point_precise_t arrow_points_[5]{};  // LVGL keeps a pointer to these
  Units units_{};
  ThreatSettings threat_settings_{};
  FixedVector<uint16_t> warning_times_{};
  LazyCallbackManager<void(uint32_t)> warning_callback_{};
  uint32_t warning_address_{0};  // the threat being warned about, or zero for none
  size_t warning_next_{0};       // index of the next warning time for that threat
  lv_color_t bg_color_{};
  lv_color_t text_color_{};
  FixedVector<Target> targets_{};
  FixedVector<Ring> rings_{};

  // How far the ownship sits below the widget centre, in pixels.
  lv_coord_t ownship_y_{0};
  // The radius the ownship and range rings were laid out for.
  float layout_radius_{-1.0f};
  // The track the display is rotated to: the ownship track while it is valid.
  float display_track_{0.0f};
  // Rotation for the display track (cached per update).
  float cos_t_{1.0f};
  float sin_t_{0.0f};
};

}  // namespace esphome::skyecho

#endif  // USE_LVGL && USE_IMAGE
