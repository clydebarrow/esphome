#include "animation.h"

#include "esphome/core/hal.h"

namespace esphome::animation {

Animation::Animation(const uint8_t *data_start, int width, int height, uint32_t animation_frame_count,
                     image::ImageType type, image::Transparency transparent)
    : Image(data_start, width, height, type, transparent),
      animation_data_start_(data_start),
      current_frame_(0),
      animation_frame_count_(animation_frame_count),
      loop_start_frame_(0),
      loop_end_frame_(animation_frame_count_),
      loop_count_(0),
      loop_current_iteration_(1) {}
void Animation::set_loop(uint32_t start_frame, uint32_t end_frame, int count) {
  loop_start_frame_ = std::min(start_frame, animation_frame_count_);
  loop_end_frame_ = std::min(end_frame, animation_frame_count_);
  loop_count_ = count;
  loop_current_iteration_ = 1;
}

uint32_t Animation::get_animation_frame_count() const { return this->animation_frame_count_; }
int Animation::get_current_frame() const { return this->current_frame_; }
void Animation::next_frame() {
  this->current_frame_++;
  if (loop_count_ && static_cast<uint32_t>(this->current_frame_) == loop_end_frame_ &&
      (this->loop_current_iteration_ < loop_count_ || loop_count_ < 0)) {
    this->current_frame_ = loop_start_frame_;
    this->loop_current_iteration_++;
  }
  if (static_cast<uint32_t>(this->current_frame_) >= animation_frame_count_) {
    this->loop_current_iteration_ = 1;
    this->current_frame_ = 0;
  }

  this->update_data_start_();
}
void Animation::prev_frame() {
  this->current_frame_--;
  if (this->current_frame_ < 0) {
    this->current_frame_ = this->animation_frame_count_ - 1;
  }

  this->update_data_start_();
}

void Animation::set_frame(int frame) {
  unsigned abs_frame = abs(frame);

  if (abs_frame < this->animation_frame_count_) {
    if (frame >= 0) {
      this->current_frame_ = frame;
    } else {
      this->current_frame_ = this->animation_frame_count_ - abs_frame;
    }
  }

  this->update_data_start_();
}

uint32_t Animation::get_frame_size_() const {
  uint32_t image_size = this->get_width_stride() * this->height_;
  // RGB565 with an alpha channel stores the alpha plane immediately after the RGB
  // plane within each frame, so the per-frame stride includes the alpha bytes.
  if (this->type_ == image::IMAGE_TYPE_RGB565 && this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
    image_size += static_cast<uint32_t>(this->width_) * this->height_;
  }
  return image_size;
}

void Animation::update_data_start_() {
  this->data_start_ = this->animation_data_start_ + this->get_frame_size_() * this->current_frame_;
}

#ifdef USE_LVGL
std::vector<lv_image_dsc_t *> Animation::get_lv_animimg_descs() {
  // Lazily build one lv_image_dsc_t per frame -- each needs to stay alive as its own
  // object for as long as LVGL's own animation timer is cycling through them, unlike
  // Image::get_lv_image_dsc()'s single cached descriptor that just gets reseated.
  if (this->lv_frame_descs_.empty()) {
    this->lv_frame_descs_.init(this->animation_frame_count_);
    const uint32_t frame_size = this->get_frame_size_();
    for (uint32_t frame = 0; frame < this->animation_frame_count_; frame++) {
      lv_image_dsc_t dsc{};
      this->fill_lv_image_dsc_(dsc, this->animation_data_start_ + frame_size * frame);
      this->lv_frame_descs_.push_back(dsc);
    }
  }
  std::vector<lv_image_dsc_t *> result;
  result.reserve(this->lv_frame_descs_.size());
  for (auto &dsc : this->lv_frame_descs_) {
    result.push_back(&dsc);
  }
  return result;
}
#endif  // USE_LVGL

}  // namespace esphome::animation
