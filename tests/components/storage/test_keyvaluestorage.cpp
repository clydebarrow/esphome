#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "esphome/components/storage/storage.h"

// Covers the KeyValueStorage contract on the host with an in-memory fake: the get/set/erase/has/
// get_size/ensure_initialized/format semantics, and that the registry's for_each_kv walker yields
// exactly the key-value devices. The native NVS backend is IDF-only, so its wiring is exercised on
// hardware; the interface behaviour it must satisfy is pinned down here.

namespace esphome::storage::testing {

// Minimal in-memory KeyValueStorage. Mirrors the documented contract: caller-provided buffers,
// NOT_FOUND for absent keys, INVALID_ARGS when a value does not fit, idempotent erase.
class FakeKV : public KeyValueStorage {
 public:
  StorageError get_info(StorageInfo *info) override {
    *info = StorageInfo{};
    return StorageError::OK;
  }

  StorageError get(uint32_t key, uint8_t *buf, size_t len, size_t *got) override {
    *got = 0;
    auto it = this->store_.find(key);
    if (it == this->store_.end())
      return StorageError::NOT_FOUND;
    if (it->second.size() > len)
      return StorageError::INVALID_ARGS;
    std::copy(it->second.begin(), it->second.end(), buf);
    *got = it->second.size();
    return StorageError::OK;
  }
  StorageError set(uint32_t key, const uint8_t *data, size_t len) override {
    this->store_[key].assign(data, data + len);
    return StorageError::OK;
  }
  StorageError erase(uint32_t key) override {
    this->store_.erase(key);  // idempotent: absent key is success
    return StorageError::OK;
  }
  bool has(uint32_t key) override { return this->store_.count(key) != 0; }
  StorageError get_size(uint32_t key, size_t *out) override {
    *out = 0;
    auto it = this->store_.find(key);
    if (it == this->store_.end())
      return StorageError::NOT_FOUND;
    *out = it->second.size();
    return StorageError::OK;
  }
  StorageError list_keys(bool (*callback)(uint32_t key, size_t size, void *ctx), void *ctx) override {
    for (auto &kv : this->store_) {
      if (!callback(kv.first, kv.second.size(), ctx))
        break;
    }
    return StorageError::OK;
  }
  StorageError ensure_initialized() override {
    this->initialized_ = true;
    return StorageError::OK;
  }
  StorageError format() override {
    this->store_.clear();
    return StorageError::OK;
  }

  bool initialized_{false};

