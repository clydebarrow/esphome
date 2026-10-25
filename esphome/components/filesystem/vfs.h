#pragma once

#include "esphome/core/defines.h"

#ifdef USE_FILESYSTEM_VFS

#include "esphome/core/helpers.h"
#include "filesystem.h"

#include <cstdint>
#include <mutex>

#include <dirent.h>

namespace esphome::filesystem {

/// Makes a Filesystem usable through the C library (open(), stat(), opendir() and so on) below a mount point,
/// for code that does its own file I/O, on any task.
///
/// Every call holds mutex(). Filesystem calls are not thread safe, so any other user of the same filesystem
/// must hold it too.
class Vfs {
 public:
  Vfs(Filesystem *fs, const char *base_path) : fs_(fs), base_path_(base_path) {}

  void set_pool_sizes(uint8_t max_files, uint8_t max_dirs) {
    this->max_files_ = max_files;
    this->max_dirs_ = max_dirs;
  }

  /// Register with ESP-IDF's VFS. Returns false on error.
  bool mount();

  /// Wait until the filesystem has mounted, e.g. while the card starts. Returns false on timeout.
  bool wait_ready(uint32_t timeout_ms);

  const char *get_base_path() const { return this->base_path_; }
  std::mutex &mutex() { return this->mutex_; }

 protected:
  struct OpenDir {
    DIR dir;  // must be first: ESP-IDF fills in its header
    Dir handle;
    struct dirent entry;
  };

  File *get_file_(int fd);

  // The VFS hooks, called with this object as the context
  static int vfs_open(void *ctx, const char *path, int flags, int mode);
  static ssize_t vfs_read(void *ctx, int fd, void *dst, size_t size);
  static ssize_t vfs_write(void *ctx, int fd, const void *data, size_t size);
  static off_t vfs_lseek(void *ctx, int fd, off_t offset, int mode);
  static int vfs_close(void *ctx, int fd);
  static int vfs_fstat(void *ctx, int fd, struct stat *st);
  static int vfs_fsync(void *ctx, int fd);
  static int vfs_ftruncate(void *ctx, int fd, off_t length);
  static int vfs_stat(void *ctx, const char *path, struct stat *st);
  static int vfs_access(void *ctx, const char *path, int amode);
  static int vfs_unlink(void *ctx, const char *path);
  static int vfs_rename(void *ctx, const char *from, const char *to);
  static int vfs_mkdir(void *ctx, const char *path, mode_t mode);
  static DIR *vfs_opendir(void *ctx, const char *path);
  static struct dirent *vfs_readdir(void *ctx, DIR *pdir);
  static int vfs_closedir(void *ctx, DIR *pdir);

  Filesystem *fs_;
  const char *base_path_;
  std::mutex mutex_;
  FixedVector<File> files_;
  FixedVector<OpenDir> dirs_;
  uint8_t max_files_{4};
  uint8_t max_dirs_{2};
};

}  // namespace esphome::filesystem

#endif  // USE_FILESYSTEM_VFS
