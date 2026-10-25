#include "filesystem_vfs.h"

#ifdef XCSOAR_VFS_MAX_FILES

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_vfs.h>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace esphome::xcsoar {

ESPHOME_LOG_TAG(TAG, "xcsoar.vfs");

using filesystem::FsError;

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

static void fill_stat(const filesystem::FileInfo &info, struct stat *st) {
  memset(st, 0, sizeof(*st));
  st->st_mode = info.is_dir ? (S_IFDIR | 0777) : (S_IFREG | 0666);
  st->st_size = info.size;
  st->st_mtime = info.mtime;
  st->st_atime = info.mtime;
  st->st_ctime = info.mtime;
}

bool FilesystemVfs::mount(const char *base_path) {
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
  const esp_err_t err = esp_vfs_register(base_path, &vfs, this);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Registering %s failed: %s", base_path, esp_err_to_name(err));
    return false;
  }
  return true;
}

bool FilesystemVfs::wait_ready(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (true) {
    {
      const std::lock_guard lock{this->mutex_};
      filesystem::FileInfo info;
      if (this->fs_->stat("/", info) == FsError::FS_ERROR_NONE)
        return true;
    }
    if (millis() - start >= timeout_ms)
      return false;
    delay(READY_POLL_MS);
  }
}

filesystem::File *FilesystemVfs::get_file_(int fd) {
  if (fd < 0 || static_cast<size_t>(fd) >= this->files_.size() || !this->files_[fd].is_open()) {
    errno = EBADF;
    return nullptr;
  }
  return &this->files_[fd];
}

int FilesystemVfs::vfs_open(void *ctx, const char *path, int flags, int mode) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  uint8_t fs_flags = 0;
  switch (flags & O_ACCMODE) {
    case O_RDONLY:
      fs_flags = filesystem::OPEN_READ;
      break;
    case O_WRONLY:
      fs_flags = filesystem::OPEN_WRITE;
      break;
    default:
      fs_flags = filesystem::OPEN_READ | filesystem::OPEN_WRITE;
      break;
  }
  if (flags & O_CREAT)
    fs_flags |= filesystem::OPEN_CREATE;
  if (flags & O_TRUNC)
    fs_flags |= filesystem::OPEN_TRUNCATE;
  if (flags & O_APPEND)
    fs_flags |= filesystem::OPEN_APPEND;
  if (flags & O_EXCL)
    fs_flags |= filesystem::OPEN_EXCLUSIVE;

  const std::lock_guard lock{self->mutex_};
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

ssize_t FilesystemVfs::vfs_read(void *ctx, int fd, void *dst, size_t size) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->read(dst, size));
}

ssize_t FilesystemVfs::vfs_write(void *ctx, int fd, const void *data, size_t size) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->write(data, size));
}

off_t FilesystemVfs::vfs_lseek(void *ctx, int fd, off_t offset, int mode) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  filesystem::Whence whence;
  switch (mode) {
    case SEEK_SET:
      whence = filesystem::Whence::WHENCE_SET;
      break;
    case SEEK_CUR:
      whence = filesystem::Whence::WHENCE_CURRENT;
      break;
    case SEEK_END:
      whence = filesystem::Whence::WHENCE_END;
      break;
    default:
      errno = EINVAL;
      return -1;
  }
  const std::lock_guard lock{self->mutex_};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->seek(offset, whence));
}

int FilesystemVfs::vfs_close(void *ctx, int fd) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->close());
}

int FilesystemVfs::vfs_fstat(void *ctx, int fd, struct stat *st) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  const int32_t size = file->size();
  if (size < 0)
    return fail_with(size);
  filesystem::FileInfo info;
  info.size = size;
  fill_stat(info, st);
  return 0;
}

int FilesystemVfs::vfs_fsync(void *ctx, int fd) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  auto *file = self->get_file_(fd);
  if (file == nullptr)
    return -1;
  return fail_with(file->sync());
}

int FilesystemVfs::vfs_ftruncate(void *ctx, int fd, off_t length) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
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

int FilesystemVfs::vfs_stat(void *ctx, const char *path, struct stat *st) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  filesystem::FileInfo info;
  const FsError error = self->fs_->stat(path, info);
  if (error != FsError::FS_ERROR_NONE)
    return fail_with(error);
  fill_stat(info, st);
  return 0;
}

int FilesystemVfs::vfs_access(void *ctx, const char *path, int amode) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  filesystem::FileInfo info;
  return fail_with(self->fs_->stat(path, info));
}

int FilesystemVfs::vfs_unlink(void *ctx, const char *path) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  return fail_with(self->fs_->remove(path));
}

int FilesystemVfs::vfs_rename(void *ctx, const char *from, const char *to) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  return fail_with(self->fs_->rename(from, to));
}

int FilesystemVfs::vfs_mkdir(void *ctx, const char *path, mode_t mode) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
  return fail_with(self->fs_->mkdir(path));
}

DIR *FilesystemVfs::vfs_opendir(void *ctx, const char *path) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  const std::lock_guard lock{self->mutex_};
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

struct dirent *FilesystemVfs::vfs_readdir(void *ctx, DIR *pdir) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  auto *open_dir = reinterpret_cast<OpenDir *>(pdir);
  const std::lock_guard lock{self->mutex_};
  filesystem::FileInfo info;
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

int FilesystemVfs::vfs_closedir(void *ctx, DIR *pdir) {
  auto *self = static_cast<FilesystemVfs *>(ctx);
  auto *open_dir = reinterpret_cast<OpenDir *>(pdir);
  const std::lock_guard lock{self->mutex_};
  return fail_with(open_dir->handle.close());
}

}  // namespace esphome::xcsoar

#endif  // XCSOAR_VFS_MAX_FILES
