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
| XCSoar `build/targets.mk` | `TARGET=ESP32`: the make build's view of the ESP32, from which the sources and flags are taken. |
| XCSoar `esp32/generate-sources.py` | Runs make's generate step for `TARGET=ESP32` and writes `output/ESP32/Sources.cmake`. |
| XCSoar `esp32/ThirdParty.cmake` | Builds zlib, libpng, Lua, IJG libjpeg and c-ares from source (`esp32/cares` is c-ares's configuration). |
| XCSoar `esp32/Sources.cmake` | The test scene's short source list for the host test, and the library downloads. |
| XCSoar `esp32/host-test/` | Builds the test scene for Linux or macOS and writes a frame to a PPM file. No ESP toolchain needed. |
| XCSoar `src/ui/display/Display.hpp` | `USE_EXTERNAL_FB` (with `USE_VFB`): the display hands each frame to the host application. |
| XCSoar `src/ui/canvas/esp32/Files.cpp` | Font search paths: `/sdcard/XCSoar/fonts`, then `/rom/fonts`. |
| ESPHome `components/xcsoar/` | Adds the IDF component, starts the task, hands frames to the display. `filesystem_vfs` makes a `filesystem` usable through the C library. |
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

## Phase 3: the full application (in progress)

Done:

- **Build.** The whole program (about 1450 XCSoar sources) builds for the ESP32. `TARGET=ESP32` in
  `build/targets.mk` selects what applies (the virtual frame buffer, no TTY serial ports, no processes, no
  GeoTIFF, no HTTP), and `esp32/generate-sources.py` takes the lists and flags from make, so they cannot drift.
  The generator also runs make's generate step, which needs the normal build's host tools (perl, ImageMagick,
  rsvg-convert, sox, gettext). The IDF component runs it when its output is missing; run it again after make
  file changes.
- **Entry point.** On ESP-IDF, XCSoar's `main()` is `XCSoarMain()`. `XCSoarEsp32::Run()` passes the screen
  size and `-datapath=` as command line arguments, so the normal start-up runs.
- **Threads.** XCSoar's main thread is a pthread (`pthread_self()` aborts in other FreeRTOS tasks), started
  with `esp_pthread_set_cfg()` so that it and the threads it creates have their stacks in PSRAM.
  `InitThreadDebug()` records it as the main thread, as on Android.
- **Storage.** The `storage` branch is merged: `sd_spi` and `fatfs` give the SD card, and `filesystem_vfs` in
  the component makes any `filesystem::Filesystem` usable through the C library at `/sdcard`, behind one lock.
  XCSoar's data directory is `/sdcard/XCSoar`. On the Tab5 the card works in SPI mode on GPIO39 to GPIO44,
  which need LDO channel 4.
- **Memory and flash.** Resources and data files are compiled into `.rodata` (`RESOURCE_SECTION`), so they
  stay in flash. The built-in translations (about 10 MB) are left out; XCSoar can load a `.mo` file from the
  data directory instead. `FileMapping` reads files into memory, since there is no `mmap()`.
- **Libraries.** zlib, libpng, Lua and c-ares are the releases `build/python/build/libs.py` uses. libjpeg is
  IJG libjpeg 9f instead of libjpeg-turbo, which refuses to be built inside another CMake project; XCSoar only
  uses the API both share. c-ares's configure checks cannot run with the ESP-IDF toolchain, so it has a
  hand-written configuration for lwIP. Boost and fmt are header only. RTTI is on
  (`CONFIG_COMPILER_CXX_RTTI`), for Boost's R-tree and fmt.
- **Internal RAM.** FreeRTOS objects (including the lock of every `FILE`) and DMA buffers must be in internal
  RAM. XCSoar allocates a great deal in small blocks, so the component sends every allocation above 64 bytes to
  PSRAM (`heap_caps_malloc_extmem_enable()`); about 320 KB of internal RAM stays free with XCSoar running.
- **Fonts.** XCSoar loads a FreeType face per font size and every face keeps its file open, more files than
  ESP-IDF allows; on ESP-IDF each font file is read once and its faces are made from memory.
- **Result.** XCSoar starts on the Tab5; with simulated taps on SIM and on the welcome dialog's Close it shows
  the map in simulator mode with its InfoBoxes. The firmware uses 7.99 MB of the 8.1 MB app partition.
  `xcsoar-tests/xcsoar-tab5-app-sim.yaml` is that test.

To do:

- **Flash space.** The app partition is nearly full. Options: a partition table with one app partition (no
  OTA), dropping debug assertions (`NDEBUG`), or keeping the resources on the SD card.
- **Thread stacks.** XCSoar asks for 640 KB per thread (`src/thread/Thread.cpp`); in PSRAM that works, but a
  smaller ESP32 size would save several MB.
- **GPS and vario input.** A new port type, `PortType::EXTERNAL`: a `BufferedPort` fed by an ESPHome
  `uart::UARTDevice` through a ring buffer, drained in the component's `loop()`. `TCP_LISTENER`/`UDP_LISTENER`
  over lwIP work without code changes once Wi-Fi is up.
- **DNS.** c-ares finds no `/etc/resolv.conf`; give it lwIP's DNS servers when networking is added.
- **Watchdog.** Just after start-up something on an XCSoar thread calls `esp_task_wdt_reset()`, which logs
  "task not found" once; find out what.
- **Data.** The SD card has no waypoints, airspace, terrain or maps yet, so the map is empty; test with real
  data files.
- **Networking** (`HAVE_HTTP`: curl, OpenSSL or mbedTLS) is left out for now.
- **Settings.** XCSoar's profile stays XCSoar's own. A few values (e.g. altitude, vario) could be ESPHome
  sensors later.

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
- Phase 3 on the Tab5: the full XCSoar starts from the SD card and, with simulated taps, reaches the map in
  simulator mode; no resets in 100 s.
