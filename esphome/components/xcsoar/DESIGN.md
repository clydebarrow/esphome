# XCSoar on ESP32-P4 with ESPHome

This document describes how the XCSoar glide computer can run on an ESP32-P4 as an ESPHome application,
what phase 1 (this component) does, and what is still to do.

## Goal

XCSoar runs in its own FreeRTOS task. Everything it draws reaches the screen through an ESPHome display
driver (`mipi_dsi` on the P4), so the board configuration, backlight, touch screen, Wi-Fi, OTA and logging stay
in ordinary ESPHome YAML.

## Code layout

| Where | What |
|---|---|
| XCSoar `esp32/xcsoar/` | ESP-IDF component. `include/XCSoarEsp32.hpp` is the whole interface between the two projects. |
| XCSoar `esp32/Sources.cmake` | The XCSoar source list and defines, shared with the host test. |
| XCSoar `esp32/host-test/` | Builds the same sources for Linux and writes a frame to a PPM file. No ESP toolchain needed. |
| XCSoar `src/ui/canvas/fb/TopCanvas.cpp` | New `USE_EXTERNAL_FB` mode: a memory canvas whose `Flip()` calls a function instead of writing to `/dev/fb0`. |
| XCSoar `src/ui/canvas/esp32/Files.cpp` | Font search paths: `/sdcard/XCSoar/fonts`, then `/rom/fonts`. |
| ESPHome `components/xcsoar/` | Adds the IDF component, starts the task, hands frames to the display. |

The ESPHome component points `add_idf_component(path=...)` at a local XCSoar checkout (`xcsoar_path`).
XCSoar is GPL-2.0-or-later, so firmware built with it is GPL as well.

## Phase 1: rendering (done)

```yaml
display:
  - platform: mipi_dsi
    id: lcd
    model: JC1060P470

xcsoar:
  display_id: lcd
  xcsoar_path: ../XCSoar
  fonts:
    - fonts/DejaVuSansCondensed.ttf
    - fonts/DejaVuSansCondensed-Bold.ttf
```

```
 XCSoar task (PSRAM stack)                 ESPHome main loop
 ─────────────────────────                 ─────────────────
 draw into memory canvas (BGRA)
 TopCanvas::Flip()
   └ CopyFromBGRA → RGB565 frame_
   └ frame_pending_ = true
   └ enable_loop_soon_any_context() ───────► loop()
   └ wait on semaphore                         draw_pixels_at(frame_, 565)
                                   ◄───────    give semaphore
 next frame                                    disable_loop()
```

- **Threads.** Display drivers are not thread safe, so only the main loop calls the display. The XCSoar task
  blocks until the frame has been copied, so the frame buffer is never written while it is being read.
- **Fast path.** `draw_pixels_at` with RGB565 and the panel's byte order goes straight to
  `esp_lcd_panel_draw_bitmap` (DMA2D). A software rotation on the display falls back to one pixel at a time,
  so XCSoar should render at the panel's native orientation (XCSoar has its own display rotation).
- **The display's own drawing.** `mipi_dsi` turns on its test card when it has no writer. The component calls
  `stop_poller()` on the display, so `update()` never runs and the display never allocates its own buffer.
- **Fonts.** XCSoar ships no fonts. The `fonts` option embeds TTF files in the firmware, and
  `XCSoarEsp32::MountRomFs()` registers a small read-only ESP-IDF VFS driver at `/rom`, so FreeType opens them
  with ordinary `fopen()`. That driver will also serve other embedded data later.
- **Memory.** `malloc()` normally stays in internal RAM under `CONFIG_SPIRAM_USE_CAPS_ALLOC`. XCSoar allocates
  with plain `new`, so `setup()` calls `heap_caps_malloc_extmem_enable(16384)`: blocks of 16 KB or more go to
  PSRAM. At 1024×600, the BGRA canvas (2.4 MB), the RGB565 frame (1.2 MB) and the panel's DMA frame buffer
  (1.2 MB) use about 5 MB of the 32 MB PSRAM.
- **Exceptions.** XCSoar reports errors with exceptions. The component sets `CONFIG_COMPILER_CXX_EXCEPTIONS`,
  and the IDF component compiles with `-fexceptions`, which overrides ESPHome's global `-fno-exceptions` for
  XCSoar's files only. ESPHome's `--wrap` stubs for `std::__throw_*` stay: a length or range error in the
  standard library still aborts, which is acceptable because it means a bug.
