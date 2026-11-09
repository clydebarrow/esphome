# Tug logger

Records each flight of a glider tug, lets the pilot complete it on a touchscreen, and uploads it to the flight log
Worker (`POST /api/tows` in `clydebarrow/flight-upload`). This branch (`tug`) is `skyecho` merged with `storage`, plus
`wifi_networks` from the xcsoar branch. It is not meant for upstream as it is.

## Setting up a device

1. Admin -> Provision a tug on the Worker downloads `tug-<REG>-device-N.json`, with the device key in plain text.
2. Copy it to the root of the SD card, under that name or as `tug.json`. If two `tug-*.json` are there and no
   `tug.json`, the device reports no tug file rather than guess.
3. Flash from the worktree: `PYTHONPATH=$PWD python -m esphome run tug-tests/tug-d1001.yaml`. The Wi-Fi settings and
   secrets come from `../../../my-devices/wifi.yaml`, relative to `tug-tests`.
4. Save the club's Wi-Fi network under Settings. Tapping a known network opens a dialog to change its password or
   forget it.
5. Update the ESP32-C6 once, on each new board: `tug-tests/tug-d1001-c6-update.yaml` installs ESP-Hosted 3.0.10. Boards
   arrive with 2.3.2, which logs `eh_init_evt: major version mismatch` at every boot.

`tug-tests/tug-d1001-test.yaml` and `tug-d1001-live-test.yaml` are bench builds, with `fly.py` for a simulated tow.

`tug-tests/tug-d1001-bringup.yaml` checks the board alone: screen, touch, SD card, clock and SkyEcho.

## Files on the card, in `/tug`

| File | Purpose |
|---|---|
| `totals.json` | The day's tows and flight time, and the flight time since refuelling |
| `current.json` | The flight in progress, written every 30 s |
| `review.jsonl` | Landed tows awaiting the pilot |
| `tows-pending.jsonl`, `tows-sending.jsonl` | Confirmed tows to send: the exact bytes that are signed and posted |
| `tows-sent.jsonl`, `tows-failed.jsonl`, `tows-discarded.jsonl` | What was sent, refused (any 4xx but 401), or marked as not a tow |
| `recent.json` | Recent gliders and pilots for the picker |
| `flights/*.csv` | A record of each flight, named by its takeoff in UTC: the last minute on the ground, then a line a second until landing, with speed, both altitudes, and the height worked out. For checking a tow's figures |

## Decisions

- **Height** is measured from the mean altitude over the last 30 s on the ground before takeoff, with the same kind of
  altitude throughout: pressure if there is one, otherwise GPS. GPS altitude was first until 9 November 2026, when tows
  read 300 to 700 ft low against glider traces and pilots' notes: GPS altitude wanders by a couple of hundred feet on
  the ground, and the reference was one reading. Pressure altitude held steady there, and the change in air pressure
  over a tow is small. The field elevation is only used with no recent ground altitude, and that tow is marked
  approximate, as is one with more than 60 s without data in flight; the Worker shows such heights with "≈". A median
  of three samples rejects a single bad fix, which delays the peak by one sample.
- **Takeoff** is above 40 kt for 5 s, **landing** below 15 kt for 30 s; times are when the speed was first crossed.
  These thresholds are untested in a real tug.
- **The status bar counts** the tows and the tug's flight time for the local date, and flight time since the Refuelled
  button was last pressed twice. A tow counts on landing. "Not a tow" takes it back out of the day's counts, but not
  out of the fuel time, since the tug still flew.
- **The glider on tow** is recognised in flight from a SkyEcho: a target of the glider category within 30 degrees of
  straight behind the tug's track, within 300 ft of its height and within 300 m. It stays on the display but raises no
  alert, and stays suppressed for 5 s after leaving that area, because positions are noisy at rope length. The first
  with a callsign to stay 30 s fills in the glider if none was entered, with `VH` removed to match how the club writes
  registrations. The pilot still checks it on review. The figures are untested in a real tow.
