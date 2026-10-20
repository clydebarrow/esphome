#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "esphome/components/filesystem/filesystem.h"
#include "esphome/components/ram_disk/ram_disk.h"

namespace esphome::filesystem::testing {

// A flat, in-memory driver: enough to exercise the handle pools, mounting and media changes in the base class.
class FakeFs : public Filesystem {
 public:
  using Filesystem::Filesystem;

  bool has_filesystem{true};
  int mounts{0};
  int formats{0};
  int closes{0};
  std::map<std::string, std::vector<uint8_t>> files;

 protected:
  struct Open {
    std::string path;
    uint32_t pos{0};
  };

  FsError fs_init(uint8_t max_files, uint8_t max_dirs) override {
    this->open_.resize(max_files);
    this->dir_pos_.resize(max_dirs);
    return FsError::FS_ERROR_NONE;
  }
  FsError fs_mount() override {
    this->mounts++;
    return this->has_filesystem ? FsError::FS_ERROR_NONE : FsError::FS_ERROR_NO_FILESYSTEM;
  }
  void fs_unmount() override {}
  FsError fs_format() override {
    this->formats++;
    this->files.clear();
    this->has_filesystem = true;
    return FsError::FS_ERROR_NONE;
  }
  FsError fs_open(uint8_t slot, const char *path, uint8_t flags) override {
    auto it = this->files.find(path);
    if (it == this->files.end()) {
      if (!(flags & OPEN_CREATE))
        return FsError::FS_ERROR_NOT_FOUND;
      it = this->files.emplace(path, std::vector<uint8_t>{}).first;
    } else if ((flags & OPEN_CREATE) && (flags & OPEN_EXCLUSIVE)) {
      return FsError::FS_ERROR_EXISTS;
    }
    if (flags & OPEN_TRUNCATE)
      it->second.clear();
    this->open_[slot] = {path, (flags & OPEN_APPEND) ? static_cast<uint32_t>(it->second.size()) : 0};
    return FsError::FS_ERROR_NONE;
  }
  int32_t fs_read(uint8_t slot, void *buf, uint32_t len) override {
    auto &f = this->open_[slot];
    auto &data = this->files[f.path];
    const uint32_t n = std::min<uint32_t>(len, data.size() - std::min<size_t>(f.pos, data.size()));
    memcpy(buf, data.data() + f.pos, n);
    f.pos += n;
    return static_cast<int32_t>(n);
  }
  int32_t fs_write(uint8_t slot, const void *buf, uint32_t len) override {
    auto &f = this->open_[slot];
    auto &data = this->files[f.path];
    if (data.size() < f.pos + len)
      data.resize(f.pos + len);
    memcpy(data.data() + f.pos, buf, len);
    f.pos += len;
    return static_cast<int32_t>(len);
  }
  int32_t fs_seek(uint8_t slot, int32_t offset, Whence whence) override {
    auto &f = this->open_[slot];
    int32_t base = 0;
    if (whence == Whence::WHENCE_CURRENT)
      base = static_cast<int32_t>(f.pos);
    if (whence == Whence::WHENCE_END)
      base = static_cast<int32_t>(this->files[f.path].size());
    f.pos = static_cast<uint32_t>(base + offset);
    return static_cast<int32_t>(f.pos);
  }
  int32_t fs_tell(uint8_t slot) override { return static_cast<int32_t>(this->open_[slot].pos); }
  int32_t fs_size(uint8_t slot) override { return static_cast<int32_t>(this->files[this->open_[slot].path].size()); }
  FsError fs_sync(uint8_t slot) override { return FsError::FS_ERROR_NONE; }
  FsError fs_truncate(uint8_t slot) override {
    this->files[this->open_[slot].path].resize(this->open_[slot].pos);
    return FsError::FS_ERROR_NONE;
  }
  FsError fs_close(uint8_t slot) override {
    this->closes++;
    return FsError::FS_ERROR_NONE;
  }
  FsError fs_open_dir(uint8_t slot, const char *path) override {
    this->dir_pos_[slot] = 0;
    return FsError::FS_ERROR_NONE;
  }
  FsError fs_read_dir(uint8_t slot, FileInfo &info, char *name, size_t name_size) override {
    if (this->dir_pos_[slot] >= this->files.size())
      return FsError::FS_ERROR_NOT_FOUND;
    auto it = std::next(this->files.begin(), this->dir_pos_[slot]++);
    info.size = it->second.size();
    info.is_dir = false;
    strncpy(name, it->first.c_str(), name_size - 1);
    name[name_size - 1] = 0;
    return FsError::FS_ERROR_NONE;
  }
  FsError fs_close_dir(uint8_t slot) override { return FsError::FS_ERROR_NONE; }
  FsError fs_remove(const char *path) override {
    return this->files.erase(path) ? FsError::FS_ERROR_NONE : FsError::FS_ERROR_NOT_FOUND;
  }
  FsError fs_rename(const char *from, const char *to) override { return FsError::FS_ERROR_INVALID; }
  FsError fs_stat(const char *path, FileInfo &info) override {
    auto it = this->files.find(path);
    if (it == this->files.end())
      return FsError::FS_ERROR_NOT_FOUND;
    info.size = it->second.size();
    info.is_dir = false;
    return FsError::FS_ERROR_NONE;
  }
  FsError fs_mkdir(const char *path) override { return FsError::FS_ERROR_NONE; }
  FsError fs_space(uint64_t &total, uint64_t &free) override {
    total = free = 0;
    return FsError::FS_ERROR_NONE;
  }

