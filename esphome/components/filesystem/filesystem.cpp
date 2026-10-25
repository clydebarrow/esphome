#include "filesystem.h"
#include "vfs.h"

#include <utility>

namespace esphome::filesystem {

ESPHOME_LOG_TAG(TAG, "filesystem");

static constexpr uint8_t MAX_FILE_SLOTS = 32;
static constexpr uint8_t MAX_DIR_SLOTS = 8;

const LogString *fs_error_to_string(FsError error) {
  switch (error) {
    case FsError::FS_ERROR_NONE:
      return LOG_STR("none");
    case FsError::FS_ERROR_IO:
      return LOG_STR("I/O error");
    case FsError::FS_ERROR_NO_MEDIA:
      return LOG_STR("no media");
    case FsError::FS_ERROR_NOT_FOUND:
      return LOG_STR("not found");
    case FsError::FS_ERROR_EXISTS:
      return LOG_STR("already exists");
    case FsError::FS_ERROR_NOT_DIR:
      return LOG_STR("not a directory");
    case FsError::FS_ERROR_IS_DIR:
      return LOG_STR("is a directory");
    case FsError::FS_ERROR_NO_SPACE:
      return LOG_STR("no space");
    case FsError::FS_ERROR_TOO_MANY_OPEN:
      return LOG_STR("too many open");
    case FsError::FS_ERROR_INVALID:
      return LOG_STR("invalid argument");
    case FsError::FS_ERROR_NO_FILESYSTEM:
      return LOG_STR("no filesystem");
    case FsError::FS_ERROR_READ_ONLY:
      return LOG_STR("read only");
    case FsError::FS_ERROR_DENIED:
      return LOG_STR("access denied");
    case FsError::FS_ERROR_CLOSED:
      return LOG_STR("closed");
  }
  return LOG_STR("unknown");
}

// File

File &File::operator=(File &&other) noexcept {
  if (this != &other) {
    this->close();
    this->take_(other);
  }
  return *this;
}

void File::take_(File &other) {
  this->fs_ = std::exchange(other.fs_, nullptr);
  this->slot_ = other.slot_;
  this->mount_ = other.mount_;
}

int32_t File::read(void *buf, uint32_t len) {
  if (this->fs_ == nullptr || !this->fs_->file_valid_(this->mount_))
    return static_cast<int32_t>(FsError::FS_ERROR_CLOSED);
  return this->fs_->fs_read(this->slot_, buf, len);
}

int32_t File::write(const void *buf, uint32_t len) {
  if (this->fs_ == nullptr || !this->fs_->file_valid_(this->mount_))
    return static_cast<int32_t>(FsError::FS_ERROR_CLOSED);
  return this->fs_->fs_write(this->slot_, buf, len);
}

int32_t File::seek(int32_t offset, Whence whence) {
  if (this->fs_ == nullptr || !this->fs_->file_valid_(this->mount_))
    return static_cast<int32_t>(FsError::FS_ERROR_CLOSED);
  return this->fs_->fs_seek(this->slot_, offset, whence);
}

int32_t File::tell() {
  if (this->fs_ == nullptr || !this->fs_->file_valid_(this->mount_))
    return static_cast<int32_t>(FsError::FS_ERROR_CLOSED);
  return this->fs_->fs_tell(this->slot_);
}

int32_t File::size() {
  if (this->fs_ == nullptr || !this->fs_->file_valid_(this->mount_))
    return static_cast<int32_t>(FsError::FS_ERROR_CLOSED);
  return this->fs_->fs_size(this->slot_);
}

FsError File::sync() {
  if (this->fs_ == nullptr || !this->fs_->file_valid_(this->mount_))
    return FsError::FS_ERROR_CLOSED;
  return this->fs_->fs_sync(this->slot_);
}

FsError File::truncate() {
  if (this->fs_ == nullptr || !this->fs_->file_valid_(this->mount_))
    return FsError::FS_ERROR_CLOSED;
  return this->fs_->fs_truncate(this->slot_);
}

FsError File::close() {
  Filesystem *fs = std::exchange(this->fs_, nullptr);
  if (fs == nullptr)
    return FsError::FS_ERROR_NONE;
  return fs->release_file_(this->slot_, this->mount_);
}

// Dir

Dir &Dir::operator=(Dir &&other) noexcept {
  if (this != &other) {
    this->close();
    this->take_(other);
  }
  return *this;
}

void Dir::take_(Dir &other) {
  this->fs_ = std::exchange(other.fs_, nullptr);
  this->slot_ = other.slot_;
  this->mount_ = other.mount_;
}

FsError Dir::read_next(FileInfo &info, char *name, size_t name_size) {
  if (this->fs_ == nullptr || !this->fs_->file_valid_(this->mount_))
    return FsError::FS_ERROR_CLOSED;
  if (name == nullptr || name_size == 0)
    return FsError::FS_ERROR_INVALID;
  return this->fs_->fs_read_dir(this->slot_, info, name, name_size);
}

FsError Dir::close() {
  Filesystem *fs = std::exchange(this->fs_, nullptr);
  if (fs == nullptr)
    return FsError::FS_ERROR_NONE;
  return fs->release_dir_(this->slot_, this->mount_);
}

// Filesystem

void Filesystem::setup() {
  if (this->max_files_ > MAX_FILE_SLOTS)
    this->max_files_ = MAX_FILE_SLOTS;
  if (this->max_dirs_ > MAX_DIR_SLOTS)
    this->max_dirs_ = MAX_DIR_SLOTS;
  FsError err = this->fs_init(this->max_files_, this->max_dirs_);
  if (err != FsError::FS_ERROR_NONE) {
    this->mark_failed(fs_error_to_string(err));
    return;
  }
#ifdef USE_FILESYSTEM_VFS
  if (this->vfs_ != nullptr && !this->vfs_->mount()) {
    this->mark_failed(LOG_STR("VFS registration failed"));
    return;
  }
#endif
  if (this->device_->has_media())
    this->ready_();
}

void Filesystem::dump_config() {
  ESP_LOGCONFIG(TAG,
                "  Mounted: %s\n"
                "  Open files: %u\n"
                "  Open directories: %u\n"
                "  Format if mount fails: %s",
                YESNO(this->mounted_), this->max_files_, this->max_dirs_, YESNO(this->format_if_mount_failed_));
}

FsError Filesystem::mount() {
  if (this->is_failed())
    return FsError::FS_ERROR_INVALID;
  this->mount_error_ = FsError::FS_ERROR_NONE;
  return this->ready_();
}

void Filesystem::unmount() {
  if (!this->mounted_)
    return;
  // Close what is still open so buffered data reaches the media while it is there. After a media change the
  // buffers belong to the old media; writing them would damage whatever is in the slot now.
  if (this->media_current_()) {
    for (uint8_t slot = 0; slot != this->max_files_; slot++) {
      if (this->files_in_use_ & (1UL << slot))
        this->fs_close(slot);
    }
    for (uint8_t slot = 0; slot != this->max_dirs_; slot++) {
      if (this->dirs_in_use_ & (1U << slot))
        this->fs_close_dir(slot);
    }
  }
  this->files_in_use_ = 0;
  this->dirs_in_use_ = 0;
  this->fs_unmount();
  this->mounted_ = false;
  ESP_LOGD(TAG, "Unmounted");
}

FsError Filesystem::ready_() {
  if (this->mounted_ && this->media_current_())
    return FsError::FS_ERROR_NONE;
  if (this->mounted_) {
    ESP_LOGI(TAG, "Media changed");
    this->unmount();
  }
  if (!this->device_->has_media())
    return FsError::FS_ERROR_NO_MEDIA;
  if (this->mount_error_ != FsError::FS_ERROR_NONE && this->media_current_())
    return this->mount_error_;
  this->media_generation_ = this->device_->get_media_generation();
  FsError err = this->fs_mount();
  if (err == FsError::FS_ERROR_NO_FILESYSTEM && this->format_if_mount_failed_) {
    ESP_LOGW(TAG, "No filesystem found, formatting");
    err = this->fs_format();
    if (err == FsError::FS_ERROR_NONE)
      err = this->fs_mount();
  }
  if (err != FsError::FS_ERROR_NONE) {
    ESP_LOGW(TAG, "Mount failed: %s", LOG_STR_ARG(fs_error_to_string(err)));
    this->mount_error_ = err;
    return err;
  }
  this->mount_error_ = FsError::FS_ERROR_NONE;
  this->mount_count_++;
  this->mounted_ = true;
  ESP_LOGD(TAG, "Mounted");
  return FsError::FS_ERROR_NONE;
}

File Filesystem::open(const char *path, uint8_t flags) {
  FsError err = this->ready_();
  if (err != FsError::FS_ERROR_NONE) {
    this->fail_(err);
    return {};
  }
  if (path == nullptr || (flags & (OPEN_READ | OPEN_WRITE)) == 0) {
    this->fail_(FsError::FS_ERROR_INVALID);
    return {};
  }
  for (uint8_t slot = 0; slot != this->max_files_; slot++) {
    if (this->files_in_use_ & (1UL << slot))
      continue;
    err = this->fs_open(slot, path, flags);
    if (err != FsError::FS_ERROR_NONE) {
      this->fail_(err);
      return {};
    }
    this->files_in_use_ |= 1UL << slot;
    this->last_error_ = FsError::FS_ERROR_NONE;
    return {this, slot, this->mount_count_};
  }
  this->fail_(FsError::FS_ERROR_TOO_MANY_OPEN);
  return {};
}

Dir Filesystem::open_dir(const char *path) {
  FsError err = this->ready_();
  if (err != FsError::FS_ERROR_NONE) {
    this->fail_(err);
    return {};
  }
  if (path == nullptr) {
    this->fail_(FsError::FS_ERROR_INVALID);
    return {};
  }
  for (uint8_t slot = 0; slot != this->max_dirs_; slot++) {
    if (this->dirs_in_use_ & (1U << slot))
      continue;
    err = this->fs_open_dir(slot, path);
    if (err != FsError::FS_ERROR_NONE) {
      this->fail_(err);
      return {};
    }
    this->dirs_in_use_ |= 1U << slot;
    this->last_error_ = FsError::FS_ERROR_NONE;
    return {this, slot, this->mount_count_};
  }
  this->fail_(FsError::FS_ERROR_TOO_MANY_OPEN);
  return {};
}

FsError Filesystem::release_file_(uint8_t slot, uint8_t mount) {
  // A handle from an earlier mount has no driver state left; its slot was freed by unmount()
  if (!this->file_valid_(mount))
    return FsError::FS_ERROR_CLOSED;
  this->files_in_use_ &= ~(1UL << slot);
  return this->fs_close(slot);
}

FsError Filesystem::release_dir_(uint8_t slot, uint8_t mount) {
  if (!this->file_valid_(mount))
    return FsError::FS_ERROR_CLOSED;
  this->dirs_in_use_ &= ~(1U << slot);
  return this->fs_close_dir(slot);
}

FsError Filesystem::remove(const char *path) {
  FsError err = this->ready_();
  if (err == FsError::FS_ERROR_NONE)
    err = path == nullptr ? FsError::FS_ERROR_INVALID : this->fs_remove(path);
  return this->fail_(err);
}

FsError Filesystem::rename(const char *from, const char *to) {
  FsError err = this->ready_();
  if (err == FsError::FS_ERROR_NONE)
    err = from == nullptr || to == nullptr ? FsError::FS_ERROR_INVALID : this->fs_rename(from, to);
  return this->fail_(err);
}

FsError Filesystem::stat(const char *path, FileInfo &info) {
  FsError err = this->ready_();
  if (err == FsError::FS_ERROR_NONE)
    err = path == nullptr ? FsError::FS_ERROR_INVALID : this->fs_stat(path, info);
  return this->fail_(err);
}

FsError Filesystem::mkdir(const char *path) {
  FsError err = this->ready_();
  if (err == FsError::FS_ERROR_NONE)
    err = path == nullptr ? FsError::FS_ERROR_INVALID : this->fs_mkdir(path);
  return this->fail_(err);
}

FsError Filesystem::space(uint64_t &total, uint64_t &free) {
  FsError err = this->ready_();
  if (err == FsError::FS_ERROR_NONE)
    err = this->fs_space(total, free);
  return this->fail_(err);
}

FsError Filesystem::format() {
  if (this->is_failed())
    return this->fail_(FsError::FS_ERROR_INVALID);
  this->unmount();
  if (!this->device_->has_media())
    return this->fail_(FsError::FS_ERROR_NO_MEDIA);
  FsError err = this->fs_format();
  if (err == FsError::FS_ERROR_NONE) {
    this->mount_error_ = FsError::FS_ERROR_NONE;
    err = this->ready_();
  }
  return this->fail_(err);
}

}  // namespace esphome::filesystem
