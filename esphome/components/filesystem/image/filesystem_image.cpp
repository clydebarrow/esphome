#include "filesystem_image.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

#include "esphome/components/runtime_image/image_decoder.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::filesystem {

ESPHOME_LOG_TAG(TAG, "filesystem.image");

using runtime_image::ImageFormat;

// Time one loop pass may spend reading and decoding
static constexpr uint32_t PASS_BUDGET_MS = 20;

// The format, from the first bytes of the file
static ImageFormat sniff_format(const uint8_t *data, size_t len) {
  if (len >= 2 && data[0] == 'B' && data[1] == 'M')
    return runtime_image::BMP;
  if (len >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
    return runtime_image::PNG;
  if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
    return runtime_image::JPEG;
  if (len >= 4 && memcmp(data, "qoif", 4) == 0)
    return runtime_image::QOI;
  return runtime_image::AUTO;
}

void FilesystemImage::setup() { this->disable_loop(); }

const char *FilesystemImage::update_path_() const {
#ifdef USE_FILE
  if (this->file_ref_ != nullptr) {
    const char *path = this->file_ref_->get_path();
    return path[0] == 0 ? nullptr : path;
  }
#endif
  return this->path_;
}

void FilesystemImage::update() {
  const char *path = this->update_path_();
  if (path != nullptr)
    this->load(path);
}

void FilesystemImage::dump_config() {
  const char *path = this->update_path_();
  ESP_LOGCONFIG(TAG,
                "Filesystem image:\n"
                "  Path: %s\n"
                "  Format: %s\n"
                "  Chunk size: %" PRIu32,
                path != nullptr ? path : LOG_STR_LITERAL("none"),
                LOG_STR_ARG(runtime_image::get_format_name(this->get_format())), this->chunk_size_);
  LOG_UPDATE_INTERVAL(this);
}

bool FilesystemImage::allocate_buffer_() {
  if (this->read_buffer_ != nullptr)
    return true;
  RAMAllocator<uint8_t> allocator;
  this->read_buffer_ = allocator.allocate(this->chunk_size_);
  if (this->read_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate a %" PRIu32 " byte read buffer", this->chunk_size_);
    return false;
  }
  this->read_buffer_size_ = this->chunk_size_;
  return true;
}

void FilesystemImage::free_buffer_() {
  RAMAllocator<uint8_t> allocator;
  allocator.deallocate(this->read_buffer_, this->read_buffer_size_);
  this->read_buffer_ = nullptr;
  this->read_buffer_size_ = 0;
}

void FilesystemImage::load(const char *path) {
  if (this->is_decoding() || this->file_) {
    ESP_LOGW(TAG, "Already loading an image");
    return;
  }
  this->file_ = this->fs_->open(path, OPEN_READ);
  if (!this->file_) {
    ESP_LOGE(TAG, "Cannot open %s: %s", path, LOG_STR_ARG(fs_error_to_string(this->fs_->last_error())));
    this->error_callback_.call();
    return;
  }
  const int32_t size = this->file_.size();
  this->unread_ = 0;
  this->end_of_file_ = false;
  if (size <= 0) {
    this->fail_(LOG_STR("File is empty"));
    return;
  }

  ImageFormat format = this->get_format();
  if (format == runtime_image::AUTO) {
    uint8_t head[16];
    const int32_t n = this->file_.read(head, sizeof(head));
    format = sniff_format(head, n > 0 ? n : 0);
    if (format == runtime_image::AUTO) {
      this->fail_(LOG_STR("Unknown image format"));
      return;
    }
    if (this->file_.seek(0) != 0) {
      this->fail_(LOG_STR("Read failed"));
      return;
    }
  }
  if (!this->begin_decode(size, format)) {
    this->fail_(LOG_STR("Cannot start the decoder"));
    return;
  }
  if (this->decoder_supports_source()) {
    const int err = this->begin_source(&this->source_);
    if (err != 0) {
      ESP_LOGE(TAG, "Decode failed: %s", runtime_image::decode_error_to_string(err));
      this->fail_(LOG_STR("Cannot start the decoder"));
      return;
    }
    this->reading_source_ = true;
  } else if (!this->allocate_buffer_()) {
    // Only decoders that are fed data need the chunk buffer
    this->fail_(LOG_STR("Out of memory"));
    return;
  }
  ESP_LOGD(TAG, "Loading %s, %s, %" PRId32 " bytes", path, LOG_STR_ARG(runtime_image::get_format_name(format)), size);
  this->start_time_ = millis();
  this->enable_loop();
}

void FilesystemImage::loop() {
  const uint32_t start = App.get_loop_component_start_time();
  while (this->step_() && millis() - start < PASS_BUDGET_MS) {
  }
}

bool FilesystemImage::step_() {
  if (!this->is_decoding() || !this->file_) {
    this->stop_();
    return false;
  }
  if (this->reading_source_) {
    const int result = this->decode_step();
    if (result == runtime_image::DECODE_STEP_MORE)
      return true;
    if (result == runtime_image::DECODE_STEP_DONE) {
      this->finish_();
    } else if (result == runtime_image::DECODE_STEP_NEED_DATA) {
      // A file has all its bytes there, so it is shorter than its size said
      this->fail_(LOG_STR("File ended before the image was complete"));
    } else {
      ESP_LOGE(TAG, "Decode failed: %s", runtime_image::decode_error_to_string(result));
      this->fail_(LOG_STR("Decode failed"));
    }
    return false;
  }
  if (!this->end_of_file_ && this->unread_ < this->read_buffer_size_) {
    const size_t want = std::min<size_t>(this->read_buffer_size_ - this->unread_, this->chunk_size_);
    const int32_t n = this->file_.read(this->read_buffer_ + this->unread_, want);
    if (n < 0) {
      ESP_LOGE(TAG, "Read failed: %s", LOG_STR_ARG(fs_error_to_string(static_cast<FsError>(n))));
      this->fail_(LOG_STR("Read failed"));
      return false;
    }
    this->end_of_file_ = n == 0;
    this->unread_ += n;
  }

  int consumed = 0;
  if (this->unread_ != 0) {
    consumed = this->feed_data(this->read_buffer_, this->unread_);
    if (consumed < 0) {
      ESP_LOGE(TAG, "Decode failed: %s", runtime_image::decode_error_to_string(consumed));
      this->fail_(LOG_STR("Decode failed"));
      return false;
    }
    this->unread_ -= consumed;
    if (consumed != 0 && this->unread_ != 0)
      memmove(this->read_buffer_, this->read_buffer_ + consumed, this->unread_);
  }

  if (this->is_decode_finished()) {
    this->finish_();
    return false;
  }
  if (consumed != 0)
    return true;
  if (this->end_of_file_) {
    // A decoder that counts pixels knows the image was cut off; PNG cannot tell
    if (this->unread_ == 0 && !this->decoder_knows_end()) {
      this->finish_();
    } else {
      this->fail_(LOG_STR("File ended before the image was complete"));
    }
    return false;
  }
  if (this->unread_ == this->read_buffer_size_) {
    this->fail_(LOG_STR("Image data does not fit the read buffer; increase chunk_size"));
    return false;
  }
  return true;
}

void FilesystemImage::finish_() {
  this->end_decode();
  ESP_LOGD(TAG, "Image loaded in %" PRIu32 " ms", millis() - this->start_time_);
  this->stop_();
  this->finished_callback_.call();
}

void FilesystemImage::fail_(const LogString *reason) {
  ESP_LOGE(TAG, "Image load failed: %s", LOG_STR_ARG(reason));
  if (this->is_decoding())
    RuntimeImage::release();
  this->stop_();
  this->error_callback_.call();
}

void FilesystemImage::stop_() {
  this->file_.close();
  this->unread_ = 0;
  this->reading_source_ = false;
  this->disable_loop();
}

void FilesystemImage::release() {
  this->stop_();
  this->free_buffer_();
  RuntimeImage::release();
}

}  // namespace esphome::filesystem
