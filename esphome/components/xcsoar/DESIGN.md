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
| XCSoar `src/Device/Port/HostPort.{hpp,cpp}` | `PortType::HOST`: a device port that reads data the host passes to `XCSoarEsp32::Feed()`. |
| ESPHome `components/xcsoar/` | Adds the IDF component, starts the task, hands frames to the display, feeds traffic. |
| XCSoar `src/net/wifi/HostWifiBackend.hpp`, `esp32/xcsoar/src/Wifi.cpp` | The Wi-Fi list and Network settings on the host's Wi-Fi control (`XCSoarEsp32::WifiHost`). |
| ESPHome `components/wifi_networks/` | Networks saved at run time, SkyEcho networks first; see "Wi-Fi networks". |
| ESPHome `components/skyecho/` | GDL90 over Wi-Fi from a SkyEcho (or the gdlsim simulator) and FLARM over a UART; merged from the `skyecho` branch. |
| ESPHome `xcsoar-tests/` | Device configs for testing on hardware, e.g. `xcsoar-tab5.yaml` for the M5Stack Tab5. |

The ESPHome component points `add_idf_component(path=...)` at a local XCSoar checkout (`xcsoar_path`).
XCSoar is GPL-2.0-or-later, so firmware built with it is GPL as well.

## Working on it

### Branches

| Repository | Branch | Holds |
|---|---|---|
| `clydebarrow/esphome` | `claude/brave-sagan-50uf3g` | This component, the test configs, and merges of `storage` (SD card, filesystem, `vfs_path`) and `skyecho`. |
| `clydebarrow/XCSoar` | `claude/brave-sagan-50uf3g` | The ESP32 port: `esp32/`, `TARGET=ESP32`, and the XCSoar changes listed in this document. |
| `clydebarrow/esphome` | `storage` | The storage components, worked on separately; merge it in when it changes. |
| `clydebarrow/esphome` | `skyecho` | The skyecho component, local only so far; its commits reach this branch by merging. |
| `clydebarrow/esphome` | `mipi-dsi-fill-dirty-qos-clock`, open as https://github.com/esphome/esphome/pull/21638 | `fill()` marks the screen dirty, the panel DMA gets the highest AXI read priority, Tab5 ST7123 at 70 MHz. |

This branch is not meant to be merged upstream as it is. Put the XCSoar checkout next to the ESPHome worktree
as `xcsoar-src` (the `xcsoar_path` substitution in `xcsoar-tests/xcsoar-tab5.yaml` is `../../xcsoar-src`,
relative to that file), on the same branch name.

### Building and flashing

- ESP-IDF 5.5.6; the first build also needs the host tools listed under "Build" in phase 3 (perl, ImageMagick,
  rsvg-convert, sox, gettext) to generate XCSoar's sources.
- Run from the ESPHome worktree, so that its `esphome` package is used:
  `PYTHONPATH=$PWD python -m esphome compile xcsoar-tests/xcsoar-tab5-skyecho.yaml`. A full build takes
  several minutes.
- `xcsoar-tests/network.yaml` includes `../../../my-devices/wifi.yaml` for the Wi-Fi settings (with
  `!secret`); provide your own there. Never commit credentials.
- **Before a USB flash or reset, press the "Safe shutdown" button** over the API
  (`.agents/skills/hardware-test/scripts/api_button.py HOST - press "Safe shutdown"`). It unmounts the SD card.
  Resets while XCSoar was writing have twice destroyed the card's `XCSoar` directory (see "SD card damage"),
  and the data (maps, airspace, profile) had to be copied back.
- The test configs have no `ota:` for now, so every flash is over USB. OTA is wanted later; it would run the
  shutdown hooks itself.

### Test configs (`xcsoar-tests/`)

| File | Use |
|---|---|
| `xcsoar-tab5.yaml` | The base: Tab5 board, SD card, display, touch, Wi-Fi (`network.yaml`), XCSoar. |
| `xcsoar-tab5-skyecho.yaml` | The base with the skyecho component feeding traffic. |
| `xcsoar-tab5-tap.yaml`, `tap.py` | The skyecho config with a `tap(x, y)` API action, and a "Mark flying" button that writes `xcsoar-session.txt` so the next start asks about an unfinished flight. `python tap.py HOST 400,620` taps FLY on the start screen; `+0.15` before two taps makes them a double tap (the menu). "Log tasks" logs where each task is waiting. |
| `xcsoar-tab5-app-sim.yaml` | Scripted taps into simulator mode and pan mode, with heap and stack reports. |
| `xcsoar-tab5-touch-sim.yaml` | Phase 2 touch test with the test scene. |
| `tab5-sd.yaml` | Lists the SD card, without XCSoar. |
| `debug_helpers.h` | Allocation reports and the sampling profiler. |

Screen coordinates are XCSoar's landscape 1280x720. Some that the tests use: FLY 400,620, SIM 870,620; the
Quick Guide's Next 110,605, its "don't show again" check box 165,678 and Close 70,675; a double tap on the map
at 640,300 opens the menu, whose buttons are in columns at x 128 and 1152, rows at y 216, 360, 504 and 648.

### Traffic without a SkyEcho

The gdlsim simulator (`gdl90_simulator.py`, a separate project) sends GDL90 like a SkyEcho, with moving
traffic and callsigns: `python3 gdl90_simulator.py --target <device address>`. It sends to UDP port 4000 and
answers discovery; the skyecho component on the device needs no settings for it. Its websocket is on port 7070,
where a real SkyEcho has it on 80 (port 80 is taken on the bench Mac); give the skyecho time platform `port: 7070`
to set the date from gdlsim.

