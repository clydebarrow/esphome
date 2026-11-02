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
4. Save the club's Wi-Fi network under Settings. A long press on a saved network forgets it.
5. Update the ESP32-C6 once, on each new board: `tug-tests/tug-d1001-c6-update.yaml` installs ESP-Hosted 3.0.10. Boards
   arrive with 2.3.2, which logs `eh_init_evt: major version mismatch` at every boot.

`tug-tests/tug-d1001-bringup.yaml` checks the board alone: screen, touch, SD card, clock and SkyEcho.

## Files on the card, in `/tug`

| File | Purpose |
|---|---|
| `current.json` | The flight in progress, written every 30 s |
| `review.jsonl` | Landed tows awaiting the pilot |
| `tows-pending.jsonl`, `tows-sending.jsonl` | Confirmed tows to send: the exact bytes that are signed and posted |
| `tows-sent.jsonl`, `tows-failed.jsonl`, `tows-discarded.jsonl` | What was sent, refused (any 4xx but 401), or marked as not a tow |
| `recent.json` | Recent gliders and pilots for the picker |

## Decisions

- **Height** is measured from the altitude on the ground just before takeoff, with the same kind of altitude
  throughout: geometric if fresh, otherwise pressure. The field elevation is only used with no recent ground altitude,
  and that tow is marked approximate. A median of three samples rejects a single bad fix, which delays the peak by one
  sample.
- **Takeoff** is above 40 kt for 5 s, **landing** below 15 kt for 30 s; times are when the speed was first crossed.
  These thresholds are untested in a real tug.
- **A tow uploads only once confirmed**, because the server refuses changes to a tow it already holds (409).
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

## Not done

- GPS tracks, detecting the real release point, and lists from the server.
- NMEA on the D1001 needs the input decided; only the build with `gps_id` is tested.
