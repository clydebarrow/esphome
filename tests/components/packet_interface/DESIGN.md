# Packet interface design

This document describes where the `packet_interface` component is going. The link layers (`uart`, `udp`), the
addressing, encryption, reliability and session layers, the `entity_transport` application and the `packet_transport`
bridge exist today. The fragmentation layer is planned.

## Goals

- Split packet handling into layers. Each layer does one job.
- Let any link layer use any of the other layers, without the link layer knowing about them.
- Let the user configure the layers they want on one `packet_interface` entry, without knowing the order in which
  they must be stacked.
- Give an application, such as `packet_transport`, one fully configured interface to use.
- Pass data between layers without copying it where possible, using `PacketBuffer`.

## Layers

From the bottom up:

| Layer         | Job                                                                       |
|---------------|---------------------------------------------------------------------------|
| Link          | Moves packets over a real channel: UART, UDP, and later radio links.      |
| Addressing    | Adds source and destination addresses; drops packets not addressed to us. |
| Encryption    | Encrypts and authenticates packets with a shared key.                     |
| Reliability   | Acknowledges packets and resends those not acknowledged in time.          |
| Fragmentation | Splits packets larger than the layers below allow, and joins them again.  |
| Session       | Sends keep-alive packets and reports whether the peer can be reached.     |
| Application   | Uses the packets, for example `packet_transport`. Not part of the stack.  |

Every layer except the application is a `PacketInterface`. A layer above the link layer holds a pointer to the layer
below it and registers itself as that layer's listener. To the layer above, it looks like any other
`PacketInterface`.

### Layer order

The order is fixed by the code, not by the user. The proposed order and the reasons for it:

- **Addressing below encryption.** A device can drop packets meant for another device on a shared bus (such as
  RS485) without spending time decrypting them.
- **Encryption below reliability.** Acknowledgements and link resets are encrypted too, so they cannot be forged or
  replayed.
- **Reliability below fragmentation.** Each fragment is acknowledged and resent on its own, so a lost fragment
  costs one fragment, not the whole packet. Fragments also arrive in order with none missing, so joining them needs
  no timers or reordering. The reliability queue holds fragments, so its slots stay the size of a link packet.
- **Fragmentation below session.** The session layer stays at the top of the stack, where an application finds it
  with `top()`, and adds its header once per packet rather than once per fragment.
- **Reliability below session.** Keep-alive packets reach the peer reliably, so a single lost packet does not mark
  the session as down.

Only the layers that are configured are created. A link layer with no other layers configured is used directly, as
it is today.

### Implementations

Each type of layer may have more than one implementation, for example a choice of encryption algorithms or of
addressing modes. The key for a layer takes a `type` option that picks the implementation, with a default, using
`cv.typed_schema()`. Implementations of the same layer type are interchangeable to the layers above and below them.

A link layer may also do the job of another layer itself. For example, a UDP link layer already has addresses. Such
a link platform declares which layer types it provides, and the user cannot configure a separate layer of that type
on it.

### Implementation order

1. Reliability.
2. Session.
3. Encryption.
4. Fragmentation.
5. Addressing, done before fragmentation, for multi-drop serial buses and shared radio channels such as LoRa.

## Example configuration

```yaml
packet_interface:
  - platform: uart # RS485 multidrop
    id: rs485_comms
    encryption:
      type: xxtea
      key: Johnny Cash
    reliability:
      timeout: 0.2s # Retransmit if no ack within 200ms
      max_queue_length: 4 # Buffer up to 4 packets
    fragmentation:
      max_packet_size: 1024 # Largest packet the application can send
    session:
      keep_alive: 11s
      binary_sensor: # Sensor ON when connected
        id: session_alive
        name: Channel connected
    addressing:
      address: 0x1234 # We only receive packets addressed to us
      destination: 0xDEAD # The one peer; reliability and session need a single peer

entity_transport:
  - packet_interface: rs485_comms
    sensors:
      - temperature
```

The keys for the layers can be given in any order. The application refers to `rs485_comms` and gets the top of the
stack.

