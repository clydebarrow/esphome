#ifdef USE_ESP32

#include "fatfs.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

namespace esphome::fatfs {

ESPHOME_LOG_TAG(TAG, "fatfs");

using filesystem::FileInfo;
using filesystem::FsError;
using filesystem::Whence;
using storage::StorageError;

static constexpr size_t MAX_PATH = 260;
static constexpr size_t FORMAT_BUFFER_SIZE = 4096;

static FatFs *drives[FF_VOLUMES];  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// FAT stores local time with no zone, in two second steps. Both directions use ESPHome's time zone, which the
// C library's localtime() and mktime() do not know about.
static time_t fat_time_to_time(WORD fdate, WORD ftime) {
  if (fdate == 0)
    return 0;
  ESPTime local{};
  local.year = (fdate >> 9) + 1980;
  local.month = (fdate >> 5) & 0x0F;
  local.day_of_month = fdate & 0x1F;
  local.hour = ftime >> 11;
  local.minute = (ftime >> 5) & 0x3F;
  local.second = (ftime & 0x1F) * 2;
  local.recalc_timestamp_local();
  return local.timestamp == -1 ? 0 : local.timestamp;
}

static FsError to_fs_error(FRESULT res) {
  switch (res) {
    case FR_OK:
      return FsError::FS_ERROR_NONE;
    case FR_NOT_READY:
      return FsError::FS_ERROR_NO_MEDIA;
    case FR_NO_FILE:
    case FR_NO_PATH:
      return FsError::FS_ERROR_NOT_FOUND;
    case FR_INVALID_NAME:
    case FR_INVALID_DRIVE:
    case FR_INVALID_PARAMETER:
      return FsError::FS_ERROR_INVALID;
    case FR_DENIED:
    case FR_LOCKED:
      return FsError::FS_ERROR_DENIED;
    case FR_EXIST:
      return FsError::FS_ERROR_EXISTS;
    case FR_INVALID_OBJECT:
      return FsError::FS_ERROR_CLOSED;
    case FR_WRITE_PROTECTED:
      return FsError::FS_ERROR_READ_ONLY;
    case FR_NOT_ENABLED:
    case FR_NO_FILESYSTEM:
      return FsError::FS_ERROR_NO_FILESYSTEM;
    case FR_TOO_MANY_OPEN_FILES:
      return FsError::FS_ERROR_TOO_MANY_OPEN;
    default:
      return FsError::FS_ERROR_IO;
  }
}

static DRESULT to_disk_result(StorageError err) {
  switch (err) {
    case StorageError::STORAGE_ERROR_NONE:
      return RES_OK;
    case StorageError::STORAGE_ERROR_NO_MEDIA:
      return RES_NOTRDY;
    case StorageError::STORAGE_ERROR_READ_ONLY:
      return RES_WRPRT;
    case StorageError::STORAGE_ERROR_INVALID:
      return RES_PARERR;
    default:
      return RES_ERROR;
  }
}

// Disk driver

DSTATUS FatFs::driver_status(BYTE pdrv) {
  const FatFs *fs = drives[pdrv];
  if (fs == nullptr || !fs->device_->has_media())
    return STA_NOINIT;
  return (fs->device_->get_geometry().flags & storage::BLOCK_FLAG_READ_ONLY) ? STA_PROTECT : 0;
}

DRESULT FatFs::driver_read(BYTE pdrv, BYTE *buf, uint32_t sector, unsigned count) {
  storage::BlockDevice *device = drives[pdrv]->device_;
  return to_disk_result(device->read(sector, 0, buf, count * device->get_geometry().block_size));
}

DRESULT FatFs::driver_write(BYTE pdrv, const BYTE *buf, uint32_t sector, unsigned count) {
  storage::BlockDevice *device = drives[pdrv]->device_;
  return to_disk_result(device->program(sector, 0, buf, count * device->get_geometry().block_size));
}

DRESULT FatFs::driver_ioctl(BYTE pdrv, BYTE cmd, void *buf) {
  storage::BlockDevice *device = drives[pdrv]->device_;
  switch (cmd) {
    case CTRL_SYNC:
      return to_disk_result(device->sync());
    case GET_SECTOR_COUNT:
      *static_cast<LBA_t *>(buf) = device->get_geometry().block_count;
      return RES_OK;
    case GET_SECTOR_SIZE:
      *static_cast<WORD *>(buf) = static_cast<WORD>(device->get_geometry().block_size);
      return RES_OK;
    case GET_BLOCK_SIZE:
      *static_cast<DWORD *>(buf) = 1;
      return RES_OK;
    case CTRL_TRIM: {
      const auto *range = static_cast<const LBA_t *>(buf);
      return to_disk_result(device->trim(range[0], range[1] - range[0] + 1));
    }
    default:
      return RES_PARERR;
  }
}

// Filesystem

void FatFs::dump_config() {
  ESP_LOGCONFIG(TAG, "FAT filesystem:\n  Drive: %u", this->pdrv_);
  Filesystem::dump_config();
  if (!this->mounted_)
    return;
  // Free space comes only from the cached count: counting it can mean reading the whole FAT
  const uint32_t clusters = this->fs_.n_fatent - 2;
  const uint64_t cluster_bytes = static_cast<uint64_t>(this->fs_.csize) * this->device_->get_geometry().block_size;
  ESP_LOGCONFIG(TAG, "  Size: %" PRIu32 " KiB", static_cast<uint32_t>((clusters * cluster_bytes) >> 10));
  if (this->fs_.free_clst <= clusters) {
    ESP_LOGCONFIG(TAG, "  Free: %" PRIu32 " KiB", static_cast<uint32_t>((this->fs_.free_clst * cluster_bytes) >> 10));
  }
}

bool FatFs::full_path_(char *out, size_t out_size, const char *path) const {
  const int len = snprintf(out, out_size, "%u:%s", this->pdrv_, path);
  return len > 0 && static_cast<size_t>(len) < out_size;
}

FsError FatFs::fs_init(uint8_t max_files, uint8_t max_dirs) {
  static const ff_diskio_impl_t DRIVER = {
      .init = &FatFs::driver_status,
      .status = &FatFs::driver_status,
      .read = &FatFs::driver_read,
      .write = &FatFs::driver_write,
      .ioctl = &FatFs::driver_ioctl,
  };
  if (ff_diskio_get_drive(&this->pdrv_) != ESP_OK) {
    ESP_LOGE(TAG, "No free FAT drive, %u in use", FF_VOLUMES);
    return FsError::FS_ERROR_TOO_MANY_OPEN;
  }
  drives[this->pdrv_] = this;
  ff_diskio_register(this->pdrv_, &DRIVER);

  RAMAllocator<FIL> file_allocator(RAMAllocator<FIL>::ALLOC_INTERNAL);
  RAMAllocator<FF_DIR> dir_allocator(RAMAllocator<FF_DIR>::ALLOC_INTERNAL);
  if (max_files != 0) {
    this->files_ = file_allocator.allocate(max_files);
    if (this->files_ == nullptr)
      return FsError::FS_ERROR_IO;
    memset(this->files_, 0, max_files * sizeof(FIL));
  }
  if (max_dirs != 0) {
    this->dirs_ = dir_allocator.allocate(max_dirs);
    if (this->dirs_ == nullptr)
      return FsError::FS_ERROR_IO;
    memset(this->dirs_, 0, max_dirs * sizeof(FF_DIR));
  }
  return FsError::FS_ERROR_NONE;
}

FsError FatFs::fs_mount() {
  const uint32_t block_size = this->device_->get_geometry().block_size;
  if (block_size < FF_MIN_SS || block_size > FF_MAX_SS) {
    ESP_LOGE(TAG, "Block size %u is not supported", static_cast<unsigned>(block_size));
    return FsError::FS_ERROR_INVALID;
  }
  char drive[4];
  this->full_path_(drive, sizeof(drive), "");
  return to_fs_error(f_mount(&this->fs_, drive, 1));
}

void FatFs::fs_unmount() {
  char drive[4];
  this->full_path_(drive, sizeof(drive), "");
  f_mount(nullptr, drive, 0);
}

FsError FatFs::fs_format() {
  char drive[4];
  this->full_path_(drive, sizeof(drive), "");
  RAMAllocator<uint8_t> allocator;
  uint8_t *work = allocator.allocate(FORMAT_BUFFER_SIZE);
  if (work == nullptr)
    return FsError::FS_ERROR_IO;
  const MKFS_PARM options = {FM_ANY, 0, 0, 0, 0};
  ESP_LOGI(TAG, "Formatting");
  const FRESULT res = f_mkfs(drive, &options, work, FORMAT_BUFFER_SIZE);
  allocator.deallocate(work, FORMAT_BUFFER_SIZE);
  return to_fs_error(res);
}

FsError FatFs::fs_open(uint8_t slot, const char *path, uint8_t flags) {
  char full[MAX_PATH];
  if (!this->full_path_(full, sizeof(full), path))
    return FsError::FS_ERROR_INVALID;
  BYTE mode = 0;
  if (flags & filesystem::OPEN_READ)
    mode |= FA_READ;
  if (flags & filesystem::OPEN_WRITE)
    mode |= FA_WRITE;
  if (!(flags & filesystem::OPEN_CREATE)) {
    mode |= FA_OPEN_EXISTING;
  } else if (flags & filesystem::OPEN_EXCLUSIVE) {
    mode |= FA_CREATE_NEW;
  } else if (flags & filesystem::OPEN_TRUNCATE) {
    mode |= FA_CREATE_ALWAYS;
  } else {
    mode |= FA_OPEN_ALWAYS;
  }
  FIL *fp = &this->files_[slot];
  FRESULT res = f_open(fp, full, mode);
  if (res != FR_OK)
    return to_fs_error(res);
  if ((flags & filesystem::OPEN_TRUNCATE) && !(flags & filesystem::OPEN_CREATE))
    res = f_truncate(fp);
  if (res == FR_OK && (flags & filesystem::OPEN_APPEND))
    res = f_lseek(fp, f_size(fp));
  if (res != FR_OK)
    f_close(fp);
  return to_fs_error(res);
}

int32_t FatFs::fs_read(uint8_t slot, void *buf, uint32_t len) {
  UINT done = 0;
  const FRESULT res = f_read(&this->files_[slot], buf, len, &done);
  return res == FR_OK ? static_cast<int32_t>(done) : static_cast<int32_t>(to_fs_error(res));
}

int32_t FatFs::fs_write(uint8_t slot, const void *buf, uint32_t len) {
  UINT done = 0;
  const FRESULT res = f_write(&this->files_[slot], buf, len, &done);
  if (res != FR_OK)
    return static_cast<int32_t>(to_fs_error(res));
  if (done == 0 && len != 0)
    return static_cast<int32_t>(FsError::FS_ERROR_NO_SPACE);
  return static_cast<int32_t>(done);
}

int32_t FatFs::fs_seek(uint8_t slot, int32_t offset, Whence whence) {
  FIL *fp = &this->files_[slot];
  int64_t pos = offset;
  if (whence == Whence::WHENCE_CURRENT) {
    pos += f_tell(fp);
  } else if (whence == Whence::WHENCE_END) {
    pos += f_size(fp);
  }
  if (pos < 0 || pos > INT32_MAX)
    return static_cast<int32_t>(FsError::FS_ERROR_INVALID);
  const FRESULT res = f_lseek(fp, static_cast<FSIZE_t>(pos));
  return res == FR_OK ? static_cast<int32_t>(f_tell(fp)) : static_cast<int32_t>(to_fs_error(res));
}

int32_t FatFs::fs_tell(uint8_t slot) { return static_cast<int32_t>(f_tell(&this->files_[slot])); }

int32_t FatFs::fs_size(uint8_t slot) { return static_cast<int32_t>(f_size(&this->files_[slot])); }

FsError FatFs::fs_sync(uint8_t slot) { return to_fs_error(f_sync(&this->files_[slot])); }

FsError FatFs::fs_truncate(uint8_t slot) { return to_fs_error(f_truncate(&this->files_[slot])); }

FsError FatFs::fs_close(uint8_t slot) { return to_fs_error(f_close(&this->files_[slot])); }

FsError FatFs::fs_open_dir(uint8_t slot, const char *path) {
  char full[MAX_PATH];
  if (!this->full_path_(full, sizeof(full), path))
    return FsError::FS_ERROR_INVALID;
  return to_fs_error(f_opendir(&this->dirs_[slot], full));
}

FsError FatFs::fs_read_dir(uint8_t slot, FileInfo &info, char *name, size_t name_size) {
  FILINFO entry;
  for (;;) {
    const FRESULT res = f_readdir(&this->dirs_[slot], &entry);
    if (res != FR_OK)
      return to_fs_error(res);
    if (entry.fname[0] == 0)
      return FsError::FS_ERROR_NOT_FOUND;
    if (strcmp(entry.fname, ".") != 0 && strcmp(entry.fname, "..") != 0)
      break;
  }
  info.size = entry.fsize;
  info.mtime = fat_time_to_time(entry.fdate, entry.ftime);
  info.is_dir = (entry.fattrib & AM_DIR) != 0;
  strncpy(name, entry.fname, name_size - 1);
  name[name_size - 1] = 0;
  return FsError::FS_ERROR_NONE;
}

FsError FatFs::fs_close_dir(uint8_t slot) { return to_fs_error(f_closedir(&this->dirs_[slot])); }

FsError FatFs::fs_remove(const char *path) {
  char full[MAX_PATH];
  if (!this->full_path_(full, sizeof(full), path))
    return FsError::FS_ERROR_INVALID;
  return to_fs_error(f_unlink(full));
}

FsError FatFs::fs_rename(const char *from, const char *to) {
  char full_from[MAX_PATH];
  char full_to[MAX_PATH];
  if (!this->full_path_(full_from, sizeof(full_from), from) || !this->full_path_(full_to, sizeof(full_to), to))
    return FsError::FS_ERROR_INVALID;
  return to_fs_error(f_rename(full_from, full_to));
}

FsError FatFs::fs_stat(const char *path, FileInfo &info) {
  // FatFs has no entry for the root directory
  if (strcmp(path, "/") == 0 || path[0] == 0) {
    info.size = 0;
    info.mtime = 0;
    info.is_dir = true;
    return FsError::FS_ERROR_NONE;
  }
  char full[MAX_PATH];
  if (!this->full_path_(full, sizeof(full), path))
    return FsError::FS_ERROR_INVALID;
  FILINFO entry;
  const FRESULT res = f_stat(full, &entry);
  if (res != FR_OK)
    return to_fs_error(res);
  info.size = entry.fsize;
  info.mtime = fat_time_to_time(entry.fdate, entry.ftime);
  info.is_dir = (entry.fattrib & AM_DIR) != 0;
  return FsError::FS_ERROR_NONE;
}

FsError FatFs::fs_mkdir(const char *path) {
  char full[MAX_PATH];
  if (!this->full_path_(full, sizeof(full), path))
    return FsError::FS_ERROR_INVALID;
  return to_fs_error(f_mkdir(full));
}

FsError FatFs::fs_space(uint64_t &total, uint64_t &free) {
  char drive[4];
  this->full_path_(drive, sizeof(drive), "");
  DWORD free_clusters;
  FATFS *fs;
  const FRESULT res = f_getfree(drive, &free_clusters, &fs);
  if (res != FR_OK)
    return to_fs_error(res);
  const uint64_t cluster_bytes = static_cast<uint64_t>(fs->csize) * this->device_->get_geometry().block_size;
  total = (fs->n_fatent - 2) * cluster_bytes;
  free = free_clusters * cluster_bytes;
  return FsError::FS_ERROR_NONE;
}

}  // namespace esphome::fatfs

// FatFs takes the time for each file it writes from get_fattime(); -Wl,--wrap=get_fattime replaces ESP-IDF's
// version, which uses localtime() and so always writes UTC.
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" DWORD __wrap_get_fattime() {
  static constexpr DWORD FAT_EPOCH = (1 << 21) | (1 << 16);  // 1980-01-01 00:00, the earliest time FAT can hold
  const esphome::ESPTime now = esphome::ESPTime::from_epoch_local(::time(nullptr));
  if (now.year < 1980 || now.year > 2107)
    return FAT_EPOCH;
  return (static_cast<DWORD>(now.year - 1980) << 25) | (static_cast<DWORD>(now.month) << 21) |
         (static_cast<DWORD>(now.day_of_month) << 16) | (static_cast<DWORD>(now.hour) << 11) |
         (static_cast<DWORD>(now.minute) << 5) | (now.second >> 1);
}
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)

#endif  // USE_ESP32