### Checking the screen

There is no screenshot command. Testing so far used a camera pointed at the panel, and the frame and touch
logs. The touch test configs drive XCSoar with the same listener calls as the touchscreen, so most UI work can
be tested without anyone at the device.

## How the application is structured

### Start-up

`XCSoarComponent::setup()` (ESPHome main task) prepares the environment and then starts XCSoar:

1. `heap_caps_malloc_extmem_enable(0)`: from here on, every `malloc()` goes to PSRAM first. FreeRTOS objects and
   DMA buffers ask for internal RAM explicitly.
2. Registers the eventfd VFS driver (4 descriptors) that XCSoar's event loop wakes up on.
3. Gets a frame buffer or its own conversion buffer for the display, and calls `stop_poller()` on the display.
4. Takes its data path from the data filesystem's `vfs_path` (`/sdcard/XCSoar`) and mounts the embedded fonts
   at `/rom`.
5. Starts the XCSoar main thread with `pthread_create()` (core 0, priority 1, stack in PSRAM). The
   `esp_pthread_cfg` is set with `inherit_cfg`, so every thread XCSoar starts gets a PSRAM stack too.

The XCSoar main thread waits up to 5 s for the card (`wait_ready()`), then calls `XCSoarEsp32::Run()`
(`RunDemo()` with `demo: true`). `Run()` builds a command line (`-1280x720 -datapath=/sdcard/XCSoar`, and `-dpi=`
with the `dpi` option) and calls
`XCSoarMain()`, which is XCSoar's normal `main()` under another name. From there XCSoar's own start-up
(`src/Startup.cpp`) runs unchanged: data layout migration, the startup dialog (profile, FLY or SIM), the
profile, then terrain, topography, waypoints and airspace.

### Threads at run time

| Thread | Created by | Does |
|---|---|---|
| ESPHome main task | ESP-IDF | `loop()`: shows finished frames, passes touches on. The only caller of the display. |
| XCSoar main thread | the component (pthread) | XCSoar's UI: event loop, windows, dialogs, `TopCanvas::Flip()`. |
| `DrawThread` | XCSoar | Draws the map into its own buffer (the build without OpenGL). |
| `CalculationThread`, `MergeThread` | XCSoar | Glide computer: merges device data, runs the task and glide calculations. |
| Terrain and topography threads | XCSoar | Load map tiles and shape files from the `.xcm` file in the background. |
| `AsioThread`, `JobThread`, workers | XCSoar | Asynchronous I/O and one-off jobs (file loading at start-up). |

