#pragma once

#include "esphome/core/defines.h"

#ifdef XCSOAR_VFS_MAX_FILES

#include "esphome/components/filesystem/filesystem.h"

#include <array>
#include <cstdint>
#include <mutex>

#include <dirent.h>

namespace esphome::xcsoar {

/// Makes a filesystem::Filesystem usable through the C library (open(), stat(), opendir() and so on) below a
/// mount point, for code such as XCSoar that does its own file I/O.
///
/// Every call holds a lock, so XCSoar's tasks can share the filesystem; nothing else may use it at the same time.
/// An sd_spi card with a card detect pin is also polled from the main loop, so it needs no detect pin here.
class FilesystemVfs {
 public:
  explicit FilesystemVfs(filesystem::Filesystem *fs) : fs_(fs) {}

  /// Register with ESP-IDF's VFS at base_path (e.g. "/sdcard"). Returns false on error.
  bool mount(const char *base_path);

  /// Wait until the filesystem has mounted, e.g. while the card starts. Returns false on timeout.
  bool wait_ready(uint32_t timeout_ms);

 protected:
  struct OpenDir {
    DIR dir;  // must be first: ESP-IDF fills in its header
    filesystem::Dir handle;
    struct dirent entry;
  };

  filesystem::File *get_file_(int fd);

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

  filesystem::Filesystem *fs_;
  std::mutex mutex_;
  std::array<filesystem::File, XCSOAR_VFS_MAX_FILES> files_{};
  std::array<OpenDir, XCSOAR_VFS_MAX_DIRS> dirs_{};
};

}  // namespace esphome::xcsoar

#endif  // XCSOAR_VFS_MAX_FILES
