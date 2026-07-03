# MasterBus Protocol — Reverse Engineering Handoff

> **Status:** Derived from CAN bus traffic capture and Mastervolt documentation.
> Suitable as a starting point for implementing a new MasterBus participant device.
> Some fields marked ⚠️ are inferred and should be validated experimentally.

---

## 1. Physical Layer

- **Bus type:** CAN bus, 29-bit extended frame IDs
- **Speed:** 250 kbit/s (standard Mastervolt MasterBus speed)
- **Topology:** Multi-drop, all devices share the bus

---

## 2. CAN ID Structure

Every 29-bit extended CAN ID encodes three logical fields:

```
Bit:  28        19 18        0
      ┌──────────┬──────────────────┐
      │  kind    │      IDB         │
      │ 10 bits  │    18 bits       │
      └──────────┴──────────────────┘
```

### 2.1 IDB — Device Address (bits 17:0, 18 bits)

The unique address of the sending or target device. Assigned at manufacture and fixed per unit. This is the same value as **IDB** in Mastervolt's Modbus interface documentation.

IDB is self-reported during device discovery (see §5) — it does not need to be calculated or looked up.

### 2.2 `kind` Field (bits 28:18, 10 bits)

The `kind` field is itself a composite of three sub-fields:

```
Bit within kind:  9      8  7    6  5    4        0
                  ┌──────┬────┬────┬────┬──────────┐
                  │  E   │ MT      │Tab │   IDAL   │
                  │ 1 bit│ 2 bits  │2 b │  5 bits  │
                  └──────┴────┴────┴────┴──────────┘
```

| Sub-field | Bits | Description |
|-----------|------|-------------|
| `E` | 9 | **Extended/Query bit.** `1` = query/request, `0` = response/announcement |
| `MT` | 8:7 | **Message Type category** (see §2.3) |
| `Tab` | 6:5 | **TabNr** — data category: `0`=monitoring, `1`=alarm, `2`=history, `3`=config |
| `IDAL` | 4:0 | **Device class identifier** (see §3) |

### 2.3 Message Type (`MT`) Categories

`MT` and `E` together form bits[9:7] of `kind`:

| bits[9:7] | Kind range | Family |
|-----------|-----------|--------|
| `010` | `0x10x`–`0x15x` | Announcement |
| `011` | `0x18x`–`0x1dx` | Ping / low-level query |
| `100` | `0x20x`–`0x27x` | Data response (Tab in bits[6:5]) |
| `101` | `0x29x`–`0x2bx` | Channel enumeration |
| `110x` | `0x60x`–`0x67x` | Extended data query (bit 9 set) |
| `110x` | `0x68x`–`0x6bx` | Extended channel query |

**Key rule:** For data messages, bit 9 (`E`) distinguishes query from response with the same Tab and IDAL. Example: `0x613` (query, Tab 0, battery) vs `0x213` (response, Tab 0, battery).

**Addressing direction:** frames a device sends on its own behalf (announcements, beacons, data responses) carry the *sender's* IDAL and IDB in the CAN ID; directed frames (queries, pings) carry the *target's*. Confirmed live (July 2026): after a newcomer announced itself, an existing bus device sent a directed frame bearing the newcomer's own IDAL and IDB. The IDB in a frame therefore identifies the sender only for non-directed kinds.

### 2.4 Construction Formula

```
kind   = (E << 9) | (MT << 7) | (Tab << 5) | IDAL
CAN_ID = (kind << 18) | IDB
```

---

## 3. IDAL — Device Class

The low 5 bits of `kind` identify the device class. All messages sent by or addressed to a device share its IDAL. Confirmed values from this bus:

| IDAL | Hex | Announcement kind | Device class |
|------|-----|-------------------|--------------|
| 14 | `0x0e` | `0x10e` | Switch output / relay |
| 19 | `0x13` | `0x113` | DCShunt / battery monitor |
| 20 | `0x14` | `0x114` | Masterview display |
| 24 | `0x18` | `0x118` | DC-DC converter (Mac Plus) |
| 26 | `0x1a` | `0x11a` | MasterBus-USB interface (announcement observed live; device identity likely but unconfirmed) |
| 10 | `0x0a` | `0x10a` | Charger ⚠️ (from Mastervolt docs, not observed) |