 private:
  std::map<uint32_t, std::vector<uint8_t>> store_;
};

// ---------------------------------------------------------------------------
// Contract
// ---------------------------------------------------------------------------

TEST(KeyValueStorage, AdvertisesKeyValueType) {
  FakeKV kv;
  EXPECT_EQ(kv.get_storage_type(), StorageType::KEY_VALUE);
}

TEST(KeyValueStorage, EnsureInitializedSucceeds) {
  FakeKV kv;
  EXPECT_EQ(kv.ensure_initialized(), StorageError::OK);
  EXPECT_TRUE(kv.initialized_);
}

TEST(KeyValueStorage, AbsentKeyReportsNotFound) {
  FakeKV kv;
  const uint32_t key = 4231u;  // a decimal-style key, like the preferences key space
  uint8_t buf[8];
  size_t got = 0;
  size_t sz = 0;
  EXPECT_FALSE(kv.has(key));
  EXPECT_EQ(kv.get(key, buf, sizeof(buf), &got), StorageError::NOT_FOUND);
  EXPECT_EQ(kv.get_size(key, &sz), StorageError::NOT_FOUND);
}

TEST(KeyValueStorage, SetThenGetRoundTrips) {
  FakeKV kv;
  const uint32_t key = 4231u;
  const uint8_t value[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};

  ASSERT_EQ(kv.set(key, value, sizeof(value)), StorageError::OK);
  EXPECT_TRUE(kv.has(key));

  size_t sz = 0;
  ASSERT_EQ(kv.get_size(key, &sz), StorageError::OK);
  EXPECT_EQ(sz, sizeof(value));

  uint8_t buf[8];
  size_t got = 0;
  ASSERT_EQ(kv.get(key, buf, sizeof(buf), &got), StorageError::OK);
  EXPECT_EQ(got, sizeof(value));
  EXPECT_EQ(0, std::memcmp(buf, value, sizeof(value)));
}

TEST(KeyValueStorage, GetRejectsTooSmallBuffer) {
  FakeKV kv;
  const uint32_t key = 7u;
  const uint8_t value[] = {1, 2, 3, 4};
  ASSERT_EQ(kv.set(key, value, sizeof(value)), StorageError::OK);

  uint8_t small[2];
  size_t got = 123;  // must be reset to 0 on the error path
  EXPECT_EQ(kv.get(key, small, sizeof(small), &got), StorageError::INVALID_ARGS);
  EXPECT_EQ(got, 0u);
}

TEST(KeyValueStorage, EraseRemovesAndIsIdempotent) {
  FakeKV kv;
  const uint32_t key = 9u;
  const uint8_t value[] = {0x42};
  ASSERT_EQ(kv.set(key, value, sizeof(value)), StorageError::OK);

  EXPECT_EQ(kv.erase(key), StorageError::OK);
  EXPECT_FALSE(kv.has(key));
  // Erasing an absent key is a no-op success.
  EXPECT_EQ(kv.erase(key), StorageError::OK);
}

TEST(KeyValueStorage, SetOverwritesExistingValue) {
  FakeKV kv;
  const uint32_t key = 5u;
  const uint8_t first[] = {1, 2, 3};
  const uint8_t second[] = {9, 9};
  ASSERT_EQ(kv.set(key, first, sizeof(first)), StorageError::OK);
  ASSERT_EQ(kv.set(key, second, sizeof(second)), StorageError::OK);

  size_t sz = 0;
  ASSERT_EQ(kv.get_size(key, &sz), StorageError::OK);
  EXPECT_EQ(sz, sizeof(second));
}

TEST(KeyValueStorage, FormatWipesTheStore) {
  FakeKV kv;
  const uint8_t value[] = {1};
  ASSERT_EQ(kv.set(1u, value, sizeof(value)), StorageError::OK);
  ASSERT_EQ(kv.set(2u, value, sizeof(value)), StorageError::OK);

  ASSERT_EQ(kv.format(), StorageError::OK);
  EXPECT_FALSE(kv.has(1u));
  EXPECT_FALSE(kv.has(2u));
}

// ---------------------------------------------------------------------------
// Registry integration: for_each_kv
// ---------------------------------------------------------------------------

// A non-KV device to prove for_each_kv filters by type. The registry only asks for its type here.
class DummyPathStorage : public FilesystemStorage {
 public:
  explicit DummyPathStorage(const char *mount) { this->set_mount_path_(mount); }
  StorageError get_info(StorageInfo *info) override {
    *info = StorageInfo{};
    return StorageError::OK;
  }
  StorageError stat(const char *, FileStat *) override { return StorageError::NOT_FOUND; }
  StorageError list_dir(const char *, bool (*)(const FileStat *, void *), void *) override {
    return StorageError::NOT_SUPPORTED;
  }
  StorageError mkdir(const char *) override { return StorageError::NOT_SUPPORTED; }
  StorageError rmdir(const char *) override { return StorageError::NOT_SUPPORTED; }
  StorageError remove(const char *) override { return StorageError::NOT_SUPPORTED; }
  StorageError rename(const char *, const char *) override { return StorageError::NOT_SUPPORTED; }
  StorageError mount() override { return StorageError::OK; }
  StorageError unmount() override { return StorageError::OK; }
  StorageError format() override { return StorageError::NOT_SUPPORTED; }
  StorageError sync() override { return StorageError::OK; }
  StorageError open(const char *, FileHandle *&, OpenMode) override { return StorageError::NOT_SUPPORTED; }
  StorageError close(FileHandle *) override { return StorageError::OK; }
  StorageError read(FileHandle *, uint8_t *, size_t, size_t *) override { return StorageError::NOT_SUPPORTED; }
  StorageError write(FileHandle *, const uint8_t *, size_t, size_t *) override { return StorageError::NOT_SUPPORTED; }
  StorageError seek(FileHandle *, int64_t, SeekMode) override { return StorageError::NOT_SUPPORTED; }
  StorageError tell(FileHandle *, uint64_t *) override { return StorageError::NOT_SUPPORTED; }
};

class KvRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    this->registry_.set_device_count(3);
    ASSERT_EQ(this->registry_.register_storage(&this->kv_a_), StorageError::OK);
    ASSERT_EQ(this->registry_.register_storage(&this->path_), StorageError::OK);
    ASSERT_EQ(this->registry_.register_storage(&this->kv_b_), StorageError::OK);
  }

  StorageRegistry registry_;
  FakeKV kv_a_;
  DummyPathStorage path_{"/sd"};
  FakeKV kv_b_;
};

TEST_F(KvRegistryTest, ForEachKvVisitsOnlyKeyValueDevices) {
  std::vector<KeyValueStorage *> seen;
  this->registry_.for_each_kv(
      [](KeyValueStorage *s, void *ctx) { static_cast<std::vector<KeyValueStorage *> *>(ctx)->push_back(s); }, &seen);

  ASSERT_EQ(seen.size(), 2u);
  // Both KV fakes are present; the path device is not.
  EXPECT_NE(std::find(seen.begin(), seen.end(), &this->kv_a_), seen.end());
  EXPECT_NE(std::find(seen.begin(), seen.end(), &this->kv_b_), seen.end());
}

}  // namespace esphome::storage::testing