- **A tow uploads only once confirmed**, because the server refuses changes to a tow it already holds (409).
- **The clock is set only while the SkyEcho reports a GPS lock.** Without one it may still report a time, and a wrong
  one would stay in the RTC and date every tow.
- **A SkyEcho is first joined by hand** under Settings, because several may be in range at a launch point. Once saved,
  it is joined whenever it is seen.
- **Upload** happens only on a network that isn't a SkyEcho. `wifi_networks` joins a SkyEcho whenever it is seen, so
  tows wait until it is off or out of range.
- **A restart in flight:** a flight on the card is carried on if the aircraft is moving when a position arrives, and
  is otherwise closed at its last saved figures. Either way it is marked approximate.
- **On battery** with no external power, not flying and no touch for `shutdown_window`, the device runs the shutdown
  hooks (which unmount the card) and releases power hold.

- **The display** is portrait, with a light and a dark theme chosen in Settings. Re-initialising LVGL's default theme
  changes it while running; button text is set by `TugUi::set_dark_mode()`.
- **Power off:** holding the green button on battery counts down 3 s, then runs the shutdown hooks and drops power hold.
  The button holds the power on through the hardware (D19, Q1) while pressed, so the power goes when it is let go, and
  that release must not assert power hold again. With USB connected the board cannot turn itself off.
- **Simulated GDL90 marks the network it arrives over as a SkyEcho.** The device then neither uploads nor looks for the
  real one, until that network is forgotten. `fly.py` does this; a real club network never carries GDL90.
- **Wi-Fi changes in the `wifi` component** (not upstream): a disconnect event for an earlier attempt no longer fails
  the attempt in progress, a scan first cancels the C6's own reconnect after a restart, and an aborted or refused scan
  is tried again after 1 s. Without them the device looped through adapter restarts after losing a SkyEcho.

## Hardware (reTerminal D1001, from Seeed's schematic)

- SD card: CLK 43, CMD 44, D0 39, D1 40, D2 41, D3 42, detect 45, power enable 46. Used in SPI mode (`sd_spi`).
- No usable serial header: J5 is not fitted and the mPCIe UART doesn't reach the P4. For NMEA, look first at
  ESPHome `usb_serial` on the USB JTAG port. The `gps_id` option takes any `gps` component.

## Screens and boards (`tug-tests/`)

- `tug-common.yaml` holds the screens and settings. A board's file gives `display_width`, `display_height` (as the
  screens are seen) and `ui_rotation`; sizes and fonts are those for 800x1280 times `scale`, the smaller of the two
  ratios. Each board also gives `time` id `rtc` and `shutdown_script`, which the Shut down button in Settings runs.
- **Splash:** the first page, white in either theme, with `logo.png` from the card's root (optional) above the
  organisation from the tug's file. It shows for 5 s from when the card has been read, or 12 s without a card.
- **JC1060P470 shutdown:** it has no power control, so it sleeps with the C6 and backlight off until the reset button
  is pressed, or external power is cycled with no battery fitted. On battery the board stays powered: on an early board
  (no Ethernet) the side button reaches no P4 GPIO and the charger ignores its presses, so only removing the battery
  turns it off. The BOOT button is GPIO35, which can't wake the P4 from deep sleep.
- **Firmware updates:** Settings > Check for update asks the Worker (`/api/firmware/check`, beside the upload address
  in the tug's file, signed with its key) for firmware built with this device's name other than the running build.
  A second press installs it with `ota.http_request.flash`, after saving any flight and unmounting the card. The link
  expires after 10 minutes, so a failed install asks for a fresh check. The Worker's README covers uploads.
- **Settings:** refreshed every second while it shows, including the Wi-Fi state ("joining" while connecting). A
  saved network can be joined from its dialog with Connect.
- **JC1060P470 power:** the USB-C ports and 4-pin socket share a 5 V rail the battery doesn't feed, so a USB GPS needs
  5 V in at another port. The board can't sense external power, so nothing shuts it down when that goes.

## Not done

- GPS tracks, detecting the real release point, and lists from the server.
- NMEA on the D1001 needs the input decided; only the build with `gps_id` is tested.
