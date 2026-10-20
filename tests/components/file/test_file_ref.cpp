#include <gtest/gtest.h>

#include <map>
#include <string>

#include "esphome/components/file/file_ref.h"
#include "esphome/components/storage/block_device.h"

namespace esphome::file::testing {

using filesystem::FileInfo;
using filesystem::FsError;
using filesystem::Whence;

class FakeDisk : public storage::BlockDevice {
 public:
  FakeDisk() {
    this->geometry_.block_size = 512;
    this->geometry_.block_count = 16;
  }

 protected:
  storage::StorageError device_read(uint32_t, uint32_t, void *, uint32_t) override {
    return storage::StorageError::STORAGE_ERROR_NONE;
  }
  storage::StorageError device_program(uint32_t, uint32_t, const void *, uint32_t) override {
    return storage::StorageError::STORAGE_ERROR_NONE;
  }
};

// Holds only directory entries; enough for the queries FileRef makes.
class FakeFs : public filesystem::Filesystem {
 public:
  using Filesystem::Filesystem;

  std::map<std::string, FileInfo> entries;
  std::string last_stat;
  std::string last_open;
  std::string last_remove;

 protected:
  FsError fs_init(uint8_t, uint8_t) override { return FsError::FS_ERROR_NONE; }
  FsError fs_mount() override { return FsError::FS_ERROR_NONE; }
  void fs_unmount() override {}
  FsError fs_format() override { return FsError::FS_ERROR_NONE; }
  FsError fs_open(uint8_t, const char *path, uint8_t) override {
    this->last_open = path;
    return FsError::FS_ERROR_NONE;
  }
  int32_t fs_read(uint8_t, void *, uint32_t) override { return 0; }
  int32_t fs_write(uint8_t, const void *, uint32_t len) override { return static_cast<int32_t>(len); }
  int32_t fs_seek(uint8_t, int32_t, Whence) override { return 0; }
  int32_t fs_tell(uint8_t) override { return 0; }
  int32_t fs_size(uint8_t) override { return 0; }
  FsError fs_sync(uint8_t) override { return FsError::FS_ERROR_NONE; }
  FsError fs_truncate(uint8_t) override { return FsError::FS_ERROR_NONE; }
  FsError fs_close(uint8_t) override { return FsError::FS_ERROR_NONE; }
  FsError fs_open_dir(uint8_t, const char *) override { return FsError::FS_ERROR_INVALID; }
  FsError fs_read_dir(uint8_t, FileInfo &, char *, size_t) override { return FsError::FS_ERROR_NOT_FOUND; }
  FsError fs_close_dir(uint8_t) override { return FsError::FS_ERROR_NONE; }
  FsError fs_remove(const char *path) override {
    this->last_remove = path;
    return this->entries.erase(path) ? FsError::FS_ERROR_NONE : FsError::FS_ERROR_NOT_FOUND;
  }
  FsError fs_rename(const char *, const char *) override { return FsError::FS_ERROR_INVALID; }
  FsError fs_stat(const char *path, FileInfo &info) override {
    this->last_stat = path;
    auto it = this->entries.find(path);
    if (it == this->entries.end())
      return FsError::FS_ERROR_NOT_FOUND;
    info = it->second;
    return FsError::FS_ERROR_NONE;
  }
  FsError fs_mkdir(const char *) override { return FsError::FS_ERROR_NONE; }
  FsError fs_space(uint64_t &total, uint64_t &free) override {
    total = free = 0;
    return FsError::FS_ERROR_NONE;
  }
};

class FileRefTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->fs_.set_pool_sizes(2, 0);
    this->fs_.setup();
    this->fs_.entries["/data.bin"] = {.size = 1234, .mtime = 1760000000, .is_dir = false};
    this->fs_.entries["/logs"] = {.size = 0, .mtime = 1750000000, .is_dir = true};
  }

  FakeDisk disk_;
  FakeFs fs_{&disk_};
};