## Data flow

### Sending

Each layer wraps the buffer from the layer above in a new `PacketBuffer`: its header as the pre-buffer, the upper
buffer as the mid-buffer, and any trailer (such as a checksum or authentication tag) as the post-buffer. It then calls
`send_to_interface()` on the layer below. The headers live on the stack for the length of the call, so no heap is
used and the payload is not copied.

Two layers must copy:

- **Encryption** changes every byte, so it writes the encrypted packet into a buffer it owns.
- **Reliability** must keep each unacknowledged packet until it is acknowledged or given up, so it copies the packet
  into one of `max_queue_length` slots.

These buffers are allocated once in `setup()`, sized from `max_packet_size`.

### Receiving

The link layer calls `on_receive_from_interface_()` with a `PacketBuffer` over its own receive buffer. Each layer
checks and removes its header and trailer, and passes a `PacketBuffer` over the rest to its listener. Encryption
decrypts into its own buffer first.

### Packet size

Each layer's `max_packet_size` is the size of the layer below minus its own header and trailer. The application
sees the space left for its payload after every layer has taken its share.

### Metadata

`PacketMetaData` travels with each packet. Layers read it on the way down and fill it in on the way up. For example,
the addressing layer would take the destination address from it when sending, and report the source address when
receiving. `PacketMetaData` will need fields for this.

## Reliability layer: `hdlc`

The first reliability implementation follows HDLC asynchronous balanced mode, with sequence numbers modulo 8 and
go-back-N retransmission. It works between two devices only.

### Frame format

```
+---------+-----------------+----------------+
| control | payload         | CRC-16 (LE)    |
| 1 byte  | 0 or more bytes | 2 bytes        |
+---------+-----------------+----------------+
```

The CRC is CRC-16/MODBUS (`crc16()` in `helpers.h`) over the control byte and the payload. A frame with a bad CRC
is dropped; the sender's timeout or a reject recovers it. The CRC is here rather than in the UART link layer so that
the UART framing stays compatible with the UART packet transport.

Control byte, using the HDLC encoding (`P/F`, bit 4, is sent as 0 and ignored):

| Frame | Bits 7-5 | Bit 4 | Bits 3-0         | Use                                     |
|-------|----------|-------|------------------|-----------------------------------------|
| I     | N(R)     | P/F   | N(S) in 3-1, 0   | Payload, sequence number, piggyback ack |
| RR    | N(R)     | P/F   | `0001`           | Acknowledge frames before N(R)          |
| REJ   | N(R)     | P/F   | `1001`           | Resend from N(R)                        |
| SABM  | `001`    | P     | `1111`           | Reset both peers' sequence numbers      |
| UA    | `011`    | F     | `0011`           | Answer to SABM                          |

### Behavior

- **Link reset.** At start up, and after `max_retries` timeouts in a row, the layer discards unacknowledged frames
  and sends SABM, repeated every second (or every `timeout`, if longer) until UA arrives. A device that receives
  SABM resets its sequence numbers, answers UA and considers the link up, so two devices that start at the same time
  both connect.
- **Sending.** Each packet is copied into a free queue slot, numbered, and sent at once if the link is up. While the
  link is being reset, packets wait in the queue. When the queue is full, `send_to_interface()` returns false.
- **Acknowledging.** Every I-frame carries N(R), the next sequence number expected from the peer. When a frame
  arrives in sequence, an RR is sent from the next `loop()`, unless the layer above sends a reply first, which
  carries the acknowledgement instead.
- **Out of sequence frames.** The first one after a frame arrives in sequence triggers a REJ, asking the peer to
  resend from N(R). Later ones, including duplicates, trigger an RR, so the peer always gets an answer.
- **Timeout.** If the oldest unacknowledged frame is not acknowledged within `timeout`, all unacknowledged frames are
  resent.

### Options

| Option             | Default | Range       |
|--------------------|---------|-------------|
| `timeout`          | 200ms   |             |
| `max_queue_length` | 4       | 1 to 7      |
| `max_retries`      | 5       | 1 to 255    |