- **Test scene.** Phase 1 draws a map-like scene with a rotating aircraft symbol, using XCSoar's canvas,
  pens, brushes and FreeType fonts. It proves the build, the canvas, fonts and the frame handoff.

## Phase 2: event loop, windows and touch

- **Event loop.** Port `src/event` (the `EventLoop` behind `ui/event/poll`): use `PollBackend` with ESP-IDF's
  `poll()`/`select()` (both exist through the VFS), and `esp_vfs_eventfd` for `WakeFD`/`EventFD`.
  `EpollBackend` and `SignalFD` stay Linux only.
- **Windows.** Build `ui/window/custom`, `ui/window/fb` and `ui/event/poll` without `NON_INTERACTIVE`.
  `TopWindow::RunEventLoop()` then runs on the XCSoar task, and `Flip()` keeps using the handoff above.
- **Touch.** An ESPHome `touchscreen::TouchListener` turns touch points into `MOUSE_DOWN`, `MOUSE_MOTION` and
  `MOUSE_UP` events and passes them to `EventQueue::Inject()`, which is already thread safe.
- **Partial updates.** Give the flip handler the dirty rectangle, so a small change does not copy the whole
  frame twice.
- **Milestone.** A port of `test/src/RunMapWindow.cpp`.

## Phase 3: the full application

- **Entry point.** Split `XCSoar.cpp`'s `Main()` into a function without `argv`, signals (`UI::BlockSignals`)
  or `ScopeGlobalAsioThread` networking. Choose the profile without the start-up dialog.
- **Storage.** The `storage` branch of the ESPHome fork provides SD card and FAT support (`sd_spi`, `fatfs`,
  `filesystem`). Its API is ESPHome's own `filesystem::Filesystem`, not the C library, while XCSoar uses POSIX
  `open()`/`stat()`/`opendir()`. Bridge the two with an ESP-IDF VFS driver on top of `filesystem::Filesystem`,
  mounted at `/sdcard`, built the same way as the `/rom` driver. `LocalPath` then points at `/sdcard/XCSoar`.
- **`mmap`.** `FileMapping` (used for PNG images and some data files) needs a fallback that reads the file
  into PSRAM.
- **Threads.** XCSoar asks for 640 KB stacks (`src/thread/Thread.cpp`). Give ESP-IDF a smaller size per thread
  and put the stacks in PSRAM (`pthread` attributes, or `esp_pthread_set_cfg` with
  `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY`). There are about six threads: calculation, merge, draw,
  terrain loader, port receive and Asio.
- **GPS and vario input.** Add a new port type, `PortType::EXTERNAL`: a `BufferedPort` whose `DataReceived()`
  is fed by an ESPHome `uart::UARTDevice` through a ring buffer. The UART is drained in the component's
  `loop()`. `TCP_LISTENER`/`UDP_LISTENER` over lwIP also work with no code change.
- **Libraries.** Lua, zzip, jasper (JPEG2000 terrain), libpng and libjpeg need IDF builds or stubs. Boost is
  header only. Networking (`HAVE_HTTP`: curl, c-ares, OpenSSL) can stay out at first
  (`NetComponentsStub.cpp`).
- **Settings.** XCSoar's profile stays XCSoar's own. Expose a few values (e.g. altitude, vario) as ESPHome
  sensors later if useful.

## Phase 4: optional

- A native RGB565 `PixelTraits` for the memory canvas: half the memory, and no per-frame conversion.
- Rotation through the P4's PPA (see the LVGL component's `ppa_rotate_`).
- Sound through an ESPHome speaker (XCSoar's vario tone generator, `PCMMixer`).
- Live tracking and downloads once networking is in.

## Risks

- **Code size.** The full XCSoar program is about 1,800 C++ files; with exceptions and unwind tables expect
  several MB of flash. The P4 boards have 16 MB or more.
- **CPU.** The memory canvas draws in software on a 360–400 MHz RISC-V core. The Kobo e-readers (1 GHz ARM)
  are the closest comparison. Drawing the map is the main cost.
- **ESP-IDF `poll()`** works only on VFS file descriptors. Every wake source must therefore be an eventfd or
  a socket.

## Verification so far

- The host test (`esp32/host-test`) builds the phase 1 sources with the host compiler and renders the test
  scene correctly.
- `RomFs.cpp` compiles (syntax check) against the ESP-IDF 5.5.1 VFS headers.
- `esphome config` and `esphome compile --only-generate` succeed for a P4 configuration.
- No ESP32 compile or hardware test has been done yet.
