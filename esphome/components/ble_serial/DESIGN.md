# ble_serial design

ble_serial connects to Bluetooth LE serial devices (HM-10, Nordic UART, ISSC and Control-J services). Today the
devices are chosen at run time from C++: an application (XCSoar) asks for a scan, gets each serial device found through
a callback, and opens or closes devices by address. Nothing can be done with it from YAML beyond `max_devices`.

This document plans YAML-configured devices. It is a plan only; none of it is written yet.

## Goal

Let a YAML config name fixed BLE serial devices, matched by service type (or custom UUIDs), address and name, and
use them as UARTs. The run-time C++ interface that XCSoar uses stays as it is.

## YAML

```yaml
ble_serial:
  id: ble_hub
  max_devices: 2              # slots for devices opened at run time from C++ (as now)
  devices:
    - id: vario_uart          # a uart::UARTComponent, usable by any UART component
      service: auto           # auto | hm10 | nordic_uart | issc | custom
      address: "C8:FD:19:*:*:*"   # optional; * matches any byte
      name: "BlueFly*"        # optional; * matches any trailing text
      buffer_size: 256
      on_connect: [...]
      on_disconnect: [...]
    - id: custom_uart
      service: custom
      service_uuid: 0000FFF0-0000-1000-8000-00805F9B34FB
      rx_characteristic: 0000FFF1-0000-1000-8000-00805F9B34FB   # notify, data from the device
      tx_characteristic: 0000FFF2-0000-1000-8000-00805F9B34FB   # write without response, data to it
```

- A device entry needs at least one of `address`, `name` or a specific `service`, so `auto` with no other filter
  cannot connect to every serial device nearby.
- `custom` requires all three UUIDs and is not allowed with the built-in types.
- A full address with no `*` connects only to that device, as `ble_client` does.
- Each device takes one connection slot from `esp32_ble`, as the run-time slots do now. The total is the number of
  devices plus `max_devices`.
- `max_devices` keeps its meaning (run-time slots). It could default to 0 when `devices` is given (see the
  questions below).

## Matching

- **Address:** Python parses the pattern into a value and a mask (`uint64_t`), so the device does no text parsing
  at run time. A device matches when `(address & mask) == value`.
- **Name:** an exact name, or a prefix followed by `*`. A name filter also covers devices that do not advertise
  their service, which BlueFly varios handle today through a fixed check. That check stays for `auto`.
- **Service:** for `auto`, any built-in serial service in the advertisement. For a specific type, that service,
  checked again once connected.
- **Binding:** while a channel is not connected, the first advertisement that matches binds it. It stays bound
  until the connection is lost, and then matches again. An option to stay bound to the first device found could
  come later.

## C++ changes

- **Service table:** the fixed list of three services becomes a list of `SerialService` entries. Each client holds
  a pointer to its entry, or to the built-in list for `auto`.
  - Custom UUIDs come from codegen as a constant on the channel.
  - A define compiles in only the built-in types the config uses. Without fixed devices all three are compiled in,
    which keeps run-time `open()` working as now.
- **New `BleSerialChannel`:** a `uart::UARTComponent` and `Component` holding one `BleSerialClient` and its match
  rule (address value and mask, name pattern, service).
  - Received data goes into a `FixedRingBuffer` of `buffer_size` bytes. Notifications arrive in the main loop, so
    the buffer needs no lock.
  - `write_array()` uses the client's existing `write()`, which already locks.
  - `flush()` waits until the transmit buffer is empty, up to a timeout. `load_settings()` does nothing, since
    there is no baud rate.
  - `on_connect` and `on_disconnect` use `build_callback_automation`.
- **BleSerial:**
  - `parse_device()` also checks unbound channels, and opens one when a device matches.
  - `update_scan_()` already scans while a device that should be open is not connected; fixed channels count the
    same way.
  - Run-time `open()` and `close()` use only the run-time slots, never a fixed channel.
- **Kept unchanged:** `start_scan()`, `stop_scan()`, `open()`, `close()`, `write()` and the three callbacks, with
  the same signatures. The device and connection callbacks also fire for fixed channels, so an application sees
  every connection.
- **New overload:** `open(address, service)`, so C++ can ask for a specific service type. `open(address)` stays as
  `auto`.

## Tests and docs

- Python tests: address patterns into value and mask, malformed patterns, the rule that a device needs at least one
  filter, `custom` requiring all three UUIDs, and slot counting against `esp32_ble`.
- C++ gtest for the address and name matching functions.
- Build tests: `test.esp32-idf.yaml` with one `auto` device matched by name, one `nordic_uart` device matched by an
  address pattern, one `custom` device, and a GPS component reading one of the channels. A `validate` test for the
  config errors.
- An esphome.io page for the component, with a GPS or vario example.
- Hardware: the T-Beam HM-10 config (`xcsoar-tests/tbeam-hm10-gps.yaml` on the xcsoar branch) as the device, with
  a fixed `hm10` channel matched by address pattern and by name. Then a reconnect test (power-cycle the T-Beam), and
  a check that the XCSoar run-time path still works with a fixed channel configured.

## Risks

- **Radio time:** a fixed device that is out of range keeps the scan running. On the Tab5 the scan shares the C6's
  radio with Wi-Fi. This may need a scan duty cycle, or a back-off after a while without a match.
- **Wrong device:** a wildcard that is too broad could bind a neighbour's device first. The docs should recommend an
  address pattern or a name.
- **Size:** the receive buffer is per channel (`buffer_size`); the transmit buffer stays 512 bytes.

## Open questions

1. Service names: `hm10`, `nordic_uart`, `issc`, or names closer to the hardware, such as
   `microchip_transparent_uart` for ISSC?
2. Should `max_devices` default to 0 when `devices` is given? It defaults to 2 today.
3. Should a channel stay bound to the first device it finds (until reboot), or match again after each lost
   connection?
4. Should there also be an `on_data` trigger for configs that do not use a UART component, or is the UART
   interface enough?