The queue uses `max_queue_length` times `max_packet_size` bytes, allocated in `setup()`.

## Session layer: `basic`

The first session implementation tells whether the peer can be reached, and notices when it restarts. Like the
reliability layer, it works between two devices only.

### Packet format

```
+------+-------------------+
| type | data              |
| 1    | 0 or more bytes   |
+------+-------------------+
```

| Type   | Packet     | Data                                                  |
|--------|------------|-------------------------------------------------------|
| `0x00` | Data       | The packet from the layer above                       |
| `0x01` | Keep-alive | A 32-bit random number chosen at boot, little endian  |

Other types are dropped, so later versions can add their own.

### Behavior

- **Keep-alive.** A keep-alive is sent at setup and every `keep_alive` interval after that. With a reliability layer
  below, the first one waits in its queue until the link is up.
- **Connected.** Any data or keep-alive packet received marks the peer as connected.
- **Lost.** The peer is lost when nothing has been received from it for `timeout`. This is checked every
  `keep_alive` interval, so it is noticed between `timeout` and `timeout` plus `keep_alive` after the last packet.
- **Restart.** A keep-alive with a different random number from the one last seen means the peer has restarted. This
  is reported as lost then connected, so an application can send its state again. A device answers a keep-alive with
  a number it has not seen with its own keep-alive at once, so both devices learn of each other without waiting for
  the next interval. The answer cannot repeat, because the other device has already seen that number.

An application can read `is_connected()` and register a callback with `add_on_state_callback()`. This is the link up
event that `entity_transport` needs.

### Options

| Option          | Default            | Range                          |
|-----------------|--------------------|--------------------------------|
| `keep_alive`    | 10s                | 1s or more                     |
| `timeout`       | 3 times keep_alive | Longer than keep_alive         |
| `binary_sensor` |                    | ON while the peer is connected |

## Addressing layer: `basic`

The first addressing implementation is for channels shared by several devices: a multi-drop serial bus such as
RS485, or a shared radio channel such as LoRa.

### Packet format

```
+----------------------+------------------+-----------------+
| destination address  | source address   | payload         |
| 2 bytes, LE          | 2 bytes, LE      | 0 or more bytes |
+----------------------+------------------+-----------------+
```

Addresses are 16 bits. `0xFFFF` is broadcast, and `0x0000` is not used, so it can mean "not set" in
`PacketMetaData`. 16 bits leave room to choose addresses without a central list; one byte would save 2 bytes per
packet on a slow bus, and could be a second implementation.

### Behavior

- **Receiving.** A packet is dropped unless its destination is this device or broadcast. Packets from this device's
  own address are dropped too, since a half-duplex bus or a radio repeater can echo them. Addressing sits below
  encryption, so these are dropped without being decrypted.
- **Point to point.** With a `destination` other than broadcast, the layer sends only to that device and accepts
  packets only from it, broadcast or not. The reliability and session layers and the rolling code keep state for one
  peer, so configuration validation requires a `destination` when they are used with addressing.
- **Broadcast.** With the default `destination: broadcast`, every device hears every packet. This suits
  `entity_transport`, which tells senders apart by dictionary id.
- **Metadata.** Received packets report their source and destination addresses in `PacketMetaData`, which the layers
  above pass up. A destination address in the metadata of a packet being sent overrides the configured one; the
  reliability layer sends its frames without metadata, so this only works without it.
- **Capability.** The layer adds `CAPABILITY_ADDRESSING`.

The layer adds 4 bytes. It needs the received packet in one piece, which the link layers provide.

## Encryption layer: `xxtea`

The first encryption implementation uses XXTEA, already used by `packet_transport`, with a 256-bit key made from the
SHA-256 of the key text.

### Packet format

The packet from the layer above is padded with zeros and followed by a trailer, and the result is encrypted as one
XXTEA block. Only the encrypted block is sent.

```
+-----------------+---------+------------------------------------------------------------------+
| payload         | padding | trailer, 4 bytes, or 16 with the rolling code                    |
| 0 or more bytes | 0 to 4  | [boot (4) | counter (4) | echo (4)] | check (3) | padding (1)    |
+-----------------+---------+------------------------------------------------------------------+
```