  std::vector<Open> open_;
  std::vector<size_t> dir_pos_;
};

// A RAM disk whose media can be taken out and put back.
class SwapDisk : public ram_disk::RamDisk {
 public:
  using RamDisk::RamDisk;
  void eject() {
    this->geometry_.block_count = 0;
    this->media_changed_();
  }
  void insert() {
    this->geometry_.block_count = this->block_count_;
    this->media_changed_();
  }
};

class FilesystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->disk_.setup();
    this->fs_.set_pool_sizes(2, 1);
  }

  SwapDisk disk_{512, 16};
  FakeFs fs_{&disk_};
};

TEST_F(FilesystemTest, MountsInSetupWhenMediaIsThere) {
  this->fs_.setup();
  EXPECT_TRUE(this->fs_.is_mounted());
  EXPECT_EQ(this->fs_.mounts, 1);
}

TEST_F(FilesystemTest, MountsOnFirstUseWhenMediaArrivesLater) {
  this->disk_.eject();
  this->fs_.setup();
  EXPECT_FALSE(this->fs_.is_mounted());
  EXPECT_FALSE(this->fs_.open("/a", OPEN_READ));
  EXPECT_EQ(this->fs_.last_error(), FsError::FS_ERROR_NO_MEDIA);
  this->disk_.insert();
  File f = this->fs_.open("/a", OPEN_WRITE | OPEN_CREATE);
  EXPECT_TRUE(f);
  EXPECT_TRUE(this->fs_.is_mounted());
}

TEST_F(FilesystemTest, WriteThenReadBack) {
  this->fs_.setup();
  {
    File f = this->fs_.open("/hello.txt", OPEN_WRITE | OPEN_CREATE);
    ASSERT_TRUE(f);
    EXPECT_EQ(f.write("hello", 5), 5);
  }
  File f = this->fs_.open("/hello.txt", OPEN_READ);
  ASSERT_TRUE(f);
  char buf[8]{};
  EXPECT_EQ(f.size(), 5);
  EXPECT_EQ(f.read(buf, sizeof(buf)), 5);
  EXPECT_STREQ(buf, "hello");
  EXPECT_EQ(f.read(buf, sizeof(buf)), 0);
}

TEST_F(FilesystemTest, OpenNeedsReadOrWrite) {
  this->fs_.setup();
  EXPECT_FALSE(this->fs_.open("/a", OPEN_CREATE));
  EXPECT_EQ(this->fs_.last_error(), FsError::FS_ERROR_INVALID);
}

TEST_F(FilesystemTest, PoolRunsOutAndClosingFreesASlot) {
  this->fs_.setup();
  File a = this->fs_.open("/a", OPEN_WRITE | OPEN_CREATE);
  File b = this->fs_.open("/b", OPEN_WRITE | OPEN_CREATE);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_FALSE(this->fs_.open("/c", OPEN_WRITE | OPEN_CREATE));
  EXPECT_EQ(this->fs_.last_error(), FsError::FS_ERROR_TOO_MANY_OPEN);
  a.close();
  EXPECT_FALSE(a);
  EXPECT_TRUE(this->fs_.open("/c", OPEN_WRITE | OPEN_CREATE));
}

