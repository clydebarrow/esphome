#include "vfs.h"

#ifdef USE_FILESYSTEM_VFS

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_vfs.h>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace esphome::filesystem {

ESPHOME_LOG_TAG(TAG, "filesystem.vfs");

static constexpr uint32_t READY_POLL_MS = 100;

static int to_errno(FsError error) {
  switch (error) {
    case FsError::FS_ERROR_NONE:
      return 0;
    case FsError::FS_ERROR_NOT_FOUND:
      return ENOENT;
    case FsError::FS_ERROR_EXISTS:
      return EEXIST;
    case FsError::FS_ERROR_NOT_DIR:
      return ENOTDIR;
    case FsError::FS_ERROR_IS_DIR:
      return EISDIR;
    case FsError::FS_ERROR_NO_SPACE:
      return ENOSPC;
    case FsError::FS_ERROR_TOO_MANY_OPEN:
      return ENFILE;
    case FsError::FS_ERROR_INVALID:
      return EINVAL;
    case FsError::FS_ERROR_READ_ONLY:
      return EROFS;
    case FsError::FS_ERROR_DENIED:
      return EACCES;
    case FsError::FS_ERROR_CLOSED:
      return EBADF;
    case FsError::FS_ERROR_NO_MEDIA:
    case FsError::FS_ERROR_NO_FILESYSTEM:
      return ENODEV;
    case FsError::FS_ERROR_IO:
    default:
      return EIO;
  }
}

/// Set errno from error and return -1, or return 0 for no error.
static int fail_with(FsError error) {
  if (error == FsError::FS_ERROR_NONE)
    return 0;
  errno = to_errno(error);
  return -1;
}

/// The same for a byte count or position, which is a negative FsError on failure.
static int32_t fail_with(int32_t result) {
  if (result >= 0)
    return result;
  errno = to_errno(static_cast<FsError>(result));
  return -1;
}

static void fill_stat(const FileInfo &info, struct stat *st) {
  memset(st, 0, sizeof(*st));
  st->st_mode = info.is_dir ? (S_IFDIR | 0777) : (S_IFREG | 0666);
  st->st_size = info.size;
  st->st_mtime = info.mtime;
  st->st_atime = info.mtime;
  st->st_ctime = info.mtime;
}

bool Vfs::mount() {
  this->files_.init(this->max_files_);
  for (uint8_t i = 0; i != this->max_files_; i++)
    this->files_.emplace_back();
  this->dirs_.init(this->max_dirs_);
  for (uint8_t i = 0; i != this->max_dirs_; i++)
    this->dirs_.emplace_back();
  esp_vfs_t vfs{};
  vfs.flags = ESP_VFS_FLAG_CONTEXT_PTR;
  vfs.open_p = vfs_open;
  vfs.read_p = vfs_read;
  vfs.write_p = vfs_write;
  vfs.lseek_p = vfs_lseek;
  vfs.close_p = vfs_close;
  vfs.fstat_p = vfs_fstat;
  vfs.fsync_p = vfs_fsync;
  vfs.ftruncate_p = vfs_ftruncate;
  vfs.stat_p = vfs_stat;
  vfs.access_p = vfs_access;
  vfs.unlink_p = vfs_unlink;
  vfs.rename_p = vfs_rename;
  vfs.mkdir_p = vfs_mkdir;
  // the filesystem removes empty directories the same way as files
  vfs.rmdir_p = vfs_unlink;
  vfs.opendir_p = vfs_opendir;
  vfs.readdir_p = vfs_readdir;
  vfs.closedir_p = vfs_closedir;
  const esp_err_t err = esp_vfs_register(this->base_path_, &vfs, this);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Registering %s failed: %s", this->base_path_, esp_err_to_name(err));
    return false;
  }
  return true;
}

bool Vfs::wait_ready(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (true) {
    FileInfo info;
    if (this->fs_->stat("/", info) == FsError::FS_ERROR_NONE)
      return true;
    if (millis() - start >= timeout_ms)
      return false;
    delay(READY_POLL_MS);
  }
}

File *Vfs::get_file_(int fd) {
  if (fd < 0 || static_cast<size_t>(fd) >= this->files_.size() || !this->files_[fd].is_open()) {
    errno = EBADF;
    return nullptr;
  }
  return &this->files_[fd];
}

int Vfs::vfs_open(void *ctx, const char *path, int flags, int mode) {
  auto *self = static_cast<Vfs *>(ctx);
  uint8_t fs_flags = 0;
  switch (flags & O_ACCMODE) {
    case O_RDONLY:
      fs_flags = OPEN_READ;
      break;
    case O_WRONLY:
      fs_flags = OPEN_WRITE;
      break;
    default:
      fs_flags = OPEN_READ | OPEN_WRITE;
      break;
  }
  if (flags & O_CREAT)
    fs_flags |= OPEN_CREATE;
  if (flags & O_TRUNC)
    fs_flags |= OPEN_TRUNCATE;
  if (flags & O_APPEND)
    fs_flags |= OPEN_APPEND;
  if (flags & O_EXCL)
    fs_flags |= OPEN_EXCLUSIVE;

  const std::lock_guard lock{self->fs_->mutex()};
  for (size_t fd = 0; fd != self->files_.size(); fd++) {
    if (self->files_[fd].is_open())
      continue;
    self->files_[fd] = self->fs_->open(path, fs_flags);
    if (!self->files_[fd])
      return fail_with(self->fs_->last_error());
    return static_cast<int>(fd);
  }
  errno = ENFILE;
  return -1;
}

