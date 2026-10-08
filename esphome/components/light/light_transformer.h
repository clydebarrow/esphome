#pragma once

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "light_color_values.h"

namespace esphome::light {

/// Base class for all light color transformers, such as transitions or flashes.
class LightTransformer {
 public:
  virtual ~LightTransformer() = default;

  void setup(const LightColorValues &start_values, const LightColorValues &target_values, uint32_t length) {
    this->start_time_ = millis();
    this->length_ = length;
    this->start_values_ = start_values;
    this->target_values_ = target_values;
    this->start();
  }

  /// Indicates whether this transformation is finished.
  virtual bool is_finished() { return this->get_progress_() >= 1.0f; }

  /// This will be called before the transition is started.
  virtual void start() {}

  /// This will be called while the transformer is active to apply the transition to the light. Can either write to the
  /// light directly, or return LightColorValues that will be applied.
  virtual optional<LightColorValues> apply() = 0;

  /// This will be called after transition is finished.
  virtual void stop() {}

  const LightColorValues &get_start_values() const { return this->start_values_; }

  const LightColorValues &get_target_values() const { return this->target_values_; }

 protected:
  // This looks crazy, but it reduces to 6x^5 - 15x^4 + 10x^3 which is just a smooth sigmoid-like
  // transition from 0 to 1 on x = [0, 1]
  static float smoothed_progress(float x) { return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f); }

  /// The progress of this transition, on a scale of 0 to 1.
  float get_progress_() {
    uint32_t now = esphome::millis();
    uint32_t elapsed = now - this->start_time_;
    if (elapsed >= this->length_)
      return 1.0f;

    return clamp(elapsed / float(this->length_), 0.0f, 1.0f);
  }

  /// The values at the given smoothed progress, for transformers that write to the light directly.
  /// Like the default transition, turning on or off fades the brightness from or to zero.
  LightColorValues get_progress_values_(float smoothed_progress) const {
    LightColorValues start = this->start_values_;
    LightColorValues end = this->target_values_;
    if (!start.is_on() && end.is_on()) {
      start = end;
      start.set_brightness(0.0f);
    } else if (start.is_on() && !end.is_on()) {
      end = start;
      end.set_brightness(0.0f);
    }
    return LightColorValues::lerp(start, end, smoothed_progress);
  }

  uint32_t start_time_;
  uint32_t length_;
  LightColorValues start_values_;
  LightColorValues target_values_;
};

}  // namespace esphome::light