TEST_F(FilesystemTest, MovingAHandleKeepsOneOwner) {
  this->fs_.setup();
  File a = this->fs_.open("/a", OPEN_WRITE | OPEN_CREATE);
  File b = std::move(a);
  EXPECT_FALSE(a);  // NOLINT(bugprone-use-after-move)
  EXPECT_TRUE(b);
  b = File();
  EXPECT_EQ(this->fs_.closes, 1);
  // Both slots are free again
  File c = this->fs_.open("/c", OPEN_WRITE | OPEN_CREATE);
  File d = this->fs_.open("/d", OPEN_WRITE | OPEN_CREATE);
  EXPECT_TRUE(c);
  EXPECT_TRUE(d);
}

TEST_F(FilesystemTest, MediaChangeClosesOldHandles) {
  this->fs_.setup();
  File a = this->fs_.open("/a", OPEN_WRITE | OPEN_CREATE);
  File b = this->fs_.open("/b", OPEN_WRITE | OPEN_CREATE);
  ASSERT_TRUE(a);
  this->disk_.eject();
  this->disk_.insert();
  // The next call notices the change, closes everything and mounts the new media
  File c = this->fs_.open("/c", OPEN_WRITE | OPEN_CREATE);
  ASSERT_TRUE(c);
  EXPECT_EQ(this->fs_.mounts, 2);
  EXPECT_EQ(a.write("x", 1), static_cast<int32_t>(FsError::FS_ERROR_CLOSED));
  // A stale handle closing must not free the slot now used by c
  EXPECT_EQ(a.close(), FsError::FS_ERROR_CLOSED);
  File d = this->fs_.open("/d", OPEN_WRITE | OPEN_CREATE);
  EXPECT_TRUE(d);
  EXPECT_FALSE(this->fs_.open("/e", OPEN_WRITE | OPEN_CREATE));
  EXPECT_EQ(c.write("y", 1), 1);
}

TEST_F(FilesystemTest, UnmountClosesOpenFiles) {
  this->fs_.setup();
  File a = this->fs_.open("/a", OPEN_WRITE | OPEN_CREATE);
  this->fs_.unmount();
  EXPECT_FALSE(this->fs_.is_mounted());
  EXPECT_EQ(this->fs_.closes, 1);
  EXPECT_EQ(a.sync(), FsError::FS_ERROR_CLOSED);
}

TEST_F(FilesystemTest, FormatsOnlyWhenAsked) {
  this->fs_.has_filesystem = false;
  this->fs_.setup();
  EXPECT_FALSE(this->fs_.is_mounted());
  EXPECT_EQ(this->fs_.formats, 0);

  FakeFs formatting{&this->disk_};
  formatting.has_filesystem = false;
  formatting.set_format_if_mount_failed(true);
  formatting.setup();
  EXPECT_TRUE(formatting.is_mounted());
  EXPECT_EQ(formatting.formats, 1);
}

TEST_F(FilesystemTest, ListsDirectory) {
  this->fs_.setup();
  this->fs_.open("/a", OPEN_WRITE | OPEN_CREATE).write("12", 2);
  this->fs_.open("/b", OPEN_WRITE | OPEN_CREATE);
  Dir dir = this->fs_.open_dir("/");
  ASSERT_TRUE(dir);
  FileInfo info;
  char name[4];
  ASSERT_EQ(dir.read_next(info, name, sizeof(name)), FsError::FS_ERROR_NONE);
  EXPECT_STREQ(name, "/a");
  EXPECT_EQ(info.size, 2u);
  ASSERT_EQ(dir.read_next(info, name, sizeof(name)), FsError::FS_ERROR_NONE);
  EXPECT_STREQ(name, "/b");
  EXPECT_EQ(dir.read_next(info, name, sizeof(name)), FsError::FS_ERROR_NOT_FOUND);
  EXPECT_FALSE(this->fs_.open_dir("/"));
  EXPECT_EQ(this->fs_.last_error(), FsError::FS_ERROR_TOO_MANY_OPEN);
}

TEST_F(FilesystemTest, PathCallsReportErrors) {
  this->fs_.setup();
  FileInfo info;
  EXPECT_EQ(this->fs_.stat("/missing", info), FsError::FS_ERROR_NOT_FOUND);
  EXPECT_EQ(this->fs_.remove("/missing"), FsError::FS_ERROR_NOT_FOUND);
  EXPECT_EQ(this->fs_.stat(nullptr, info), FsError::FS_ERROR_INVALID);
  this->disk_.eject();
  EXPECT_EQ(this->fs_.mkdir("/d"), FsError::FS_ERROR_NO_MEDIA);
}

}  // namespace esphome::filesystem::testing