- **Boot number, counter and echo.** Only with `rolling_code_enable`; see below.
- **Check.** 3 fixed bytes, different with and without the rolling code, so two devices set up differently drop
  each other's packets. XXTEA mixes the whole block, so if any byte of the encrypted packet changes, or it was
  encrypted with another key, the check and padding count are wrong on decryption, except by chance about once in
  a billion.
- **Padding count.** The number of zero bytes before the trailer, so that the whole is a multiple of 4 bytes and at
  least 8, the smallest block XXTEA can encrypt.

The layer adds 4 to 8 bytes, or 16 to 19 with the rolling code. Its `max_packet_size` is that of the layer below
rounded down to a multiple of 4, less the trailer. XXTEA works on 32-bit words in the byte order of the device,
which is little endian on every supported platform.

Without the rolling code, the same packet always encrypts to the same bytes, and a recorded packet can be sent
again by anyone and is accepted.

### Rolling code

With `rolling_code_enable`, which is off by default as in `packet_transport`, the trailer also holds:

- **Boot number.** Saved in flash and increased at every boot, before any packet is sent, so it costs one flash
  write per boot. A boot number is never used twice.
- **Counter.** Starts at 0 at every boot and increases with every packet. If it wraps, the boot number is increased.
- **Echo.** The highest boot number this device has heard from the peer, sent back to it.

A packet is delivered only if:

- its echo is this device's current boot number, so it was made after this device last started; and
- its boot number and counter are higher than those of the last packet accepted from the peer, so it is not a
  repeat of a packet from this boot.

The echo does the job of `packet_transport`'s ping and pong, without extra packets: the boot number is a fresh
challenge after every restart, and every packet answers it. The peer's boot number is learnt from any packet that
passes the check, even one dropped because its echo is wrong, so after a restart each device's first packets are
dropped until the other has heard from it. Above this layer, the reliability layer resends them.

If the peer's flash is erased, its boot number starts again from 1, and its packets are dropped until this device
restarts. Packets that arrive out of order are also dropped.

## Fragmentation layer

Planned. A link limits the packet size: a UART link by its buffers, ESP-NOW to 250 bytes, LoRa to 255 bytes. The
fragmentation layer lets an application send packets up to its own `max_packet_size`, larger than the layers below
allow, by splitting each into fragments that fit and joining them again at the other end.

- **Header.** Each fragment carries a short header: an 8-bit packet number, the fragment's index within the packet,
  and a flag on the last fragment. A packet that fits in one fragment still has the header.
- **Joining.** The receiver copies fragments into one buffer of `max_packet_size`, allocated in `setup()`. When the
  packet number changes or an index is skipped, the incomplete packet is dropped. Above a reliability layer this
  never happens. Without one, for example on UDP, a lost fragment loses the packet, and fragments that arrive out of
  order are not handled.
- **Sending.** Each fragment is copied from the upper packet with `copy_to()` into a buffer on the stack, so the
  upper packet is not held after `send_to_interface()` returns.
- **Partial sends.** If the layer below refuses a fragment part way through a packet, for example because the
  reliability queue is full, the peer receives an incomplete packet and drops it. A packet must only be started when
  the layer below can take all of its fragments; see the open questions.

## Applications

`packet_transport` keeps its own encryption, rolling code and ping-pong features, and is used through the
`packet_interface` transport platform.

`entity_transport` replaces it for new configurations. It sends sensor and binary sensor states like
`packet_transport`, but leaves encryption, replay protection, reliable delivery and the peer's status to the layers.

### Dictionary

Each device numbers its sensors and its binary sensors from 0. A dictionary packet gives the device name and the id
for each number, and data packets then refer to entities by number: a sensor record is 5 bytes, a binary sensor record
2, with no names. Records for both types share a data packet, sensors first, with a count of sensor records in the
header so a receiver can skip numbers it does not use.