Note: IDAL is also the value of `data[0]` in all announcement frames, and equals `kind & 0x1F` for any frame from that device.

---

## 4. Known Devices on This Bus

Identified from the CAN traffic capture:

| IDB | IDAL | Device | Article | Serial |
|-----|------|--------|---------|--------|
| `0x1150d` | `0x14` | Masterview display | — | — |
| `0x2c944` | `0x13` | Battery / DCShunt | — | — |
| `0x1605c` | `0x0e` | Single-gang switch output | 77030500 | M825H0156 |
| `0x0788c` | `0x18` | Mac Plus 24/12-25 DC-DC #1 | 81205200 | M803E0012 or M820E0041 |
| `0x100a9` | `0x18` | Mac Plus 24/12-25 DC-DC #2 | 81205200 | M803E0012 or M820E0041 |
| `0x2ff8d` | `0x1a` | MasterBus-USB interface (not USB-connected, mostly silent) | — | — |

The exact serial-to-IDB mapping for the two DC-DC converters is unconfirmed; power-cycle one at a time to determine which IDB disappears.

The USB gateway (`0x2ff8d`) announces itself with its own class `0x1a` (observed live, July 2026) but also sends messages with varying IDAL values, consistent with it bridging between Modbus and MasterBus on behalf of multiple device types.

---

## 5. Device Discovery

Devices announce themselves at boot and in response to new devices joining the bus. No pre-knowledge of IDB or IDAL is needed — all addressing information is obtained from the bus itself.

### 5.1 Announcement Frame Format

All `0x11X` family announcements share the same 8-byte payload:

```
Byte:  0       1    2    3       4    5    6    7
       ┌───────┬────┬────┬────┬────┬────┬────┬────┐
       │ IDAL  │    IDB (little-endian, 3 bytes)   │  counter (LE uint16) │ 00 │ 00 │
       └───────┴────┴────┴────┴────┴────┴────┴────┘
```

- `data[0]` = IDAL (device class, matches `kind & 0x1F`)
- `data[1:4]` = IDB as 24-bit little-endian (upper 6 bits unused)
- `data[4:6]` = 16-bit little-endian message sequence counter (increments per announcement)
- `data[6:8]` = `0x00 0x00`

Example — battery announcing itself:
```
CAN_ID = 0x044ec944   kind=0x113  IDB=0x2c944
data:    13 44 C9 02  61 03 00 00
         ↑  └──────┘  └──────┘
        IDAL IDB LE   counter=0x0361
```

### 5.2 Startup / Discovery Sequence

1. **Existing devices** periodically broadcast `0x11X` announcements (~every 30 s)
2. **New device joins:** Send an empty presence beacon, then a full self-announcement:
   ```
   # Step 1: empty beacon
   kind = (0x02 << 7) | (0x02 << 5) | your_IDAL   # = 0x140 | IDAL — bits[9:7]=010, tab=2
   CAN_ID = (kind << 18) | your_IDB
   data = (empty)

   # Step 2: self-announcement
   kind = 0x100 | your_IDAL                         # bits[9:7]=010, tab=0
   CAN_ID = (kind << 18) | your_IDB
   data = [your_IDAL, IDB_byte0, IDB_byte1, IDB_byte2, counter_lo, counter_hi, 0, 0]
   ```
3. **Every device on the bus replies** to a new arrival with its own `0x11X` announcement
4. **Extract** IDB from `data[1:4]` LE and IDAL from `data[0]` of each reply

Verified live (July 2026): sending the beacon (kind `0x140 | IDAL`) followed by a
self-announcement made every device on the bus reply, including a Mac Plus DC-DC
that never transmits otherwise. Shortly afterwards an existing device sent a
directed frame addressed to the newcomer's IDAL/IDB (see §2.3 addressing direction).

A captured display boot (July 2026) shows it does **not** discover the bus from
a blank slate: it began sending directed pings to previously-known devices
(battery, switch, both DC-DCs) about 1.2 seconds *before* sending its own
presence beacon and self-announcement. It likely keeps a persistent address
book across power cycles and resumes polling remembered devices immediately,
independent of when it re-introduces itself.