ssize_t Vfs::vfs_read(void *ctx, int fd, void *dst, size_t size) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->read(dst, size));
}

ssize_t Vfs::vfs_write(void *ctx, int fd, const void *data, size_t size) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->write(data, size));
}

off_t Vfs::vfs_lseek(void *ctx, int fd, off_t offset, int mode) {
  auto *self = static_cast<Vfs *>(ctx);
  Whence whence;
  switch (mode) {
    case SEEK_SET:
      whence = Whence::WHENCE_SET;
      break;
    case SEEK_CUR:
      whence = Whence::WHENCE_CURRENT;
      break;
    case SEEK_END:
      whence = Whence::WHENCE_END;
      break;
    default:
      errno = EINVAL;
      return -1;
  }
  const std::lock_guard lock{self->fs_->mutex()};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->seek(offset, whence));
}

int Vfs::vfs_close(void *ctx, int fd) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->close());
}

int Vfs::vfs_fstat(void *ctx, int fd, struct stat *st) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  const int32_t size = file->size();
  if (size < 0)
    return fail_with(size);
  FileInfo info;
  info.size = size;
  fill_stat(info, st);
  return 0;
}

int Vfs::vfs_fsync(void *ctx, int fd) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->sync());
}

int Vfs::vfs_ftruncate(void *ctx, int fd, off_t length) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  // The filesystem cuts at the current position
  const int32_t position = file->tell();
  if (position < 0)
    return fail_with(position);
  int32_t result = file->seek(length);
  if (result >= 0)
    result = static_cast<int32_t>(file->truncate());
  file->seek(position);
  return fail_with(result);
}

int Vfs::vfs_stat(void *ctx, const char *path, struct stat *st) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  FileInfo info;
  const FsError error = self->fs_->stat(path, info);
  if (error != FsError::FS_ERROR_NONE)
    return fail_with(error);
  fill_stat(info, st);
  return 0;
}

int Vfs::vfs_access(void *ctx, const char *path, int amode) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  FileInfo info;
  return fail_with(self->fs_->stat(path, info));
}

int Vfs::vfs_unlink(void *ctx, const char *path) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  return fail_with(self->fs_->remove(path));
}

int Vfs::vfs_rename(void *ctx, const char *from, const char *to) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  FsError err = self->fs_->rename(from, to);
  // POSIX rename() replaces an existing file, which FAT does not; XCSoar saves files by renaming over them
  FileInfo info;
  if (err == FsError::FS_ERROR_EXISTS && self->fs_->stat(to, info) == FsError::FS_ERROR_NONE && !info.is_dir &&
      self->fs_->remove(to) == FsError::FS_ERROR_NONE)
    err = self->fs_->rename(from, to);
  return fail_with(err);
}

int Vfs::vfs_mkdir(void *ctx, const char *path, mode_t mode) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  return fail_with(self->fs_->mkdir(path));
}

DIR *Vfs::vfs_opendir(void *ctx, const char *path) {
  auto *self = static_cast<Vfs *>(ctx);
  const std::lock_guard lock{self->fs_->mutex()};
  for (auto &open_dir : self->dirs_) {
    if (open_dir.handle.is_open())
      continue;
    open_dir.handle = self->fs_->open_dir(path);
    if (!open_dir.handle) {
      fail_with(self->fs_->last_error());
      return nullptr;
    }
    return &open_dir.dir;
  }
  errno = ENFILE;
  return nullptr;
}

struct dirent *Vfs::vfs_readdir(void *ctx, DIR *pdir) {
  auto *self = static_cast<Vfs *>(ctx);
  auto *open_dir = reinterpret_cast<OpenDir *>(pdir);
  const std::lock_guard lock{self->fs_->mutex()};
  FileInfo info;
  struct dirent &entry = open_dir->entry;
  const FsError error = open_dir->handle.read_next(info, entry.d_name, sizeof(entry.d_name));
  if (error != FsError::FS_ERROR_NONE) {
    // The end of the directory is not an error
    if (error != FsError::FS_ERROR_NOT_FOUND)
      fail_with(error);
    return nullptr;
  }
  entry.d_ino = 0;
  entry.d_type = info.is_dir ? DT_DIR : DT_REG;
  return &entry;
}

int Vfs::vfs_closedir(void *ctx, DIR *pdir) {
  auto *self = static_cast<Vfs *>(ctx);
  auto *open_dir = reinterpret_cast<OpenDir *>(pdir);
  const std::lock_guard lock{self->fs_->mutex()};
  return fail_with(open_dir->handle.close());
}

}  // namespace esphome::filesystem

#endif  // USE_FILESYSTEM_VFS