All XCSoar threads are pthreads with 128 KB PSRAM stacks (640 KB elsewhere); the largest use seen is about 25 KB.
They run on either core. The main thread has priority 2, and every other XCSoar thread lowers itself to 1 when it
starts, the same as the ESPHome main loop (XCSoar's own priority calls do nothing on ESP-IDF), so the UI stays
responsive while the map is drawn. They talk to each other only through XCSoar's own locks and the event queue;
the component touches none of XCSoar's state.

### Data paths between ESPHome and XCSoar

- **Frames:** XCSoar main thread -> `frame_ready_()` -> PPA into the back frame buffer -> `loop()` shows it
  (see phase 1).
- **Touch:** touchscreen component -> `XCSoarComponent::update()`/`release()` (main task) ->
  `XCSoarEsp32::InjectPointer()` -> `EventQueue::Inject()`, which wakes the XCSoar event loop by its eventfd.
- **Files:** XCSoar uses `open()`, `fopen()`, `opendir()` and `stat()`. Paths under `/sdcard` go through
  the `filesystem` component's VFS (`vfs_path: /sdcard`, FAT on the SD card), behind one mutex. XCSoar reserves
  8 files and 4 directories on top of the filesystem's own. `/rom` is the read-only font store in flash.
- **Traffic:** `skyecho` callbacks (ESPHome main task) -> `XCSoarEsp32::Feed(name, bytes)` -> `HostPort::Push()`
  (a 4 KB FIFO per port, under a mutex) -> `InjectEvent` -> `HostPort::OnInject()` on the XCSoar event loop ->
  `DataReceived()`, which hands the bytes to the device's driver (GDL90 or FLARM). See "Data from the host".
- **Logging:** XCSoar writes its log to `/sdcard/XCSoar/xcsoar.log`; the component logs through ESPHome.

### SD card layout

XCSoar's data directory is `/XCSoar` on the card. At start-up XCSoar moves files it finds at the top level
into these folders (`DataLayoutMigration.cpp`, marker file `.xcsoar-subdir-layout-v1`):

| Folder | Files | Profile key |
|---|---|---|
| `profiles/` | `default.prf` (settings) | |
| `maps/` | `*.xcm`: a zip with terrain (`terrain.jp2`), topography shape files, and sometimes waypoints and airspace | `MapFile` |
| `waypoints/` | `*.cup`, `*.dat`, `*.wpt`, `*.cupx` | `WPFileList` |
| `airspace/` | OpenAir `*.txt`, `*.openair` | `AirspaceFileList` |
| `tasks/`, `logs/`, `planes/`, `flarm/`, `input/`, `lua/` | tasks, IGC logs, polars, FLARMnet, key maps, scripts | |
| `fonts/` | TTF files that replace the embedded ones | |

Files are picked in the profile (or in Config > System > Site Files). Values may start with `%LOCAL_PATH%\`,
which stands for the data directory; list keys separate files with `|`. For example:

```
MapFile="%LOCAL_PATH%\maps\AUS_QLD.xcm"
WPFileList="%LOCAL_PATH%\waypoints\warwck7e.cup"
AirspaceFileList="%LOCAL_PATH%\airspace\AU-ASP-National-SoaringWeb.txt"
HomeLocation="151.94305 -28.14945"
```

Maps, waypoints and airspace files are listed at `http://download.xcsoar.org/repository`.

## Phase 1: rendering (done)

```yaml
display:
  - platform: mipi_dsi
    id: lcd
    model: M5STACK-TAB5-ST7123
    frame_buffers: 2
    rotation: 270

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
 draw into the memory canvas (RGB565)
 TopCanvas::Flip() -> frame_ready_()
   wait until the back buffer is off screen
   PPA: rotate (and RGB888 if needed) into
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
- **PPA.** The P4's pixel processing unit copies XCSoar's canvas (RGB565 since phase 4, `BytesPerPixel()`) to the
  panel's RGB565 or RGB888 and rotates it to the display's `rotation` in the same pass. XCSoar draws at the
  rotated size (1280x720 on the Tab5 with `rotation: 270`).
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
  display's `rotation` (`swap_xy` and `mirror_x` for `rotation: 270` on the Tab5, `mirror_y` for 90).
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
- **Storage.** The `storage` branch is merged: `sd_spi` and `fatfs` give the SD card. `vfs_path` on a filesystem
  (`filesystem::Vfs`) makes it usable through the C library below that path, behind one lock; it started in this
  component and moved to `filesystem`, since any component may want it.
  XCSoar's data directory is `/sdcard/XCSoar`. On the Tab5 the card works in SPI mode on GPIO39 to GPIO44,
  which need LDO channel 4. XCSoar saves files (the profile, for one) by writing a temporary file and renaming it
  over the old one; FAT refuses to rename onto an existing file, so `rename()` removes the old file first.
- **Memory and flash.** Resources and data files are compiled into `.rodata` (`RESOURCE_SECTION`), so they
  stay in flash. The built-in translations (about 10 MB) are left out; XCSoar can load a `.mo` file from the
  data directory instead. `FileMapping` reads files into memory, since there is no `mmap()`.
- **Libraries.** zlib, libpng, Lua and c-ares are the releases `build/python/build/libs.py` uses. libjpeg is
  IJG libjpeg 9f instead of libjpeg-turbo, which refuses to be built inside another CMake project; XCSoar only
  uses the API both share. c-ares's configure checks cannot run with the ESP-IDF toolchain, so it has a
  hand-written configuration for lwIP. Boost and fmt are header only. RTTI is on
  (`CONFIG_COMPILER_CXX_RTTI`), for Boost's R-tree and fmt.
- **Internal RAM.** FreeRTOS objects (including the lock of every `FILE`) and DMA buffers must be in internal
  RAM. XCSoar allocates a great deal in small blocks (parsing an airspace file alone used 200 KB), so the component
  sends all of `malloc()` to PSRAM (`heap_caps_malloc_extmem_enable(0)`); about 150 KB of internal RAM stays free
  with XCSoar running.
- **PSRAM.** Measured at 1280x720 with the test card, before RGB565: two RGB565 panel buffers 3.7 MB, XCSoar's
  screen 3.7 MB, three map buffers (double buffer and work buffer) 7.2 MB, the topography and airspace caches
  2.4 MB each, icons and other bitmaps several MB, terrain tiles 128 KB each, the welcome dialog's text cache
  4.7 MB. That did not fit in 32 MB. Now: RGB565 drawing (phase 4) halves the screen, map buffers and bitmaps;
  topography is drawn without a cache on ESP-IDF (as with OpenGL); at most 16 terrain tiles stay loaded. With
  `dpi: 200`, 9.4 MB stays free; with `dpi: 294` (the Tab5's real density, larger icons) 4.3 MB.
- **Stacks.** 128 KB per XCSoar thread. JasPer's wavelet code had 256 KB column buffers on the stack; on ESP-IDF
  they are 16 KB, and longer columns use the heap as before.
- **Watchdog.** `sd_spi` and `storage` fed the task watchdog during transfers from whatever task made them. On
  an XCSoar thread that failed ("task not found") and also updated ESPHome's last feed time, so the main loop
  stopped feeding it and the watchdog fired. They now feed it only on the main loop thread.
- **Fonts.** XCSoar loads a FreeType face per font size and every face keeps its file open, more files than
  ESP-IDF allows; on ESP-IDF each font file is read once and its faces are made from memory.
- **Display density.** XCSoar sizes fonts and symbols by DPI and assumes 96 without it, which is far too small
  on the Tab5. The `dpi` option passes `-dpi=`.
- **Flash.** The sounds (1.8 MB) are left out until there is sound output. XCSoar is compiled with `-O2`
  instead of ESPHome's `-Os` (map drawing about 30% faster, 0.9 MB more flash). The firmware uses 7.1 MB of the
  8.1 MB app partition, so the default partition table with two OTA slots stays.
- **Map buffers.** Pan mode and hiding the InfoBoxes make the map full screen. XCSoar then allocated its five map
  sized buffers again at 1.8 MB each, which fragmented PSRAM until a full-screen buffer no longer fitted and
  XCSoar aborted (the reboot on Pan). A memory canvas now reuses its buffer when a resize fits in it, and on
  ESP-IDF a buffer of at least half the screen is allocated at full-screen size from the start
  (`VirtualCanvas::reserve_size`). PSRAM free with the map full screen: 5.6 MB, largest block 4.6 MB (before:
  5.9 MB, largest block 1.5 MB).
- **White map.** While the draw thread renders a new map, `MapWindow::OnPaint()` showed the old map moved to the
  new position for two paints only, then a white map; after a resize it went white at once. A render takes
  about 20 ms on a desktop, so this was never visible there, but up to seconds here: a white flash after a
  drag, and seconds of white after Pan. The old map is now shown until the new one is ready.
- **Result.** With the Queensland map, the Warwick waypoints and the Australian airspace on the card, XCSoar starts
  on the Tab5 and, with simulated taps on SIM and on the welcome dialog's Close, shows the map in simulator mode:
  terrain, topography, waypoints, airspace, InfoBoxes and an airspace warning. No resets in 200 s.
  `xcsoar-tests/xcsoar-tab5-app-sim.yaml` is that test; it also reports large and failed allocations
  (`debug_helpers.h`), free memory and the unused stack of every task.
- **Debug checks.** XCSoar is built with `NDEBUG` (`esp32/xcsoar/CMakeLists.txt`), and the component sets
  `CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE: false` so that ESP-IDF's `assert()` does not evaluate its argument
  either. Flash use is now 6.82 MB of the 8.13 MB app slot (83.9%; 84.3% with `skyecho`), internal RAM 53.7%.
- **UI fixes found in use** (XCSoar commits on the branch):
  - The Quick Guide threw `std::bad_alloc` in `VirtualCanvas::Create`: every `RichTextWidget` kept a full-size
    text cache while hidden. It now frees it on `Hide()`.
  - The progress window (splash and loading bar) did not show during start-up. `ProgressGlue::Disable()` is now
    called only in `Shutdown()`.
  - Images lost their transparent parts in the memory canvas (black boxes behind icons). Bitmaps keep an
    alpha plane (`ImportAlpha`), drawn with `Canvas::StretchWithAlpha` and `BlendOverWhite`.
  - `RichTextWindow` paints straight to the screen on ESP32 instead of through a second full-size buffer.
  - The traffic page had no Close and Details buttons: `MainWindow::InitialiseConfigured()` did not set
    `area_stack_rect`, so the widget area overlapped the InfoBoxes.
  - The small FLARM radar on the map was a square: without OpenGL it needs `SetTransparent()` and a filled
    circle in `FlarmTrafficWindow::OnPaint`. Cost: a transparent window makes `TopWindow` repaint the whole
    screen each time the radar changes (see "Partial repaint").
- **Traffic.** A SkyEcho's GDL90 traffic (and FLARM, if the skyecho component has a FLARM UART) reaches XCSoar's
  radar and traffic list, with callsigns. Tested with the gdlsim simulator; see "Data from the host".

To do:

- **Flash space.** 1.3 MB are left in each OTA slot. More room: keeping resources (sounds, large bitmaps) on
  the SD card. OTA itself is wanted, so the two-slot partition table must stay.
- **GPS and vario input.** Use the host stream mechanism below: a component (UART GPS, a pressure sensor turned
  into `$POV` or `$PLXVF` sentences, or the SkyEcho's own ownship report) calls `XCSoarEsp32::Feed()` with a
  stream whose driver is the matching XCSoar driver (`"Generic"` NMEA for a GPS). `TCP_LISTENER`/`UDP_LISTENER`
  over lwIP should work without code changes now that Wi-Fi is up, but have not been tried.
- **Dates.** GDL90 has no date. The GDL90 driver takes the date from the system clock (its "use system UTC
  date" setting, on by default), so the clock must be set or IGC logs are dated 1970. `time: - platform: skyecho`
  sets it from the GPS time in the SkyEcho's websocket `/stats` stream (`host.GPS.UTCTime`, seconds since the
  GPS epoch). It connects to the address the GDL90 data comes from, reads one message and closes, every 15
  minutes and every 30 s until the first sync. A real SkyEcho (firmware 0.2.38) has the websocket on port 80,
  the default `port`, and sends `UTCTime` as gdlsim does; it also has a `/` stream with its position, callsign,
  fix and satellite count. Its `/stats` messages are about 1400 bytes (the receive buffer is 2048). Tested
  against gdlsim, and against the real SkyEcho with a host build on the Mac joined to its network: it found the
  SkyEcho, received its traffic and set the clock to the right UTC time.
- **SD card.** After the storage merge (`origin/storage`, card and filesystem locks), `sd_spi` once logged
  "Card not responding", restarted the card and remounted the filesystem while XCSoar had files open. That may
  be how the directory damage below happens; the storage work is done on the `storage` branch.
- **skyecho issues seen here.** The broadcast discovery ping failed (`Sendto failed ... err -1`) on the IPv6
  socket that `socket_ip()` makes with IPv6 enabled; it now has an IPv4 socket, and the failures are gone. The
  skyecho `loop()` took 200 to 650 ms
  while XCSoar was starting (it blocks the main loop, so frames and touches wait).
- **Stuck dialogs.** Twice a nested dialog stopped reacting: the Map Item List Settings dialog (from a tap on
  the map), and Config > Devices > Edit after setting the port to Disabled. After a tap on OK or Cancel the
  screen stayed as it was and further taps did nothing, while XCSoar's timers kept running (the 30 s device
  reconnect still logged); for the device edit, "Saving profiles" was never logged, so the OK handler did not
  run to its save. Not found yet. `xcsoar-tab5-tap.yaml` has a "Log tasks" button (`dump_tasks()` in
  `debug_helpers.h`) that logs each task's state and the code addresses on its stack, for addr2line; press it
  while a dialog is stuck. Repeat: FLY, double tap the map (`+0.15 640,300 640,300`), Config 128,360, Devices
  1152,360, row B 800,200, Edit 160,40, Port 700,178, Disabled 500,50, OK 420,640.
- **DNS.** c-ares finds no `/etc/resolv.conf`; give it lwIP's DNS servers when networking is added.
- **Loading time.** Start-up takes about 40 s with this card: terrain 19 s, airspace 18 s; measure where the
  time goes (SD card reads in SPI mode, or parsing). The screen is not redrawn while loading, so taps made then
  are handled only when loading ends.
- **Speed.** See "Performance" below: `double` arithmetic in airspace warnings and terrain scanning.
- **Real touch.** Works in use. In a menu, the button that leads to the next page is labelled with that page
  ("Config Page 2/3" on page 1), which reads as if the menu opened on page 2.
- **Brief flicker (found).** The panel showed an occasional whole-screen frame shifted towards green. Two causes,
  both between the frame buffer and the panel, not in the frames XCSoar sends:
  - The DSI driver restarts the panel's DMA from an interrupt at the end of every frame. Interrupts kept off for
    longer than the vertical blanking (about 2.5 ms) delay that, and the next frame comes out wrong, usually
    without an underrun being logged. In the test config, `heap_caps_get_largest_free_block()` every 5 s (it walks
    the PSRAM heap inside the heap's critical section) and `uxTaskGetSystemState()` every minute (10 ms, scanning
    the 128 KB stacks) did this; a 7 s heap report moved the green frames to a 7 s period. Neither runs on a timer
    any more, and the panel's frame interrupt was then never late in five minutes.
  - Underruns: the panel cannot read its frame buffer from PSRAM in time while XCSoar draws. Giving its DMA the
    highest AXI priority (`mipi_dsi`) took 13 minutes of dragging from 5 underruns to 1; running the panel at
    70 MHz instead of 80 MHz (58 Hz) gave none in a five-minute run.
- **SD card damage.** After many resets while XCSoar was writing (reflashing, and the reboots on Pan), the
  first sector of the card's `XCSoar` directory was overwritten: its `.` and `..` entries were gone, and from byte
  96 on it held a repeating pattern that looks like SD bus traffic during a write (a data response token, busy
  zeros, then 0xFF), found nowhere else on the card. `maps`, `airspace` and `profiles` were lost with it. Nothing
  outside `XCSoar` was hurt, and no SD or SPI error was logged. `sd_spi` is the only device on its bus, so the
  card most likely stored bytes from a transfer that went out of step. The same damage happened a second time,
  after another reset from the USB port while XCSoar was running. The filesystem now unmounts in
  `on_shutdown()`, holding the VFS lock so XCSoar's threads cannot write after that; this covers software
  restarts (OTA, a restart button), not a reset from the USB port. To do: a debug option in `sd_spi` that reads
  every written sector back and logs a mismatch, run under XCSoar with repeated resets.
- **Networking** (`HAVE_HTTP`: curl, OpenSSL or mbedTLS) is left out for now.
- **Settings.** XCSoar's profile stays XCSoar's own. A few values (e.g. altitude, vario) could be ESPHome
  sensors later.

## Data from the host

XCSoar reads instruments through device drivers on ports (serial, TCP, Bluetooth). On the ESP32 the data
comes from ESPHome components instead, so there is one more port type, `DeviceConfig::PortType::HOST` (profile
value `host`), implemented by `HostPort`:

- The host declares its streams before XCSoar starts, in `XCSoarEsp32::Config::streams`: a name, which is also
  the port name XCSoar shows ("SkyEcho"), and the driver that reads it by default ("GDL90").
- After `LoadProfile()`, `HostPort::ApplyStreams()` puts each stream that no device reads yet into the first
  disabled device slot, with its driver, and adds its name to the profile key `HostStreams` (names separated
  by `|`). A stream named there is never set up again, so a device the user set to another port or to Disabled
  in Config > Devices stays that way. A stream that found no free slot is not recorded and is tried again at the
  next start. The key reaches the file with the next profile save (closing Config > Devices saves); deleting it
  offers every stream again.
- `XCSoarEsp32::Feed(name, data)` may be called from any task. It copies the data into the FIFO of each open
  port with that name and wakes the event loop; data for a stream that no open port reads, or that does not fit
  in the FIFO (4 KB), is dropped. GDL90 and NMEA recover from a lost chunk. Writes from XCSoar to the port are
  discarded.
- At most 4 streams and 8 open ports (`MAX_STREAMS`, `open_ports` in `HostPort.cpp`).

The xcsoar component uses it for the skyecho component (`skyecho_id`, which defines `USE_XCSOAR_SKYECHO`):

```yaml
skyecho:
  id: skyecho_hub

xcsoar:
  skyecho_id: skyecho_hub
```

`setup()` adds the stream "SkyEcho" with the GDL90 driver, and "SkyEcho FLARM" with the FLARM driver when the
skyecho component has a FLARM UART, and registers `add_on_gdl90_data_callback()` and
`add_on_flarm_data_callback()` (added to skyecho for this; `LazyCallbackManager`, so they cost nothing
without a listener). The callbacks pass the raw bytes the skyecho component received, so XCSoar's own
drivers decode them. `xcsoar-tests/xcsoar-tab5-skyecho.yaml` is the test config.

A new input (GPS, vario) needs: a stream name and driver in `setup()`, and a call to `Feed()` wherever the
component has the bytes. If the data is not yet in a format an XCSoar driver reads (a pressure reading, say),
format it as the sentences of a simple driver (e.g. OpenVario `$POV`) rather than writing a new driver.

## Wi-Fi networks

The user picks and saves networks in XCSoar (Config > Setup > Network, then "WiFi List"); ESPHome keeps them and
chooses which to join. They are kept in ESPHome, not in XCSoar's profile, so Wi-Fi (and OTA) works without XCSoar
or the SD card, and passwords stay off the card.

- **`wifi_networks` component** (`max_networks`, default 8; `skyecho_id`). It keeps the saved networks in one
  preference, and before `wifi` sets up (priority `WIFI + 1`) it takes a copy of the configured networks and gives
  `wifi` the list: saved SkyEcho networks (priority 100), other saved networks (50), then the configured ones
  (their own priority). A saved network stands in for a configured one of the same name. `wifi` scans and joins the
  visible network with the highest priority, so it needs `fast_connect: false` (checked at validation): fast_connect
  goes back to the last network first.
- **SkyEcho networks are found by their data and by name:** when the skyecho component receives GDL90, the network
  in use is saved (with its password, if it is a configured one) and flagged as a SkyEcho. With gdlsim on the home
  network, that network is flagged. A network whose name starts with "SkyEcho" (a real one is `SkyEcho_MKJ`, open)
  is a SkyEcho too: an open one is saved and flagged when a scan sees it, and one with a password is flagged when
  it is joined.
- **Switching to a SkyEcho:** while another network is in use, every 60 s `wifi_networks` scans without leaving it
  (`WiFiComponent::scan_while_connected()`, added on this branch with `get_sta_list()`). If a saved SkyEcho is in
  range, `wifi` is restarted (`disable()`, `enable()`), and its scan picks the SkyEcho. After a switch the next one
  waits 5 minutes, so a SkyEcho that is seen but cannot be joined does not drop the other network every minute.
- **User override:** joining a network in the list keeps that network alone in `wifi`'s list until "Automatic
  network" is turned on again, Wi-Fi is turned off and on, or a restart. "WiFi Enabled: Off" and Disconnect turn
  Wi-Fi off until it is turned on or the device restarts.
- **Threads:** XCSoar calls `XCSoarEsp32::WifiHost` (set in `Config::wifi` by the xcsoar component's
  `wifi_networks_id`) from its own threads. `wifi_networks` queues those requests for the main loop, and answers the
  list and status from a copy it refreshes every second, under a mutex. Results show in the dialog, which refreshes
  every second, shortly after a request.
- **Not handled:** a network saved by the captive portal or Improv replaces `wifi`'s list at every `wifi` start,
  so do not use them with `wifi_networks`. Hidden networks cannot be added from the list.

Tested on the Tab5 (`xcsoar-tab5-tap.yaml`): the panel and list show the network in use with its IP address, the
list fills from a scan without the connection dropping, Forget restarts Wi-Fi and the network is flagged again as
GDL90 arrives, and the list survives a restart. Joining the real SkyEcho from the list (open network) worked: it was
flagged as its GDL90 arrived, and its traffic reached XCSoar. With both saved, forgetting the home network restarted
Wi-Fi and the scan chose the SkyEcho (priority 100, -81 dBm) over the home network (priority 0, -37 dBm). With the SkyEcho turned on while the Tab5 was on the home network, the next 60 s scan saw it
and the Tab5 switched (7 s from the scan to connected), and on the Tab5 the clock was set from the SkyEcho's
`/stats`. Not tested yet: the save by name of a SkyEcho never joined before, joining a network with a password, and
Wi-Fi off and on. The test taps go through the API,
which the Tab5 loses on the SkyEcho network: the SkyEcho does not pass traffic between its Wi-Fi clients. The
restart of `wifi` holds the main loop for about 330 ms.

## Bluetooth LE devices (built; needs C6 firmware with Bluetooth)

Varios, FLARM adapters and GPS units with a Bluetooth LE serial link are added in XCSoar, as on Android: the device
list is kept in XCSoar's profile, and ESPHome only provides the radio. The configuration says how many devices may be
connected at once, never which ones.

**How Android XCSoar does it.** In Config > Devices > Edit > Port, `PortPicker.cpp` starts a scan while the list is
open (through `DetectDeviceListener`, a plain C++ interface). Each device found is added with
`UpdatePortEntry(df, type, address, name)` as a "BLE port" when it advertises a serial service (HM-10 `FFE0`, Nordic
UART, or ISSC as used by BlueFly), or as a "BLE sensor" (heart rate and others). The choice is stored as the port type
and `bluetooth_mac` in the device slot (`Profile/DeviceConfig.cpp`, not Android specific). `ConfiguredPort.cpp`
opens the stored address; on Android, `BleSerialPort.java` finds the serial service and asks for a 512 byte MTU.

**On the ESP32:**

- **`XCSoarEsp32::BleHost`** (`Config::ble`, next to `WifiHost`): `start_scan`, `stop_scan`, `open(address)`,
  `close(address)` and `write(address, data, size)`. The host answers with `XCSoarEsp32::BleDetected(address, name,
  features)` for each device found, `BleConnected(address, connected)`, and `Feed(address, data)` for received data.
  Addresses are written `AA:BB:CC:DD:EE:FF`.
- **XCSoar side:** `src/Device/Port/HostBluetooth.hpp` declares what XCSoar calls; `esp32/xcsoar/src/Bluetooth.cpp`
  implements it on the `BleHost`. The port picker's detection path (`HAVE_DETECT_DEVICE` in `PortPicker.cpp`) is
  enabled on `ESP_PLATFORM` too: while the picker is shown it registers as a listener, which starts the host's scan,
  so the user picks from the same list as on Android. `DeviceConfig::IsAvailable()` allows `BLE_SERIAL` there, and
  `ConfiguredPort.cpp` opens it as a `HostPort` named after the address with `bluetooth` set. Such a port asks the
  host to connect when it is created and to let go when it is deleted, sends writes to the host (settings and task
  declarations need them), and is `PortState::LIMBO` until `BleConnected()` says the device is ready; the state
  change reaches XCSoar on its event loop.
- **ESPHome component `ble_serial`** (`max_devices`, default 2, taken from `esp32_ble`'s connection slots). It has one
  `BleSerialClient` (a `BLEClientBase`) per device, given its address at run time. While a scan is requested it
  reports each device with a serial service, or named "BlueFly...", once per scan (again when its name becomes
  known). After a connection it finds the serial service (HM-10, Nordic UART or ISSC, as `BleSerialPort.java`), asks
  for the notifications of its data characteristic and passes them on. Writes are kept in a 512 byte buffer per
  device and sent from the main loop as write-without-response, in pieces of MTU - 3 bytes (20 with the default
  MTU), at most 4 per loop and not while the link is congested. A lost device is connected again when it next
  advertises. The public methods may be called from any task: requests are queued for the main loop.
- **Scanning shares the C6's radio with Wi-Fi,** so it runs only while the picker is open or an opened device is not
  connected (`esp32_ble_tracker` with `continuous: false`; `ble_serial` turns the tracker's continuous scan on and
  off). The scan is active, so that names in scan responses are seen.
- **The xcsoar component** takes `ble_serial_id` (`USE_XCSOAR_BLE_SERIAL`) and connects the two.
- **BLE serial only.** BLE sensors are decoded in Java on Android; on the ESP32 they would be decoded in
  `ble_serial` and passed on as sentences of an existing driver, later.

`xcsoar-tests/xcsoar-tab5-ble.yaml` is the test config. It builds: Bluedroid on the P4 (`esp32_ble` with
`esp32_hosted`, `CONFIG_ESP_HOSTED_HOST_FEAT_BT`) and `ble_serial` add 388 KB of flash (89.3% of the app partition
used, 870 KB free) and 19 KB of internal RAM to `xcsoar-tab5-skyecho.yaml`.

**The Tab5's C6 firmware (1.4.1) has no Bluetooth.** On the first run `esp32_ble` failed with "BT controller init
failed; co-processor firmware 1.4.1 may lack BT support" and marked itself failed; the rest (Wi-Fi, XCSoar, the
picker's scan request) ran on, and the tracker refused the scan. The C6 needs firmware built with Bluetooth, which the
`esp32_hosted` `update` platform can install (from an embedded image or over HTTP). `xcsoar-tests/tab5-c6-update.yaml`
installed ESPHome's ESP-Hosted 3.0.10 image (Wi-Fi and Bluetooth over VHCI, the version of the host library) in
about 10 s. Started from `on_update_available` it ran again after every restart, because the image reports its
version as "1" and the C6 as "3.0.10"; the config now installs only from a button. The C6 has no classic Bluetooth,
so RFCOMM devices cannot be used either way.
Devices that need pairing would need bond keys kept somewhere. An address saved in lower case (by hand) would not
match the data, which comes with upper case addresses.

`xcsoar-tests/tbeam-hm10-gps.yaml` makes a LilyGo T-Beam into an HM-10 style GPS for testing: service `FFE0`,
characteristic `FFE1` (read, write without response, notify), NMEA from its GPS in 20 byte notifications. Read from
a Mac with bleak, the NMEA arrives with a 515 byte MTU.

## Phase 4: optional

- **RGB565 drawing (done).** `USE_RGB565` selects `RGB565PixelTraits` for the memory canvas and its bitmaps.
  Channels are widened to 8 bits for the per-channel operations, so blending and anti-aliased text are unchanged;
  like greyscale it has no alpha channel (bitmaps already lose theirs when loaded). The PPA still rotates. Drawing
  straight into the panel's frame buffer without rotation would remove the PPA step too.
- Overlap the PPA with drawing: start the conversion without waiting and draw the next frame into a second
  canvas, so a frame takes the longer of the two steps instead of their sum (3.7 MB more PSRAM).
- Sound through an ESPHome speaker (XCSoar's vario tone generator, `PCMMixer`).
- Live tracking and downloads once networking is in.

## Performance

Measured on the Tab5 (360 MHz) with the Queensland map and the national airspace file, in simulator mode with
the map full screen. The ESP32-P4's floating point unit does single precision (`float`) only, so every
`double` operation is done in software, and XCSoar uses `double` for almost all of its arithmetic.

| Work | Time | Notes |
|---|---|---|
| Terrain shading (`GenerateSlopeImage`) | 4 to 7 s, now 0.26 s | per-pixel `double` math and `sqrt`, now `float` |
| Terrain height scan (`ScanMap`) | 0.21 s | projection per sample, `double` |
| Whole terrain layer | 0.5 s; 0.18 s while panning | 3.6 s at first; coarser (4 pixels) while panning or zooming |
| Airspace layer | 0.43 s, up to 2.8 s | the padding fill draws every airspace twice (stencil and fill) |
| Whole map render | 1.2 s, up to 3.8 s | |
| Calculation, per GPS fix | 166 ms | `ProcessBasicTask` 57 ms |
| Calculation, idle pass (about 1 s) | 470 ms | airspace warnings 340 ms, now about 200 ms |

Times are medians, measured after the threads were allowed on both cores (all XCSoar threads used to share
core 0, which made most of them about twice as long). The airspace warnings test every nearby airspace;
around Warwick that includes large ones such as the Brisbane FIR CTAs, whose boundaries have many points, and
the point-in-polygon test compared `GeoPoint`s (`double`) for every edge, and every boundary segment's
`FlatRay` did two `double` divisions. The inside test now skips edges by their flat (integer) latitude, with the
same result, and the rays no longer divide: each of the three predictions (glide, filter, task) takes about 60 ms
instead of 100 ms. The flat coordinates themselves (about 111 m per unit) are too coarse to replace the exact
test.

A sampling profiler (`prof::start()` in `xcsoar-tests/debug_helpers.h`) records where preempted XCSoar threads
were. While dragging the map, a quarter of the time went to `memmove` and filling: XCSoar's window system
repaints the whole screen for every frame, so full 1.8 MB buffers are copied and cleared in PSRAM each time.
JPEG 2000 decoding of terrain tiles took about 12%, polygon fills and wide lines about 8%.

- **Terrain tiles.** 32 tiles now stay loaded instead of 16 (2 MB more PSRAM; about 4.9 MB stayed free), and
  JPEG 2000 decoding no longer shows in the profile while dragging. JPEGDEC or the P4's JPEG decoder cannot
  help: the terrain is JPEG 2000, with 16-bit heights.
- **Partial repaint.** Invalidating a window now passes the window up to `TopWindow`, which keeps a list of up to
  16. With `USE_EXTERNAL_FB`, `Expose()` repaints each of them and every window above it that overlaps what was
  repainted, and `TopCanvas::Flip()` passes the changed rectangles (up to 8) to the host, which converts only
  those, together with the previous frame's (the panel alternates between two buffers). It repaints everything
  for a transparent window that would be painted over itself, a window partly off the screen, a full list or a
  removed window. Containers that draw over their children (dialog borders, the cursor bar, the InfoBox arrange
  screen) are repainted whole, and moving or resizing a window repaints its parent. Frames now convert 73% of the
  screen on average with the InfoBoxes shown, and 89% in pan mode, where the map covers the screen and changes
  with every frame. The profile shows about 45% less filling; copying (mostly the map buffer) is unchanged.

`IsSlowCPU()` is true on ESP32 (it reports the configured CPU frequency), which turns off InfoBox fades,
kinetic scrolling and other animations: each of their steps costs a whole frame here.

XCSoar's draw stopwatch (`STOP_WATCH` in `Screen/StopWatch.hpp`) logs the time of each map layer; it is off in
the build.

## Drawing with LVGL (considered)

The idea: keep XCSoar's calculations, and draw the screen with LVGL, using pre-rendered fonts and images and
LVGL's partial updates.

- **The map** is most of the drawing time, and LVGL does not help there: terrain shading, topography and
  airspace polygons are XCSoar's own renderers and would still draw into one large image. The map moves with
  the glider, so most of it changes on every frame and partial updates save little.
- **InfoBoxes, menus and dialogs** are cheap to draw in comparison (FreeType glyphs are already cached).
  Replacing them means writing a new user interface: XCSoar has several hundred dialogs and widgets, all
  written against its own window classes.
- **Partial updates** are possible without LVGL: XCSoar already knows which windows are invalid, so only those
  areas could be converted and sent to the panel.

So LVGL would be a new application around XCSoar's engine rather than a port, for a small gain. Making the map
renderers cheaper (`float`, less work per frame) helps more.

## Work on the ESPHome main loop instead of threads

Considered and not done. ESPHome expects each `loop()` to return within a few milliseconds, and XCSoar's
threads exist because their work does not:

- The UI thread runs nested modal loops (every dialog has its own event loop in `ShowModal()`), which cannot be
  split into polled steps without rewriting XCSoar's dialogs.
- `DrawThread` takes 0.7 to 4 s per map frame (see "Performance"), and the terrain and topography loaders block
  on SD card reads and JPEG 2000 decoding for seconds.
- `CalculationThread` takes about 175 ms per GPS fix and 500 ms per idle pass.
- `MergeThread` is light and could run in `loop()` next to a UART port, but it takes the same locks as the UI
  thread, so it would make the main loop wait on XCSoar.

A thread costs a 128 KB PSRAM stack and nothing else, so moving work into the main loop would save little.

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
- With RGB565 and real data (map, waypoints, airspace) on the Tab5: the map shows terrain, topography,
  waypoints, airspace and an airspace warning; no resets in 200 s at `dpi: 200` and 150 s at `dpi: 294`. The host
  test renders the test scene correctly in RGB565.
- Pan mode with real data: no allocations when the map goes full screen, 5.6 MB of PSRAM free, no resets over
  two minutes of dragging. The timings in "Performance" were measured in the same runs. With the old map kept on
  screen until the new one is drawn, a probe that sampled the map area of every frame found white maps only in
  the first 5 s after start-up (no map drawn yet), none after Pan and none in two minutes of dragging; before,
  the map was white for 9 s after Pan and flashed white after drags.
- Traffic with `xcsoar-tab5-skyecho.yaml` and gdlsim: XCSoar sets up the "SkyEcho" device with the GDL90 driver
  on a fresh profile, and the simulated aircraft show on the round radar and in the traffic list with their
  callsigns. The traffic page's Close and Details buttons, the Quick Guide and the start-up progress window were
  checked on the panel with the camera.