### 5.3 Keep-Alive / Presence

Devices repeat their announcements approximately every 30 seconds. The display also sends an empty beacon (kind `0x154`) periodically. Implement a similar heartbeat to remain visible on the bus.

### 5.4 Ping

To confirm a device is alive after discovery, send a directed ping:

```
# Request
kind    = 0x1C0 | target_IDAL    # bits[9:7]=011, tab=2, E=0 — confirmed live (July 2026)
CAN_ID  = (kind << 18) | target_IDB
data    = [0x08, 0x3F]

# Response (sender-addressed: carries the responding device's own IDB)
kind    = 0x180 | target_IDAL
data    = [0x08, 0x3F, device_type_byte, 0x00]
```

The `device_type_byte` in the ping response carries a device-class count or sub-type
(values observed live, July 2026):
- Battery: `0x02`
- Display: `0x10`
- DC-DC: `0x04`
- Switch output: `0x01`

**The "ping" query is one Tab-indexed variant of a broader low-level query
family, not a single fixed kind.** The family occupies kind range `0x180`–`0x1FF`
(E=0, MT=3); within it, Tab selects a query sub-type and **all responses use
Tab=0** (`0x180 | IDAL`) regardless of which Tab the request used:
- Tab=0 (`0x180 | IDAL`): response only.
- Tab=1 (`0x1A0 | IDAL`): a second, mostly-unexplored query type. Captured
  live directed at the switch with codes `[0x08, 0x23]` and `[0x08, 0x07]`;
  the switch did not answer either. Since responses don't carry the request's
  Tab, a query code's meaning is only defined within its own Tab — `0x23`
  under Tab=1 is unrelated to the `[0x08, 0x23]` sent to us under Tab=2.
- Tab=2 (`0x1C0 | IDAL`): the ping/device-identification family documented
  above and in §5.4's newcomer-interrogation notes.
- Tab=3 (`0x1E0 | IDAL`): unexplored; the one code tried against the battery
  (`[0x08, 0x3F]`) got no reply, consistent with it being a different,
  unmapped code space rather than proof the kind is unused.

Live observations (July 2026):