TEST_F(FileRefTest, ReportsAFile) {
  FileRef ref(&this->fs_, 32, "/data.bin");
  EXPECT_STREQ(ref.get_path(), "/data.bin");
  EXPECT_TRUE(ref.exists());
  EXPECT_TRUE(ref.is_file());
  EXPECT_FALSE(ref.is_directory());
  EXPECT_EQ(ref.get_size(), 1234u);
  const ESPTime modified = ref.get_modified();
  ASSERT_TRUE(modified.is_valid());
  EXPECT_EQ(modified.timestamp, 1760000000);
  // No time zone in the test build, so local time is UTC: 2025-10-09 08:53:20
  EXPECT_EQ(modified.year, 2025);
  EXPECT_EQ(modified.month, 10);
  EXPECT_EQ(modified.day_of_month, 9);
  EXPECT_EQ(modified.hour, 8);
  EXPECT_EQ(modified.minute, 53);
  EXPECT_EQ(modified.second, 20);
}

TEST_F(FileRefTest, ReportsADirectory) {
  FileRef ref(&this->fs_, 32, "/logs");
  EXPECT_TRUE(ref.exists());
  EXPECT_FALSE(ref.is_file());
  EXPECT_TRUE(ref.is_directory());
  EXPECT_EQ(ref.get_modified().timestamp, 1750000000);
}

TEST_F(FileRefTest, MissingFileReportsNothing) {
  FileRef ref(&this->fs_, 32, "/missing");
  EXPECT_FALSE(ref.exists());
  EXPECT_FALSE(ref.is_file());
  EXPECT_FALSE(ref.is_directory());
  EXPECT_EQ(ref.get_size(), 0u);
  EXPECT_FALSE(ref.get_modified().is_valid());
  EXPECT_EQ(this->fs_.last_error(), FsError::FS_ERROR_NOT_FOUND);
}

TEST_F(FileRefTest, SeesChangesMadeElsewhere) {
  FileRef ref(&this->fs_, 32, "/new.txt");
  EXPECT_FALSE(ref.exists());
  this->fs_.entries["/new.txt"] = {.size = 5, .mtime = 0, .is_dir = false};
  EXPECT_TRUE(ref.exists());
  EXPECT_EQ(ref.get_size(), 5u);
  // A filesystem that records no time reports 0
  EXPECT_FALSE(ref.get_modified().is_valid());
}

TEST_F(FileRefTest, SetPathChangesTarget) {
  FileRef ref(&this->fs_, 32, "/missing");
  EXPECT_TRUE(ref.set_path(std::string("/data.bin")));
  EXPECT_STREQ(ref.get_path(), "/data.bin");
  EXPECT_EQ(ref.get_size(), 1234u);
}

TEST_F(FileRefTest, SetPathRejectsBadPaths) {
  FileRef ref(&this->fs_, 9, "/data.bin");
  EXPECT_FALSE(ref.set_path("relative"));
  EXPECT_FALSE(ref.set_path(""));
  EXPECT_FALSE(ref.set_path(nullptr));
  EXPECT_FALSE(ref.set_path("/ten-chars"));
  EXPECT_STREQ(ref.get_path(), "/data.bin");
  EXPECT_TRUE(ref.set_path("/nine-chr"));
  EXPECT_STREQ(ref.get_path(), "/nine-chr");
}

TEST_F(FileRefTest, UnsetPathNeverReachesTheDriver) {
  FileRef ref(&this->fs_, 32, nullptr);
  EXPECT_STREQ(ref.get_path(), "");
  EXPECT_FALSE(ref.exists());
  EXPECT_EQ(this->fs_.last_error(), FsError::FS_ERROR_INVALID);
  EXPECT_FALSE(ref.open(filesystem::OPEN_READ));
  EXPECT_EQ(ref.remove(), FsError::FS_ERROR_INVALID);
  EXPECT_TRUE(this->fs_.last_stat.empty());
  EXPECT_TRUE(this->fs_.last_open.empty());
  EXPECT_TRUE(this->fs_.last_remove.empty());
}

TEST_F(FileRefTest, OpenAndRemoveUseThePath) {
  FileRef ref(&this->fs_, 32, "/data.bin");
  filesystem::File f = ref.open(filesystem::OPEN_READ);
  EXPECT_TRUE(f);
  EXPECT_EQ(this->fs_.last_open, "/data.bin");
  f.close();
  EXPECT_EQ(ref.remove(), FsError::FS_ERROR_NONE);
  EXPECT_FALSE(ref.exists());
}

}  // namespace esphome::file::testing
