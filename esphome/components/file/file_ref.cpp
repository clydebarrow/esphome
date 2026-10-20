#include "file_ref.h"
#ifdef USE_FILE

#include <cstring>

#include "esphome/core/log.h"

namespace esphome::file {

ESPHOME_LOG_TAG(TAG, "file");

using filesystem::FileInfo;
using filesystem::FsError;

FileRef::FileRef(filesystem::Filesystem *fs, size_t max_path_length, const char *path)
    : fs_(fs), path_(std::make_unique<char[]>(max_path_length + 1)), max_path_length_(max_path_length) {
  if (path != nullptr)
    this->set_path(path);
}

bool FileRef::set_path(const char *path) {
  const size_t len = path == nullptr ? 0 : strlen(path);
  if (len == 0 || path[0] != '/' || len > this->max_path_length_) {
    ESP_LOGW(TAG, "Invalid path '%s', the limit is %u characters starting with '/'", path == nullptr ? "" : path,
             static_cast<unsigned>(this->max_path_length_));
    return false;
  }
  memcpy(this->path_.get(), path, len + 1);
  return true;
}

FsError FileRef::stat(FileInfo &info) const { return this->fs_->stat(this->target_(), info); }

bool FileRef::exists() const {
  FileInfo info;
  return this->stat(info) == FsError::FS_ERROR_NONE;
}

bool FileRef::is_file() const {
  FileInfo info;
  return this->stat(info) == FsError::FS_ERROR_NONE && !info.is_dir;
}

bool FileRef::is_directory() const {
  FileInfo info;
  return this->stat(info) == FsError::FS_ERROR_NONE && info.is_dir;
}

uint32_t FileRef::get_size() const {
  FileInfo info;
  return this->stat(info) == FsError::FS_ERROR_NONE ? info.size : 0;
}

ESPTime FileRef::get_modified() const {
  FileInfo info;
  if (this->stat(info) != FsError::FS_ERROR_NONE || info.mtime == 0)
    return ESPTime{};
  return ESPTime::from_epoch_local(info.mtime);
}

}  // namespace esphome::file

#endif  // USE_FILE
