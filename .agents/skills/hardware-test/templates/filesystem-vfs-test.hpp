#pragma once

// Checks for the filesystem's C library layer (vfs_path), used by filesystem-vfs-tab5-dut.yaml. Everything happens
// in TEST_DIR on the card; nothing else on it is touched.

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vfs_test {

static constexpr const char *TAG = "vfs_test";
static constexpr const char *TEST_DIR = "/sdcard/vfs-test";
// Left open across a restart: it only has its data afterwards if the filesystem closed it on shutdown
static constexpr const char *REBOOT_FILE = "/sdcard/vfs-test/reboot.txt";
static constexpr size_t REBOOT_FILE_SIZE = 5000;
static constexpr size_t PATTERN_SIZE = 3000;

static unsigned passed, failed;

static void check(bool ok, const char *what) {
  if (ok) {
    passed++;
    ESP_LOGI(TAG, "PASS %s", what);
  } else {
    failed++;
    ESP_LOGE(TAG, "FAIL %s (errno %d)", what, errno);
  }
}

static char pattern_byte(size_t i, unsigned seed) { return static_cast<char>('A' + (i * 7 + seed) % 26); }

static bool write_pattern(const char *path, size_t size, unsigned seed, const char *mode = "w") {
  FILE *f = fopen(path, mode);
  if (f == nullptr)
    return false;
  bool ok = true;
  for (size_t i = 0; i < size && ok; i++)
    ok = fputc(pattern_byte(i, seed), f) != EOF;
  return fclose(f) == 0 && ok;
}

static bool check_pattern(const char *path, size_t size, unsigned seed) {
  FILE *f = fopen(path, "r");
  if (f == nullptr)
    return false;
  bool ok = true;
  for (size_t i = 0; i < size && ok; i++)
    ok = fgetc(f) == pattern_byte(i, seed);
  ok = ok && fgetc(f) == EOF;
  fclose(f);
  return ok;
}

static long file_size(const char *path) {
  struct stat st{};
  return stat(path, &st) == 0 ? static_cast<long>(st.st_size) : -1;
}

static void remove_test_files() {
  if (DIR *d = opendir(TEST_DIR)) {
    char path[96];
    while (struct dirent *e = readdir(d)) {
      snprintf(path, sizeof(path), "%s/%s", TEST_DIR, e->d_name);
      if (strcmp(path, REBOOT_FILE) != 0)
        unlink(path);
    }
    closedir(d);
  }
}

static void test_files() {
  check(write_pattern("/sdcard/vfs-test/a.txt", PATTERN_SIZE, 1), "write a file");
  check(file_size("/sdcard/vfs-test/a.txt") == PATTERN_SIZE, "stat gives its size");
  check(check_pattern("/sdcard/vfs-test/a.txt", PATTERN_SIZE, 1), "read it back");

  FILE *f = fopen("/sdcard/vfs-test/a.txt", "a");
  check(f != nullptr && fputs("tail", f) >= 0 && fclose(f) == 0, "append");
  check(file_size("/sdcard/vfs-test/a.txt") == PATTERN_SIZE + 4, "appended size");

  f = fopen("/sdcard/vfs-test/a.txt", "r");
  bool ok = f != nullptr && fseek(f, 100, SEEK_SET) == 0 && fgetc(f) == pattern_byte(100, 1) && ftell(f) == 101 &&
            fseek(f, -4, SEEK_END) == 0 && fgetc(f) == 't';
  if (f != nullptr)
    fclose(f);
  check(ok, "seek and tell");

  int fd = open("/sdcard/vfs-test/a.txt", O_RDWR);
  check(fd >= 0 && ftruncate(fd, 100) == 0 && close(fd) == 0, "truncate");
  check(file_size("/sdcard/vfs-test/a.txt") == 100 && check_pattern("/sdcard/vfs-test/a.txt", 100, 1),
        "truncated contents");

  errno = 0;
  fd = open("/sdcard/vfs-test/a.txt", O_WRONLY | O_CREAT | O_EXCL, 0666);
  check(fd < 0 && errno == EEXIST, "O_EXCL refuses an existing file");
  if (fd >= 0)
    close(fd);

  errno = 0;
  check(fopen("/sdcard/vfs-test/missing.txt", "r") == nullptr && errno == ENOENT, "a missing file gives ENOENT");
}

static void test_rename() {
  check(write_pattern("/sdcard/vfs-test/b.txt", 200, 2), "write a second file");
  // FAT refuses to rename onto an existing file; POSIX replaces it
  check(rename("/sdcard/vfs-test/a.txt", "/sdcard/vfs-test/b.txt") == 0, "rename over an existing file");
  check(file_size("/sdcard/vfs-test/a.txt") < 0, "the old name is gone");
  check(check_pattern("/sdcard/vfs-test/b.txt", 100, 1), "the new name has the renamed contents");
}

static void test_directories() {
  check(mkdir("/sdcard/vfs-test/sub", 0777) == 0, "mkdir");
  check(write_pattern("/sdcard/vfs-test/sub/c.txt", 10, 3), "write in a subdirectory");
  unsigned found = 0;
  bool types_ok = true;
  if (DIR *d = opendir(TEST_DIR)) {
    while (struct dirent *e = readdir(d)) {
      if (strcmp(e->d_name, "b.txt") == 0) {
        found++;
        types_ok = types_ok && e->d_type == DT_REG;
      } else if (strcmp(e->d_name, "sub") == 0) {
        found++;
        types_ok = types_ok && e->d_type == DT_DIR;
      }
    }
    closedir(d);
  }
  check(found == 2 && types_ok, "readdir lists the file and the directory with their types");
  errno = 0;
  check(rmdir("/sdcard/vfs-test/sub") != 0, "rmdir refuses a directory that is not empty");
  check(unlink("/sdcard/vfs-test/sub/c.txt") == 0 && rmdir("/sdcard/vfs-test/sub") == 0, "unlink and rmdir");
  check(file_size("/sdcard/vfs-test/sub") < 0, "the directory is gone");
}

struct Writer {
  const char *path;
  unsigned seed;
  std::atomic<bool> done{false};
  bool ok{true};
};

static void writer_task(void *arg) {
  auto *w = static_cast<Writer *>(arg);
  for (unsigned round = 0; round != 50 && w->ok; round++)
    w->ok = write_pattern(w->path, 700, w->seed + round) && check_pattern(w->path, 700, w->seed + round);
  w->done = true;
  vTaskDelete(nullptr);
}

static void test_tasks() {
  // Two tasks use the C library at the same time; the VFS lock keeps their calls apart
  static Writer first{"/sdcard/vfs-test/t1.txt", 10};
  static Writer second{"/sdcard/vfs-test/t2.txt", 20};
  xTaskCreate(writer_task, "vfs_t1", 4096, &first, 1, nullptr);
  xTaskCreate(writer_task, "vfs_t2", 4096, &second, 1, nullptr);
  const uint32_t start = esphome::millis();
  while (!(first.done && second.done) && esphome::millis() - start < 60000) {
    // this runs on the main loop
    esphome::App.feed_wdt();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  check(first.done && second.done, "two tasks finish 50 rounds each");
  check(first.ok && second.ok, "two tasks writing and reading at once");
}

// Run after a restart that test_shutdown() caused: the file it left open must have all its data
static bool check_after_restart() {
  const long size = file_size(REBOOT_FILE);
  if (size < 0)
    return false;
  check(size == REBOOT_FILE_SIZE && check_pattern(REBOOT_FILE, REBOOT_FILE_SIZE, 4),
        "a file left open across a restart has its data");
  unlink(REBOOT_FILE);
  return true;
}

static void test_shutdown() {
  FILE *f = fopen(REBOOT_FILE, "w");
  if (f == nullptr) {
    check(false, "open the file to leave open");
    return;
  }
  // no stdio buffer, so every byte reaches the filesystem before the restart
  setvbuf(f, nullptr, _IONBF, 0);
  for (size_t i = 0; i != REBOOT_FILE_SIZE; i++)
    fputc(pattern_byte(i, 4), f);
  ESP_LOGI(TAG, "restarting with %s open; the next boot checks it", REBOOT_FILE);
  esphome::App.safe_reboot();
}

inline void run() {
  passed = failed = 0;
  mkdir(TEST_DIR, 0777);
  if (check_after_restart()) {
    remove_test_files();
    rmdir(TEST_DIR);
    ESP_LOGI(TAG, "after the restart: %u passed, %u failed", passed, failed);
    return;
  }
  remove_test_files();
  test_files();
  test_rename();
  test_directories();
  test_tasks();
  remove_test_files();
  ESP_LOGI(TAG, "%u passed, %u failed", passed, failed);
  test_shutdown();
}

}  // namespace vfs_test
