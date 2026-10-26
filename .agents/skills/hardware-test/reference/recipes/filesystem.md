# filesystem

## The C library layer (vfs_path) on an SD card

`templates/filesystem-vfs-tab5-dut.yaml` with `templates/filesystem-vfs-test.hpp` (copy both into the scratch directory)
runs on an M5Stack Tab5 with a FAT formatted card in the slot (`sd_spi`, `fatfs` with `vfs_path: /sdcard`). At
boot it works only in `/sdcard/vfs-test`, which it removes at the end:

- write, stat, read back, append, seek and tell, `ftruncate()`, `O_EXCL` on an existing file, `ENOENT` for a
  missing one;
- `rename()` over an existing file (FAT refuses this; the VFS removes the target first, as POSIX does);
- `mkdir()`, `readdir()` with `d_type`, `rmdir()` of a directory that is not empty (refused) and of an empty one;
- two FreeRTOS tasks writing and reading their own files at once, 50 rounds each;
- then it leaves a file open, unbuffered, and calls `App.safe_reboot()`; the next boot checks the file has all its
  5000 bytes, which needs the filesystem's `on_shutdown()`.

Flash by USB, then `serial_capture.py PORT 90 OUT` covers both boots. Expected: `vfs_test: 22 passed, 0 failed`,
the restart line, then `after the restart: 1 passed, 0 failed`. A later reset runs it again, once.

The two-task check found that `sd_spi` fed the task watchdog from whatever task called it: `esp_task_wdt_reset:
task not found`, then a watchdog reset on `loopTask`, since that also skipped the main loop's own feed.