- Devices reply to a `0x1C0`-family ping within ~10 ms.
- `0x1E0`-family requests (the previously suspected alternative) get no reply.
- The MasterBus-USB interface (`0x2ff8d`) does not answer pings.
- `[0x08, 0x3F]` is one of a family of low-level queries carried on the `0x1C0`
  request kind. Each query is retried aggressively (dozens of frames) until
  answered, so a bus participant should answer them. The display interrogates a
  newcomer step by step: once the ping `[0x08, 0x3F]` is answered it proceeds
  with `[0x08, 0x08]`, `[0x08, 0x23]`, `[0x09, 0x01]` and `[0x09, 0x03]`
  (`[0x08, 0x0B]`, `[0x08, 0x12]` and `[0x08, 0x1F]` were also seen). Answers
  observed from the battery: `08:02` → `08:02:02:00`, `08:12` → `08:12:0D:00`,
  `08:1F` → `08:1F:13:00` (echoes its own IDAL, so `08:1F` = "what device class
  are you?"), and `09:03` → `09:03:58:01` — after which the display fetched
  string labels starting at index `0x0158`, so `09:03` returns the device's
  name label index.
  Answers observed from the display itself: `08:02` → `08:02:04:00`,
  `08:0B` → `08:0B:0E:02`, `08:12` → `08:12:00:00` (0 for the display vs 13
  for the battery — plausibly the monitoring-variable count), `09:01` →
  `09:01:22:00` and `09:03` → `09:03:00:00`; it does **not** answer
  `[0x08, 0x08]` or `[0x08, 0x23]`.
- The `09:01` and `09:03` answers are both string label indices: after receiving
  them from a newcomer, the display fetches those labels with §7.5 queries
  (`data = [0x30, index_lo, index_hi, chunk]`), retrying until answered. So a
  participant that answers `09:01`/`09:03` must also serve the labels it
  pointed at.
- Label meanings (confirmed by reading them back from the display): `09:03`
  points at the device name — display label `0x0000` = "DIS Easy", battery
  label `0x0158` = "BAT LiIon House" — and `09:01` points at the article
  number: display label `0x0022` = "77010310" (a MasterView Easy part number).
- Once a newcomer has answered the enumeration (ping, `09:01`/`09:03`, labels),
  the display adds it to its ~5-second liveness ping rotation.
- The display also emits an announcement-style payload (its IDAL + IDB LE +
  incrementing counter) on its `0x194` response kind roughly every 5 seconds,
  purpose unknown.
- The display pings each device on the bus roughly every 5 seconds — this
  appears to be the normal liveness poll.
- The `0x180` response kind is shared with string-label responses (§7.5) and other
  low-level query responses; match a ping response by its echoed `[0x08, 0x3F]`
  leading bytes, not by kind alone.
- Unexplained: bursts of `kind 0x1E0, IDB 0x00000, data [0x08, 0x12]` were observed
  shortly after a newcomer join — possibly a broadcast form of a low-level query.

---

## 6. Data Model — Tab / Index / Sub-index

Data variables on each device are addressed by three coordinates, matching Mastervolt's Modbus interface:

| Coordinate | Bits in `kind` | Range | Description |
|------------|----------------|-------|-------------|
| **Tab** | bits[6:5] | 0–3 | Category: 0=monitoring, 1=alarm/label, 2=history, 3=config |
| **Index** | payload bytes[0:2] | device-specific | Selects the variable within the tab |
| **Sub-index** | payload byte[1] (Tab ≥1) | device-specific | Selects channel within a multi-channel variable |

---

## 7. Message Types — Query and Response

### 7.1 Tab 0 — Monitoring (live telemetry)

**Query** (`E=1`, `MT=10`, `Tab=00`):
```
kind   = 0x600 | (0 << 5) | IDAL    # = 0x610 | IDAL  e.g. 0x613 for battery
CAN_ID = (kind << 18) | target_IDB
data   = [index_lo, index_hi]        # uint16 LE
```

**Response** (`E=0`, `MT=00`, `Tab=00`):
```
kind   = 0x000 | (0 << 5) | IDAL    # = 0x200 | IDAL  e.g. 0x213 for battery
data   = [index_lo, index_hi, value_b0, value_b1, value_b2, value_b3]
         # index = uint16 LE, value = float32 LE
```

Battery monitoring items (Tab 0):

| Index | Description | Unit |
|-------|-------------|------|
| `0x0000` | State of charge | % |
| `0x0001` | Voltage | V |
| `0x0002` | Current | A |
| `0x0003` | Amp-hours consumed | Ah |

The battery broadcasts Tab 0 data **unsolicited** approximately every 1–2 seconds,
but only indices 0–2 (state of charge, voltage, current). Other indices — battery
temperature (`0x0005`) in particular — are never broadcast and must be polled with
a Tab 0 query. Other devices may need explicit polling for everything.

### 7.2 Tab 1 — Alarm / Label metadata

**Query** (`E=1`, `Tab=01`):
```
kind   = 0x620 | IDAL               # e.g. 0x633 for battery
data   = [index_lo, sub_index, 0x00]
```

**Response** (`E=0`, `Tab=01`):
```
kind   = 0x220 | IDAL               # e.g. 0x233 for battery
data   = [index_lo, sub_index, 0x00, flags, unit_id_lo, unit_id_hi]
```

The `unit_id` returned is itself an index that can be resolved to a string via the string label protocol (§7.5). Known unit IDs from the battery:

| unit_id | String |
|---------|--------|
| `0x0011` | `%` |
| `0x00fc` | `V` |
| `0x00e7` | `A` |

Battery Tab 1 items:

| Index | Sub-index | Description |
|-------|-----------|-------------|
| `0x0002` | 0,1,2 | Channel count / channel presence flags |
| `0x0008` | 0,1,2 | Scale factor (float, e.g. `0x3C23D70A` ≈ 0.01) |
| `0x002c` | 0 | Unit ID for channel 0 (SOC → `0x0011` = `%`) |
| `0x002c` | 1 | Unit ID for channel 1 (voltage → `0x00fc` = `V`) |
| `0x002c` | 2 | Unit ID for channel 2 (current → `0x00e7` = `A`) |
| `0x0128` / `0x012c` | 1 | Channel label ID (→ `0x0029` = `"Volt"`) |
| `0x0228` / `0x022c` | 2 | Channel label ID (→ `0x002a` = `"Curr"`) |

### 7.3 Tab 2 — History

**Query** (`E=1`, `Tab=10`):
```
kind   = 0x640 | IDAL               # e.g. 0x653 for battery
data   = [index_lo, sub_index, 0x00]
```

**Response** (`E=0`, `Tab=10`):
```
kind   = 0x240 | IDAL               # e.g. 0x253 for battery
data   = [index_lo, sub_index, 0x00, flags, value_lo, value_hi]
         # or 8 bytes for float values
```

Observed battery history items:

| Index | Description |
|-------|-------------|
| `0x0003` | Historical value slot 0 |
| `0x0007` | Historical value slot 1 (with float, e.g. `0x40A00000` = 5.0) |

### 7.4 Tab 3 — Configuration

**Query** (`E=1`, `Tab=11`):
```
kind   = 0x660 | IDAL               # e.g. 0x673 for battery, 0x678 for DC-DC
data   = [index_lo, sub_index, 0x00]
```

**Response** (`E=0`, `Tab=11`):
```
kind   = 0x260 | IDAL               # e.g. 0x278 for DC-DC
data   = [index_lo, sub_index, 0x00, flags, ...]
```

DC-DC channel enumeration uses Tab 3 config queries. The DC-DC responds with a burst of `0x278` frames enumerating its measurement channels (e.g. input voltage, output voltage, output current).

Observed battery config items: index `0x0002` → `4` (meaning unknown); indices
`0x0000` and `0x0001` are invalid (see below).

#### Invalid-Index / NAK Responses (Tab 1–3)

Querying an index a device doesn't implement does not time out silently — it
gets an immediate reply on a different kind. A normal response clears the
query kind's bit 10 (subtract `0x400`, e.g. `0x653` → `0x253`). An invalid
index instead clears only bit 9 (subtract `0x200`, e.g. `0x653` → `0x453`,
`0x673` → `0x473`) and returns a short 4-byte payload — `[index_lo, sub_index,
0x00, 0x00]` — with no value field. Confirmed live (July 2026) on Tab 2 index
`0x0000` and Tab 3 indices `0x0000`/`0x0001` for the battery, and Tab 3
indices `0x0000`/`0x0001` for the DC-DC too.

DC-DC channel enumeration is **not** a Tab 3 query response — it is
unsolicited, sent when the DC-DC detects a newcomer joining (§5.2). Querying
Tab 3 indices on a DC-DC directly does not trigger it; forcing an immediate
re-announcement while watching the DC-DC is the way to reproduce it on
demand.

### 7.5 String Label Protocol

String labels (human-readable names and units) are queried using the ping mechanism with a special `0x30` prefix:

**Query** (embedded in ping-family kind, e.g. `0x1d8` for IDAL=`0x18`):
```
kind   = 0x1C0 | IDAL
data   = [0x30, index_lo, 0x00, 0x00]
```

**Response** (kind + `0x40`):
```
kind   = 0x180 | IDAL
data   = [0x30, index_lo, 0x00, flags, char0, char1, char2, char3]
         # text is ASCII, up to 4 bytes per frame (may be continued across frames ⚠️)
```

Known string label indices (from battery):

| Index | String | Meaning |
|-------|--------|---------|
| `0x0011` | `%` | Percent unit |
| `0x0029` | `Volt` | Voltage channel name |
| `0x002a` | `Curr` | Current channel name |
| `0x0054` | ` cha…` | Charger state label (truncated) |
| `0x00e7` | `A` | Amps unit |
| `0x00fc` | `V` | Volts unit |
| `0x0022` | `0310` | Display firmware version string |
| `0x0002` | `12 2…` | DC-DC product name (e.g. `"12 25"` = 12V/25A) |

---

## 8. Channel Enumeration (DC-DC and Switch devices)

After a new device joins, IDAL=`0x18` devices (DC-DC, switch) send a burst of channel enumeration frames to declare their measurement structure.

```
# Per-channel frame
kind   = 0x278    # Tab 3 response, IDAL=0x18
data   = [0x02, channel_index_lo, channel_index_hi, channel_index_lo, total_channels, 0x00]

# Summary frame
kind   = 0x298
data   = [0x03, 0x00, 0x00, total_items, total_items, 0x00]

# Finalise
kind   = 0x698
data   = [0x03, 0x00, 0x00, first_data_index]

# Complete
kind   = 0x098
data   = [total_items, 0x00, 0x00, 0x00, 0x00, 0x00]
```

Observed channel counts:
- **Single-gang switch** (`0x1605c`): 4 items (state + electrical parameters for one pole)
- **DC-DC converter** (`0x100a9`): appears to have 2 sides × N parameters ⚠️ (enumeration was incomplete in the capture)

The "finalise" kind (`0x698`, IDAL `0x18`) also has a counterpart for the
battery: kind `0x693` (IDAL `0x13`) with the same `[0x03, 0x00, 0x00,
first_data_index]` shape, observed once after a Tab 3 query. It appeared in
isolation, without an accompanying `0x278`-style burst or `0x298` summary —
consistent with the battery not being a multi-channel device to enumerate,
but confirming the query itself isn't DC-DC/switch-specific.

---

## 9. Complete Message Kind Reference

| `kind` | Binary | E | MT | Tab | IDAL | Description |
|--------|--------|---|----|-----|------|-------------|
| `0x098` | `0010011000` | 0 | 01 | 00 | `0x18` | Init / reset (DC-DC) |
| `0x10e` | `0100001110` | 0 | 10 | 00 | `0x0e` | Announcement → newcomer (switch) |
| `0x113` | `0100010011` | 0 | 10 | 00 | `0x13` | Announcement → newcomer (battery) |
| `0x114` | `0100010100` | 0 | 10 | 00 | `0x14` | Announcement → newcomer (display) |
| `0x118` | `0100011000` | 0 | 10 | 00 | `0x18` | Announcement → newcomer (DC-DC) |
| `0x154` | `0101010100` | 0 | 10 | 10 | `0x14` | Presence beacon, empty payload (display) |
| `0x18e` | `0110001110` | 0 | 11 | 00 | `0x0e` | Ping response (switch) |
| `0x193` | `0110010011` | 0 | 11 | 00 | `0x13` | Ping response (battery) |
| `0x194` | `0110010100` | 0 | 11 | 00 | `0x14` | Ping response (display) |
| `0x198` | `0110011000` | 0 | 11 | 00 | `0x18` | Ping response (DC-DC/switch) |
| `0x1ae` | `0110101110` | 0 | 11 | 01 | `0x0e` | Query response (switch) |
| `0x1ce` | `0111001110` | 0 | 11 | 10 | `0x0e` | Ping / item query (switch) |
| `0x1d3` | `0111010011` | 0 | 11 | 10 | `0x13` | Ping / item query (battery) |
| `0x1d4` | `0111010100` | 0 | 11 | 10 | `0x14` | Ping / item query (display) |
| `0x1d8` | `0111011000` | 0 | 11 | 10 | `0x18` | Ping / item query (DC-DC) |
| `0x213` | `1000010011` | 1 | 00 | 00 | `0x13` | Tab 0 monitoring data — float (battery) |
| `0x233` | `1000110011` | 1 | 00 | 01 | `0x13` | Tab 1 alarm/label data (battery) |
| `0x253` | `1001010011` | 1 | 00 | 10 | `0x13` | Tab 2 history data (battery) |
| `0x278` | `1001111000` | 1 | 00 | 11 | `0x18` | Tab 3 channel enum response (DC-DC) |
| `0x298` | `1010011000` | 1 | 01 | 00 | `0x18` | Channel enum summary |
| `0x613` | `1100010011` | 1 | 10 | 00 | `0x13` | Tab 0 query — monitoring index (battery) |
| `0x633` | `1100110011` | 1 | 10 | 01 | `0x13` | Tab 1 query — label index (battery) |
| `0x653` | `1101010011` | 1 | 10 | 10 | `0x13` | Tab 2 query — history index (battery) |
| `0x673` | `1101110011` | 1 | 10 | 11 | `0x13` | Tab 3 query — config index (battery) |
| `0x678` | `1101111000` | 1 | 10 | 11 | `0x18` | Tab 3 query — config/channel (DC-DC) |
| `0x693` | `1101010011` | 1 | 11 | 00 | `0x13` | Channel enum query (battery) |
| `0x698` | `1101011000` | 1 | 11 | 00 | `0x18` | Channel enum finalise (DC-DC) |

---

## 10. IDB / IDAL Assignment

- **IDAL** is a fixed device-class code, the same for all units of the same product type
- **IDB** is a unique 18-bit address assigned at manufacture; the generation algorithm is proprietary and not publicly derivable from article/serial numbers
- **Both are self-reported on the bus** — no pre-configuration needed. Listen for `0x11X` announcement frames and extract IDB from `data[1:4]` LE and IDAL from `data[0]`

The Mastervolt Modbus bridge interface uses these same IDB/IDAL values. They can also be read from any device using MasterAdjust software via the USB gateway.

---

## 11. Implementation Notes

### Joining the Bus

1. Choose an IDB that does not conflict with any observed device (listen first)
2. Choose an IDAL appropriate to your device class, or use an unused value
3. Send presence beacon then self-announcement (§5.2)
4. Process incoming `0x11X` frames to build a device table

### Querying a Device

```python
def make_can_id(E, MT, tab, idal, idb):
    kind = (E << 9) | (MT << 7) | (tab << 5) | idal
    return (kind << 18) | idb

# Poll battery SOC (Tab 0, index 0)
can_id = make_can_id(E=1, MT=0b10, tab=0, idal=0x13, idb=0x2c944)
# kind = 0x613, CAN_ID = 0x184ec944
payload = bytes([0x00, 0x00])   # index 0 LE

# Parse response (kind=0x213)
def parse_tab0_response(data):
    import struct
    index = struct.unpack_from('<H', data, 0)[0]
    value = struct.unpack_from('<f', data, 2)[0]
    return index, value
```

### Receiving Unsolicited Telemetry

The battery broadcasts Tab 0 data (kind `0x213`) every 1–2 seconds without being asked. Simply listen for frames with `kind == 0x213` and `IDB == 0x2c944`.

### Controlling the Switch Output

Tab 3 (config) writes are inferred to control device outputs ⚠️. Send a Tab 3 query frame with the appropriate index and a value payload. The exact index for switch state is not yet confirmed from the capture — use MasterAdjust to identify it, or observe the bus while toggling the switch manually via the display.

### Response Timing

From the capture, devices respond within one CAN frame time. A timeout of 100 ms per query is conservative. Retry twice before declaring a device absent.

---

## 12. Open Questions / Items to Validate

| # | Question |
|---|----------|
| 1 | Which serial (M803E0012 vs M820E0041) corresponds to IDB `0x0788c` vs `0x100a9`? Power-cycle one DC-DC to confirm. |
| 2 | Exact Tab 3 index for switch on/off control |
| 3 | Tab 0 monitoring items for DC-DC converters (input V, output V, output A) |
| 4 | Full string label table for DC-DC converters |
| 5 | Whether `0x2ff8d` (USB gateway) participates in any control flows when USB is connected |
| 6 | Confirm presence beacon kind `0x154` is required vs optional for bus participation |
| 7 | Whether writes to Tab 3 require a specific acknowledge/handshake beyond the response frame |
| 8 | IDAL value for charger devices (documented as `0x0a` in HSYCO integration but not observed here) |
| 9 | Maximum CAN payload size — all observed frames are ≤ 8 bytes (standard CAN DLC limit) |
| 10 | Whether multi-frame string labels longer than 4 chars use a continuation mechanism |

---

## 13. References

- Mastervolt MasterBus Modbus Interface manual (product 77030800)
- HSYCO MasterBus I/O Server documentation: https://docs.hsyco.com/docs/io-servers/masterbus/
- MasterBus Wireshark dissector (pmarches): https://github.com/pmarches/masterbus-esp32/blob/master/masterbusDissector.lua
- Mastervolt IDB/IDAL calculator: https://www.mastervolt.com/modbus.php
