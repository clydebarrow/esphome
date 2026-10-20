#pragma once

#ifdef USE_ESP32

#include "esphome/components/filesystem/filesystem.h"

#include <diskio_impl.h>
#include <ff.h>

namespace esphome::fatfs {

/// FAT12, FAT16 and FAT32 through the FatFs library in ESP-IDF, on a block device with 512 byte blocks.
class FatFs : public filesystem::Filesystem {
 public:
  using filesystem::Filesystem::Filesystem;

  void dump_config() override;

 protected:
  filesystem::FsError fs_init(uint8_t max_files, uint8_t max_dirs) override;
  filesystem::FsError fs_mount() override;
  void fs_unmount() override;
  filesystem::FsError fs_format() override;
  filesystem::FsError fs_open(uint8_t slot, const char *path, uint8_t flags) override;
  int32_t fs_read(uint8_t slot, void *buf, uint32_t len) override;
  int32_t fs_write(uint8_t slot, const void *buf, uint32_t len) override;
  int32_t fs_seek(uint8_t slot, int32_t offset, filesystem::Whence whence) override;
  int32_t fs_tell(uint8_t slot) override;
  int32_t fs_size(uint8_t slot) override;
  filesystem::FsError fs_sync(uint8_t slot) override;
  filesystem::FsError fs_truncate(uint8_t slot) override;
  filesystem::FsError fs_close(uint8_t slot) override;
  filesystem::FsError fs_open_dir(uint8_t slot, const char *path) override;
  filesystem::FsError fs_read_dir(uint8_t slot, filesystem::FileInfo &info, char *name, size_t name_size) override;
  filesystem::FsError fs_close_dir(uint8_t slot) override;
  filesystem::FsError fs_remove(const char *path) override;
  filesystem::FsError fs_rename(const char *from, const char *to) override;
  filesystem::FsError fs_stat(const char *path, filesystem::FileInfo &info) override;
  filesystem::FsError fs_mkdir(const char *path) override;
  filesystem::FsError fs_space(uint64_t &total, uint64_t &free) override;

  /// Put the drive prefix in front of path. Returns false if it does not fit.
  bool full_path_(char *out, size_t out_size, const char *path) const;

  // The disk driver FatFs calls, one table entry per drive number
  static DSTATUS driver_status(BYTE pdrv);
  static DRESULT driver_read(BYTE pdrv, BYTE *buf, uint32_t sector, unsigned count);
  static DRESULT driver_write(BYTE pdrv, const BYTE *buf, uint32_t sector, unsigned count);
  static DRESULT driver_ioctl(BYTE pdrv, BYTE cmd, void *buf);

  FATFS fs_{};
  FIL *files_{nullptr};
  FF_DIR *dirs_{nullptr};
  BYTE pdrv_{0xFF};
};

}  // namespace esphome::fatfs

#endif  // USE_ESP32
