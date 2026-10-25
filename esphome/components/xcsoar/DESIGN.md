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
| ESPHome `xcsoar-tests/` | Device configs for testing on hardware, e.g. `xcsoar-tab5.yaml` for the M5Stack Tab5. |

The ESPHome component points `add_idf_component(path=...)` at a local XCSoar checkout (`xcsoar_path`).
XCSoar is GPL-2.0-or-later, so firmware built with it is GPL as well.

## Phase 1: rendering (done)

```yaml
display:
  - platform: mipi_dsi
    id: lcd
    model: M5STACK-TAB5-ST7123
    frame_buffers: 2
    rotation: 90

xcsoar:
  display_id: lcd
  xcsoar_path: ../XCSoar
  fonts:
    - fonts/DejaVuSansCondensed.ttf
    - fonts/DejaVuSansCondensed-Bold.ttf
```

```
 XCSoar task (core 0, PSRAM stack)            ESPHome main loop (core 1)
 ---------------------------------            --------------------------
 draw into the memory canvas (BGRA)
 TopCanvas::Flip() -> frame_ready_()
   wait until the back buffer is off screen
   PPA: BGRA -> RGB565, rotated, into
        the back frame buffer
   frame_pending_ = true
   enable_loop_soon_any_context() --------->  loop()
   wait on semaphore                            show_frame_buffer(back buffer)
                                   <--------    give semaphore
 next frame
```

- **Threads.** Display drivers are not thread safe, so only the main loop calls the display. Writing pixels into
  a frame buffer the panel is not showing is not a display call, so the PPA does that from the XCSoar task.
- **Double buffering.** With `frame_buffers: 2`, `mipi_dsi` allocates two panel frame buffers and gives a
  renderer `get_frame_buffer()`, `show_frame_buffer()` and `wait_frame_buffer_free()`. Showing a buffer is no
  copy: ESP-IDF switches the panel to it at its next refresh. The XCSoar task waits for that refresh before it
  writes into the buffer that was on screen, so there is no tearing.
- **PPA.** The P4's pixel processing unit converts XCSoar's BGRA canvas (ARGB8888 in little endian order) to the
  panel's RGB565 or RGB888 and rotates it to the display's `rotation` in the same pass. XCSoar draws at the
  rotated size (1280x720 on the Tab5 with `rotation: 90`).
- **Without double buffering** (any other display, or `frame_buffers: 1`), the PPA converts into a buffer of
  this component and the main loop copies it with `draw_pixels_at`, which takes about 16 ms per frame there.
  A rotation then goes through the display's slow per-pixel path.
- **The display's own drawing.** `mipi_dsi` turns on its test card when it has no writer. The component calls
  `stop_poller()` on the display, so `update()` never runs and the display never allocates its own buffer.
- **Measured on the Tab5** (720x1280 panel, test scene, per frame):

  | Step | Before | Now |
  |---|---|---|
  | XCSoar draws the scene (CPU) | 108 ms | about 92 ms |
  | Convert to RGB565 | 101 ms (CPU) | 56 ms (PPA, with or without rotation) |
  | Main loop | 15.6 ms (copy) | 0.1 ms (buffer switch) |
  | Frame rate | 4 to 5 fps | 6 to 7 fps |

  The PPA conversion is limited by the PPA itself (about 17 million pixels per second); the cache flush of the
  canvas takes only 0.5 ms of it.
- **Fonts.** XCSoar ships no fonts. The `fonts` option embeds TTF files in the firmware, and
  `XCSoarEsp32::MountRomFs()` registers a small read-only ESP-IDF VFS driver at `/rom`, so FreeType opens them
  with ordinary `fopen()`. That driver will also serve other embedded data later.
- **Memory.** `malloc()` normally stays in internal RAM under `CONFIG_SPIRAM_USE_CAPS_ALLOC`. XCSoar allocates
  with plain `new`, so `setup()` calls `heap_caps_malloc_extmem_enable(16384)`: blocks of 16 KB or more go to
  PSRAM. At 720x1280, the BGRA canvas (3.7 MB) and two RGB565 frame buffers (1.8 MB each) use about 7.4 MB
  of the 32 MB PSRAM.
- **Exceptions.** XCSoar reports errors with exceptions. The component sets `CONFIG_COMPILER_CXX_EXCEPTIONS`,
  and the IDF component compiles with `-fexceptions`, which overrides ESPHome's global `-fno-exceptions` for
  XCSoar's files only. ESPHome's `--wrap` stubs for `std::__throw_*` stay: a length or range error in the
  standard library still aborts, which is acceptable because it means a bug.