Every packet starts with a type byte, whose high nibble is the format version, and a 32-bit **dictionary id**: a hash
of the device name and every entity's type and id. Per packet, this is the only metadata:

- It tells a receiver which device sent a data packet, so several devices can share a broadcast channel without
  their names in every packet. Once the addressing layer exists, its source address could do this instead.
- It changes whenever the device's entities change, so a receiver never applies numbers from an old dictionary.
- With session and reliability layers it is not strictly needed, but 4 bytes is little, and keeping one packet format
  is simpler than two.

A receiver keeps, for each remote entity, the dictionary id and number it learnt, not whole dictionaries. A data packet
with a dictionary id the receiver does not know, while some of its remote entities are not yet learnt, makes it send a
request, at most every 5 seconds; the device with that dictionary id answers with its dictionary and all states.

### When states are sent

- On change, from `loop()`.
- The dictionary and all states at boot, when the session layer reports the peer connected, when requested, and every
  `update_interval`.
- `update_interval` defaults to `never` when the interface has both session and reliability capabilities, since the
  dictionary and states are then sent on every connect and arrive in order. Otherwise it defaults to 15 seconds.
- A packet the interface refuses is sent again after a second, with the latest states. Data waits for the dictionary.

When the session layer reports the peer lost, the remote entities lose their state.

### Memory

Local entities are a `FixedVector`, and remote entities are `StaticVector`s sized by `cg.slot_counter`. Packets are
built in one buffer allocated in `setup()`. Nothing is allocated after setup.

## Capabilities

Each layer adds a bit to the capabilities of the stack: `CAPABILITY_ENCRYPTION`, `CAPABILITY_RELIABILITY` or
`CAPABILITY_SESSION`. A layer copies the bits of the layer below in `stack_on_()` and adds its own, so the top layer
reports the whole stack. A link layer that does a layer's job itself, such as one with built-in encryption, would set
the bit in its own constructor. An application asks `top()->has_capability()` and adapts: `entity_transport` resends
periodically unless both session and reliability are present.

The session state callback is in `PacketInterface`, so an application needs no session class. The session layer is
always the top layer, so an application registers it on `top()`.

## Code generation

The layer schemas are defined once in `esphome/components/packet_interface/__init__.py` and added to every link
platform by `packet_interface_schema()`. A link platform only adds its own options.

`new_packet_interface()` creates the link layer, then creates each configured layer in the fixed order, passing each
one the layer below to its constructor. Each layer is registered as a component.

The ID in the configuration is declared with the link layer's class, but an application needs the top of the stack.
Each layer's constructor records itself as the layer above the one it is given, and `PacketInterface::top()` walks up
from the link layer to the top. Applications call `top()` in `setup()`, so the order in which code generation creates
the objects does not matter.

## Open questions

- **Wire format.** The header layout of each layer, and whether a link layer needs a byte that says which layers are
  in use, so that badly matched configurations are detected rather than producing garbage.
- **Best effort packets.** HDLC unnumbered information (UI, control byte `0x03`) frames could carry data that is
  only useful while fresh, such as streamed readings, without a sequence number, queue slot or retry. A flag in
  `PacketMetaData` would select them. A UI frame must not be sent while I-frames are unacknowledged, or it could
  arrive before older data that is still being resent.
- **Ciphers.** Which ciphers are offered besides XXTEA, and how much each adds to the packet size.
- **Reliability with several peers.** How acknowledgements and the send queue work when there is more than one
  destination address. Until then, a device can use the reliability and session layers with one other device on a
  shared channel. One link layer instance per bus also means one stack per bus, so several point-to-point stacks on
  one UART would need the link layer to pass each packet to several stacks.
- **Room to send.** The fragmentation layer needs to know, before it starts a packet, that the layer below will take
  all of its fragments. A `PacketInterface` method that reports how many packets can be sent now would do this. The
  reliability queue must also be at least as long as the number of fragments in the largest packet.
- **Fragment header size.** Whether the index needs more than 7 bits, which limits a packet to 128 fragments.
- **Session sensor.** With several peers, whether there is one sensor per peer or one for the whole channel.
