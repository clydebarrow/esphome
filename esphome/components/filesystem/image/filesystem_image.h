#pragma once

#include "esphome/components/filesystem/filesystem.h"
#include "esphome/components/runtime_image/runtime_image.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#ifdef USE_FILE
#include "esphome/components/file/file_ref.h"
#endif

#include <string>

namespace esphome::filesystem {

/// Lets a decoder read an open file itself
class FileSource : public runtime_image::DecodeSource {
 public:
  explicit FileSource(File &file) : file_(file) {}
  int32_t read(uint8_t *buf, uint32_t len) override { return this->file_.read(buf, len); }
  bool seek(uint32_t position) override { return this->file_.seek(static_cast<int32_t>(position)) >= 0; }

 protected:
  File &file_;
};

/** An image decoded at run time from a file.
 *
 * load() opens the file and returns; the file is then read and decoded in loop(), a few steps per pass, so the
 * main loop is never held up for long. A decoder that can read the file itself (JPEG) does so a step at a time;
 * the others are fed chunks of chunk_size bytes. update() loads the configured path, or the current path of the
 * file set with set_file().
 */
class FilesystemImage : public PollingComponent, public runtime_image::RuntimeImage {
 public:
  FilesystemImage(Filesystem *fs, const char *path, int width, int height, runtime_image::ImageFormat format,
                  image::ImageType type, image::Transparency transparency, image::Image *placeholder,
                  uint32_t chunk_size, bool is_big_endian)
      : RuntimeImage(format, type, transparency, placeholder, is_big_endian, width, height),
        fs_(fs),
        path_(path),
        chunk_size_(chunk_size) {}

  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;

  /// Start loading a file. Progress is reported through the finished and error callbacks.
  void load(const char *path);
  void load(const std::string &path) { this->load(path.c_str()); }
  /// Stop any load in progress and free the image.
  void release();
#ifdef USE_FILE
  void set_file(file::FileRef *file) { this->file_ref_ = file; }
#endif

  template<typename F> void add_on_load_finished_callback(F &&callback) {
    this->finished_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_error_callback(F &&callback) {
    this->error_callback_.add(std::forward<F>(callback));
  }

 protected:
  /// One read and decode step. Returns false when the load has ended, either way.
  bool step_();
  void finish_();
  void fail_(const LogString *reason);
  void stop_();
  bool allocate_buffer_();
  void free_buffer_();
  /// The path update() loads, or nullptr when there is none.
  const char *update_path_() const;

  LazyCallbackManager<void()> finished_callback_;
  LazyCallbackManager<void()> error_callback_;
  Filesystem *fs_;
  const char *path_;
#ifdef USE_FILE
  file::FileRef *file_ref_{nullptr};
#endif
  File file_;
  FileSource source_{file_};
  uint8_t *read_buffer_{nullptr};  // Kept between loads so they do not churn the heap
  size_t read_buffer_size_{0};
  size_t unread_{0};
  uint32_t chunk_size_;
  uint32_t start_time_{0};
  bool end_of_file_{false};
  bool reading_source_{false};  // The decoder reads the file itself
};

}  // namespace esphome::filesystem
