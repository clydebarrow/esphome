#pragma once

#include "esphome/core/defines.h"
#if defined(USE_LVGL) && defined(USE_IMAGE)

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/image/image.h"
#include "esphome/core/helpers.h"
#include "skyecho.h"

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
  // Position an object relative to the display centre, applying the track-up
  // rotation. delta_north/delta_east are in pixels.
  void translate_(lv_obj_t *obj, float delta_north, float delta_east);

  // Move a position report forward along its track by dt seconds.
  static void extrapolate_(gdl90PositionReport_t &report, float dt);

  // Size and position the concentric range rings for the given radius (pixels).
  void layout_rings_(float radius);

  // Format a distance in meters as a short human-readable string (e.g. "5km").
  static void format_range_(char *buf, size_t len, float meters);

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
  size_t num_rings_{3};

  lv_obj_t *ownship_obj_{nullptr};
  lv_obj_t *north_obj_{nullptr};
  FixedVector<Target> targets_{};
  FixedVector<Ring> rings_{};

  // Rotation for the current ownship track (cached per update).
  float cos_t_{1.0f};
  float sin_t_{0.0f};
};

}  // namespace esphome::skyecho

#endif  // USE_LVGL && USE_IMAGE
