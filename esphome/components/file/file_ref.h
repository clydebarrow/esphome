#pragma once

#include "esphome/core/defines.h"
#ifdef USE_FILE

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "esphome/components/filesystem/filesystem.h"
#include "esphome/core/time.h"

namespace esphome::file {

/** A path on a filesystem, which may or may not name an existing file or directory.
 *
 * Nothing is cached: each query asks the filesystem, so it sees changes made elsewhere and a change of media.
 * A query that fails, including for a path that was never set, reports no file; the filesystem's
 * last_error() says why.
 */
class FileRef {
 public:
  FileRef(filesystem::Filesystem *fs, size_t max_path_length, const char *path);

  /// Change the path. Returns false, keeping the old path, unless it is absolute and at most max_path_length long.
  bool set_path(const char *path);
  bool set_path(const std::string &path) { return this->set_path(path.c_str()); }
  const char *get_path() const { return this->path_.get(); }
  filesystem::Filesystem *get_filesystem() const { return this->fs_; }

  /// Fetch size, modification time and type in one call.
  filesystem::FsError stat(filesystem::FileInfo &info) const;
  bool exists() const;
  bool is_file() const;
  bool is_directory() const;
  /// The size in bytes, or 0 when there is no file.
  uint32_t get_size() const;
  /// The last modification time in local time; not is_valid() when there is no file or it is not recorded.
  ESPTime get_modified() const;

  filesystem::File open(uint8_t flags) const { return this->fs_->open(this->target_(), flags); }
  filesystem::FsError remove() const { return this->fs_->remove(this->target_()); }

 protected:
  // An unset path is empty, which a driver may take as the root directory; the filesystem refuses nullptr
  const char *target_() const { return this->path_[0] == 0 ? nullptr : this->path_.get(); }

  filesystem::Filesystem *fs_;
  std::unique_ptr<char[]> path_;
  size_t max_path_length_;
};

}  // namespace esphome::file

#endif  // USE_FILE