- **Test scene.** Phase 1 draws a map-like scene with a rotating aircraft symbol, using XCSoar's canvas,
  pens, brushes and FreeType fonts. It proves the build, the canvas, fonts and the frame handoff.

## Phase 2: event loop, windows and touch (done)

- **Event loop.** XCSoar's `EventLoop` (`src/event`) runs with `PollBackend`, using ESP-IDF's `poll()` and an
  `esp_vfs_eventfd` to wake up (`src/event/Features.h`, `src/system/EventFD.cxx`). The component registers the
  eventfd driver and asks for VFS select support. `ui/event/poll` has no signals and no child processes on
  ESP-IDF (`ESP_PLATFORM`).
- **Threads.** `ThreadId` uses the FreeRTOS task handle on ESP-IDF: `pthread_self()` aborts in a task that was
  not created through pthreads, and neither the XCSoar task nor ESPHome's main task is.
- **Windows.** The ESP32 build is the virtual frame buffer build (`USE_VFB`) plus `USE_EXTERNAL_FB`:
  `UI::Display` holds the host's flip handler, so the normal `TopWindow`, `SingleWindow` and `RunEventLoop()`
  code works unchanged. The test scene is now a `SingleWindow` that repaints on a `PeriodicTimer`.
- **Touch.** `USE_INJECTED_INPUT` gives the input queue no devices of its own;
  `XCSoarEsp32::InjectPointer()` passes `MOUSE_DOWN`, `MOUSE_MOTION` and `MOUSE_UP` events to the thread safe
  `EventQueue::Inject()`. The component is a `touchscreen::TouchListener` (`touchscreen_id`) and follows the
  first finger. The touchscreen maps touches to the display's rotated size; its `transform` must match the
  display's `rotation` (`swap_xy` and `mirror_y` for `rotation: 90` on the Tab5).
- **Test.** The test window counts taps, draws the path of the current touch and has a pause button.
  `xcsoar-tests/xcsoar-tab5-touch-sim.yaml` drives it with scripted touches through the listener calls, and the
  host test injects touches the same way.
- **Libraries.** Boost 1.92 (the release XCSoar uses, header-only parts so far) and fmt are downloaded once into
  `output/esp32` of the XCSoar checkout.
- **Not done yet.** Partial updates (handing the dirty rectangle to the flip handler), and the
  `RunMapWindow` milestone, which needs much of phase 3 (terrain, topography, waypoints, looks).

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

- **TODO: draw in RGB565.** Add an RGB565 `PixelTraits` to XCSoar's memory canvas (it has BGRA and greyscale
  today), so XCSoar draws straight into the display's frame buffer. That halves the memory XCSoar draws into
  and removes the PPA conversion (56 ms per frame on the Tab5). It needs 16-bit versions of the blending and
  anti-aliased text code, and a rotation would then need the PPA again or XCSoar's own handling.
- Overlap the PPA with drawing: start the conversion without waiting and draw the next frame into a second
  canvas, so a frame takes the longer of the two steps instead of their sum (3.7 MB more PSRAM).
- Sound through an ESPHome speaker (XCSoar's vario tone generator, `PCMMixer`).
- Live tracking and downloads once networking is in.

## Risks

- **Code size.** The full XCSoar program is about 1,800 C++ files; with exceptions and unwind tables expect
  several MB of flash. The P4 boards have 16 MB or more.
- **CPU.** The memory canvas draws in software on a 360 to 400 MHz RISC-V core. The Kobo e-readers (1 GHz ARM)
  are the closest comparison. Drawing the map is the main cost.
- **ESP-IDF `poll()`** works only on VFS file descriptors. Every wake source must therefore be an eventfd or
  a socket.

## Verification so far

- The host test (`esp32/host-test`) builds the phase 1 sources with the host compiler (Linux and macOS) and
  renders the test scene correctly.
- Built with ESP-IDF 5.5.6 and run on an M5Stack Tab5 (ST7123 panel, ESP32-P4 v1.3): with two frame buffers
  in portrait and with `rotation: 90`, and with one frame buffer (the copy path). A 60 s run showed no resets
  and no main loop blocking warnings.
- Phase 2 on the Tab5 with simulated touches: taps on the pause button toggle the animation and a drag draws
  its path. The real touch chip starts and reports its size, but no finger has touched it yet.
