# Packet interface design

This document describes the `packet_interface` component. The link layers (`uart`, `udp`, `tcp`, `espnow`,
`sx126x`, `sx127x`), the addressing, encryption, reliability, fragmentation and session layers, the `entity_transport`
application and the `packet_transport` bridge exist today.

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
| Link          | Moves packets over a real channel: UART, UDP, TCP, ESP-NOW or LoRa.       |
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

A link layer may also do the job of another layer itself. The link platform passes the keys of those layers as
`provides` to `packet_interface_schema()`, which leaves them out of its schema, and sets their capability bits in its
constructor. The `tcp` link provides reliability, addressing and session.

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

These buffers are allocated once in `setup()`, sized from `max_packet_size`. A link is given its buffer by code
generation as a `std::span`, so the buffer's size is the link's `max_packet_size` and the two cannot differ.

### Receiving

The link layer calls `on_receive_from_interface_()` with a `PacketBuffer` over its own receive buffer. Each layer
checks and removes its header and trailer, and passes a `PacketBuffer` over the rest to its listener. Encryption
decrypts into its own buffer first.

### Packet size

`max_packet_size` is fixed when a layer is constructed: a link takes it from the buffer it is given, and a layer
above takes the size of the layer below minus its own header and trailer. The application sees the space left for
its payload after every layer has taken its share.

### Metadata

`PacketMetaData` travels with each packet. Layers read it on the way down and fill it in on the way up. The
addressing layer reports `source_address` and `destination_address` when receiving; when sending, a
`destination_address` of 0 uses the configured destination. Radio links fill in `rssi` and `snr`, and the ESP-NOW
link the sender's `mac_address`.

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
- **Data lost on a reset.** A reset of a connected link, after `max_retries` or on SABM from a peer that restarted,
  can lose data both ways. The layer calls its session state callback with false, which the layers above pass on. The
  session layer reports the session lost, and connected again when the peer is next heard, so `entity_transport`
  sends its dictionary and all states again, as after a restart. A Wi-Fi test found this: a peer joining its network
  moved its radio between channels and missed ESP-NOW frames for about a second, the sender reset the link, and two
  values were lost for good, because the session stayed up and no periodic resend was configured.
- **Sending.** Each packet is copied into a free queue slot, numbered, and sent at once if the link is up and no
  earlier frame is waiting. While the link is being reset, packets wait in the queue. When the queue is full,
  `send_to_interface()` returns false.
- **Pacing.** At most one I-frame is sent per loop pass, and the loop is requested again for the next. On a radio,
  sending blocks while the frame is on air, about 400 ms for a 250 byte LoRa frame at SF7; sending a whole window at
  once blocked the loop for seconds, during which the peer's frames and acknowledgements were missed and both
  devices timed out and resent, over and over. Between frames, the radio's own loop now receives the peer's frames.
- **Refused frames.** A pointer V(N) marks the next frame to send, between V(A) and V(S). When the layer below
  refuses a frame, for example while a radio is receiving, that frame and the rest are sent again 50 ms later, with
  no need for the retransmit timeout; a refused RR is retried the same way. A timeout or REJ moves V(N) back to V(A).
- **Acknowledging.** Every I-frame carries N(R), the next sequence number expected from the peer. When a frame
  arrives in sequence, an RR is sent from the next `loop()`, or after `ack_delay` if set, unless an I-frame is sent
  first, which carries the acknowledgement instead. On a half-duplex link, the delay keeps the RR from colliding with
  a second frame the peer sends straight after the first.
- **Out of sequence frames.** The first one after a frame arrives in sequence triggers a REJ, asking the peer to
  resend from N(R). Later ones, including duplicates, trigger an RR, so the peer always gets an answer.
- **Timeout.** If the oldest unacknowledged frame is not acknowledged within `timeout`, plus up to a quarter at
  random, all unacknowledged frames are resent.

### Options

| Option             | Default | Range               |
|--------------------|---------|---------------------|
| `timeout`          | 200ms   |                     |
| `ack_delay`        | 0ms     | Less than `timeout` |
| `max_queue_length` | 4       | 1 to 7              |
| `max_retries`      | 5       | 1 to 255            |

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

An application can read `is_connected()`, or register a callback with `add_on_session_state_callback()` on `top()`. This is the link up
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
- **Ports.** Above `BasicAddressing`, one per link, are one or more `AddressingPort`s, each the bottom of its own
  stack of layers. A broadcast goes to the broadcast port; any other packet goes to the port whose destination is
  its source address, and if there is none, to the broadcast port. Without a broadcast port, a broadcast goes to the
  source's port, and a packet with no port to go to is dropped. A broadcast from a device with a port of its own
  cannot go to that port when there is a broadcast port, as that port's layers (reliability, encryption) expect only
  its point-to-point packets, so a hub can send states that every device needs once, on its broadcast port.
- **Point to point.** A port with a destination other than broadcast sends only to that device and receives only its
  packets. The reliability and session layers and the rolling code keep state for one peer, so
  configuration validation requires a destination for them.
- **Broadcast.** A port with `destination: broadcast` sends to every device. This suits `entity_transport`, which
  tells senders apart by dictionary id.
- **Metadata.** Received packets report their source and destination addresses in `PacketMetaData`, which the layers
  above pass up. A destination address in the metadata of a packet being sent overrides the configured one; the
  reliability layer sends its frames without metadata, so this only works without it.
- **Capability.** The layer adds `CAPABILITY_ADDRESSING`.

The layer adds 4 bytes. It needs the received packet in one piece, which the link layers provide.

### Several peers

```yaml
packet_interface:
  - platform: uart
    id: rs485
    addressing:
      address: 0x0001
      peers:
        - id: pump_link
          destination: 0x0002
          encryption: !secret pump_key
          reliability:
          session:
        - id: valve_link
          destination: 0x0003
          reliability:
        - id: bus_link
          destination: broadcast
```

- **Forms.** With `destination` under `addressing`, there is one port, and the layers are configured on the link as
  usual; the port is the link's top, so applications use the link's id. With `peers`, each peer has its own id,
  destination and layers, the layers cannot be configured on the link, and applications must use a peer's id. Final
  validation of `entity_transport` and the `packet_transport` bridge rejects the link's id.
- **Names.** Each peer's port is named after its id, and the layers above take that name, so each stack has its own
  log prefix and its own encryption boot number preference.
- **Stacking.** A port with siblings takes the link's name and capabilities through `inherit_from_()`, which unlike
  `stack_on_()` does not make it the link's top. `top()` on the link then returns the link itself.
- **Collisions.** Devices that send whenever they like can collide on a half-duplex bus. The reliability layer
  resends, and its retransmit timeout has up to a quarter added at random, so devices whose frames collided do not
  resend at the same moment again. A bus that needs polling or arbitration is a separate design.
- **Cost.** Each reliable peer has its own queue and buffers, about 3 KB at 508-byte packets. Each peer's
  acknowledgements and keep-alives use channel time, which matters on LoRa with its duty cycle limits. States that
  every device needs are cheaper sent once to a broadcast port than once per reliable peer.

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

## Fragmentation layer: `basic`

A link limits the packet size: a UART link by its buffers, ESP-NOW to 250 bytes, LoRa to 255 bytes. The
fragmentation layer lets an application send packets up to the layer's own `max_packet_size`, default 1024, by
splitting each into fragments that fit the layer below and joining them again at the other end.

- **Header.** 2 bytes per fragment: an 8-bit packet number, then the fragment's index in bits 0-6 with bit 7 set on
  the last fragment, so a packet has at most 128 fragments; setup fails if `max_packet_size` needs more. Packet
  numbers start at a random value at boot. A packet that fits in one fragment still has the header.
- **Sending.** A packet in one fragment is sent with no copy, the header as the pre-buffer and the packet as the
  mid-buffer. A larger one is copied fragment by fragment, with `copy_to()` at an offset, into one buffer of a
  fragment's size allocated in `setup()`; the layer below copies or sends each fragment before the next is made.
- **Room to send.** `PacketInterface::get_send_capacity()` reports how many packets a layer accepts now: unlimited by
  default, the free queue slots in the reliability layer. A packet is started only if all of its fragments fit, so a
  full queue never leaves the peer with part of a packet. This works because fragmentation is directly above
  reliability; another layer in between would have to pass the capacity on. `dump_config` warns when the largest
  packet needs more fragments than the queue holds.
- **Joining.** Fragments are copied into one buffer of `max_packet_size`, allocated in `setup()`; a packet in one
  fragment is passed up with no copy. A fragment with a gap in the index or another packet number drops the packet
  being joined, as does the first fragment of a new packet. Other fragments from another source address in
  `PacketMetaData` are dropped alone, and a packet in one fragment from another source is passed up, both leaving
  the packet being joined intact. Above a reliability layer none of this happens; without one, a lost fragment loses
  the packet, and fragments out of order are not handled. So of two devices on a broadcast port sending fragmented
  packets at once, the one that started last gets through; without an addressing layer, the receiver can drop both.
  The two buffers are one block from `RAMAllocator`; if it cannot be allocated, the layer is marked failed.
- **Capability.** The layer adds `CAPABILITY_FRAGMENTATION`.

## Link: `tcp`

A TCP connection between two devices, as a client that connects to a host, or a server that accepts one client at a
time, built on the socket component's `TcpClientLink` and `TcpListener`, as `tcp_uart` is.

- **Framing.** Each packet is sent with its length, 2 bytes little endian, in front. A length larger than
  `max_packet_size` cannot be resynchronised, so the connection is closed and made again.
- **Provided layers.** TCP resends what is lost and delivers in order, and a connection joins exactly two devices, so
  the link provides reliability, addressing and session. Its connection state is the session state: connected when
  the connection comes up, lost when it goes down. A peer that restarts is seen as a new connection.
- **Keep-alive.** An empty packet is sent when nothing else has been sent for `keep_alive`; the connection is closed
  and made again when nothing has been received for three intervals. TCP alone can take minutes to notice a peer that
  lost power. In testing, a server noticed a client reset within about 5 seconds anyway, as the client answered the
  server's next write with a reset once it was back on the network.
- **Sending.** Each packet is queued in the socket link's 1 KB buffer and written at once, so `max_packet_size` is at
  most 1022. The link keeps the default, unlimited, send capacity: a stream that flushes as it goes is not a bounded
  queue, and reporting how many full-size packets fit in the buffer stopped the fragmentation layer from ever starting
  a packet of two fragments.

## Link: `udp`

UDP packets through a `udp` component, which sends each packet to every address in its `addresses`.

- **Point to point.** When the `udp` component sends to one unicast IPv4 address, the link accepts packets only from
  that address and provides addressing. The `udp` component tells the link the sender of each packet it passes on.
  An address ending in `.255` is taken as a subnet broadcast, since the subnet is not known when the configuration
  is checked; such a link works, but without the filter.
- **Broadcast.** Otherwise every packet is accepted, and the addressing layer can be configured to tell devices apart.
- **No session.** UDP has no connection, so a session layer is needed to know whether the peer can be reached.

## Link: `espnow`

ESP-NOW packets through an `espnow` component, between ESP32 and ESP8266 devices on the same Wi-Fi channel.

- **Point to point.** With a `peer_address` other than broadcast, packets go to that device only and only packets it
  sends to this device are accepted, so the link provides addressing. The peer is added to the `espnow` component's
  peers.
- **Broadcast.** With the default broadcast `peer_address`, every broadcast packet is accepted, and the addressing
  layer can be configured to tell devices apart. The `espnow` component passes packets on only from known peers, so
  the link also listens for packets from unknown senders; with `auto_add_peer` the sender is added and the packet
  arrives as from a known peer.
- **Sharing.** A peer link listens only for packets sent to this device, and a broadcast link only for broadcasts, so
  both kinds can use one `espnow` component. ESP8266 does not report a packet's destination, so there a broadcast
  link also receives packets meant for a peer link.
- **Packet size.** `max_packet_size` defaults to 250 bytes, the ESP-NOW v1 limit, and may be raised up to the `espnow`
  component's `max_payload_size`.
- **Metadata.** The sender's MAC address, and on ESP32 the RSSI, are passed up with each packet.
- **Sending.** ESP-NOW queues each packet and reports delivery later; the link refuses a packet only when the queue
  is full or the peer cannot be added. Lost packets are left to a reliability layer.

## Links: `sx126x` and `sx127x`

LoRa or FSK radio packets through an `sx126x` or `sx127x` component.

- **Packets.** Each packet is one radio packet, so the radio marks where it starts and ends, and checks it with its
  CRC when `crc_enable` is set. The radio's `payload_length` must be 0, for packets of varying length. Packets are at
  most 255 bytes, or 64 on an SX127x without LoRa. Received packets carry the RSSI and SNR.
- **Shared channel.** Every device in range hears every packet, so the addressing layer is usually configured.
- **Sending.** A send waits until the radio has transmitted, which takes from a few milliseconds to over a second.
  Sends are refused until the radio has been set up, as the layers above are set up first.
- **Listen before talk.** A radio cannot hear while it sends, so a send is refused while the radio is receiving a
  packet, and the layers above send again shortly. `ListenBeforeTalk` limits this to 2 seconds, so a busy flag that
  never clears cannot stop sending for good. Testing on real radios is described under Open questions.

## Applications

`packet_transport` keeps its own encryption, rolling code and ping-pong features, and is used through the
`packet_interface` transport platform.

`entity_transport` replaces it for new configurations. It sends sensor and binary sensor states like
`packet_transport`, but leaves encryption, replay protection, reliable delivery and the peer's status to the layers.

### Dictionary

Each device numbers the entities of each type from 0, up to 255 of each. A dictionary packet gives the device name
and, in a section for each type, the id for each number. Data packets then refer to entities by type and number, with
no names.

A dictionary too large for one packet is sent in parts. Each part has a byte after the header: its index in bits 0-6,
counting from 0 and wrapping after 127, and bit 7 on the last part. A receiver counts a dictionary as received in full
only when its last part follows all the others in order. Before the part byte, the first part to arrive could mark the
dictionary as received, so a lost later part naming the receiver's entities was never asked for again, and with
`update_interval: never` never arrived.

### Data records

Each record in a data packet starts with a 16-bit tag, little endian:

| bits  | field                                                                                  |
|-------|----------------------------------------------------------------------------------------|
| 0-7   | the entity's number                                                                    |
| 8-12  | its type: 1 sensor, 2 binary sensor; 0 is not used, so up to 31 types                  |
| 13-14 | the payload: 0 none, 1 four bytes, 2 a length byte and that many bytes, 3 no state     |
| 15    | with payload 0, the state                                                              |

A binary sensor record is 2 bytes, with its state in bit 15; a sensor record is 6, with its float state, or 2 when it
has no state (NaN). Records of any type can be mixed in one packet, in any order. The payload field gives each
record's size without knowing its type, so a device skips records of types it does not know, and a type added later,
such as a text sensor with a length byte, does not break older devices. Larger entities such as a climate entity
would use the length form, with a layout of their own.

Two other layouts were considered:

- **Sections**, like the dictionary: a type and a count, then records of a number and a payload. A full send of many
  entities of one type is a few bytes smaller, but a single change, the usual packet, costs a byte more, and the
  sender must group the changed entities by type.
- **Payload sizes in the dictionary**, with no tag: every receiver would have to keep a table of sizes for each
  provider and type, where it now keeps only what it learnt for its own remote entities.

The tag costs a sensor one byte over a layout without types, which a full send pays once per sensor, and which the
session layer makes rare.

### Checking packets

A data or dictionary packet is checked in full before anything in it is used: each record or section must fit, its
type must not be 0, the value bit is only allowed with payload 0, sensors and binary sensors must use their own
payload forms, and a dictionary section's numbers must fit in one byte. A packet that fails is dropped whole, so a
damaged or forged packet that gets past the layers below changes no state and teaches nothing. A request packet must
be exactly 5 bytes. A packet that is too short, malformed, or of another format version is logged as a warning, at
most once a minute, as it points to a device with other firmware or a fault, and such packets can arrive steadily.

Every packet starts with a type byte, whose high nibble is the format version, and a 32-bit **dictionary id**: a hash
of the device name and every entity's type and id. Per packet, this is the only metadata:

- It tells a receiver which device sent a data packet, so several devices can share a broadcast channel without
  their names in every packet. The addressing layer's source address is not used instead: a stack with one peer has
  only one possible sender, and on a broadcast channel the dictionary id is still needed to tell old dictionaries
  from new ones, so it would save nothing.
- It changes whenever the device's entities change, so a receiver never applies numbers from an old dictionary.
- With session and reliability layers it is not strictly needed, but 4 bytes is little, and keeping one packet format
  is simpler than two.

A receiver keeps, for each remote entity, the dictionary id and number it learnt, not whole dictionaries, and the
last 8 dictionary ids it has received in full. A data packet with a dictionary id not among those makes it send a
request, at most every 5 seconds, even when some remote entities already learnt that id from the parts that arrived;
the device with that dictionary id answers with its dictionary and all states. A provider's new dictionary, or one
with a lost part, is so asked for, while one received in full is not asked for again, even if it names none of the
receiver's entities, since every answer resends everything to every device. With more than 8 providers on one
channel, the oldest are forgotten and asked for once more. A request the interface refuses does not start the 5
seconds, so the next data packet tries again.

### When states are sent

- On change, from `loop()`.
- The dictionary and all states at boot, when the session layer reports the peer connected, when requested, and every
  `update_interval`.
- `update_interval` defaults to `never` when the interface has both session and reliability capabilities, since the
  dictionary and states are then sent on every connect and arrive in order. Otherwise it defaults to 15 seconds.
- A packet the interface refuses is sent again after a second, with the latest states. Data waits for the dictionary.

When the session layer reports the peer lost, the remote entities lose their state. With `timeout`, a remote entity also loses its state when
none arrives for it within the timeout, for interfaces without a session layer; the timeout should be longer than the
provider's `update_interval`.

A dictionary in several packets resumes after the last packet the interface accepted, so a refused packet does not
make the earlier ones be sent again. Retrying from the start made a large dictionary over fragmentation resend its
first, largest packet whenever the queue had room for it, so the rest never went out.

### Memory

Local entities are a `FixedVector`, and remote entities are `StaticVector`s sized by `cg.slot_counter`. Packets are
built in one buffer allocated in `setup()`. Nothing is allocated after setup.

## Capabilities

Each layer adds a bit to the capabilities of the stack: `CAPABILITY_ADDRESSING`, `CAPABILITY_ENCRYPTION`,
`CAPABILITY_RELIABILITY`, `CAPABILITY_FRAGMENTATION` or `CAPABILITY_SESSION`. A layer copies the bits of the layer
below in `stack_on_()` and adds its own, so the top layer reports the whole stack. A link layer that does a layer's
job itself sets the bit in its own constructor, as `tcp` does for addressing, reliability and session. An application asks `top()->has_capability()` and adapts: `entity_transport` resends
periodically unless both session and reliability are present.

The session state callback is in `PacketInterface`, so an application needs no session class. An application
registers it on `top()`. The session layer is the top layer when configured; when a link provides the session, as
TCP does, each layer above registers on the one below and calls its own callbacks, in `inherit_from_()`, so the state
still reaches the top.

## Code generation

### Layer registry

The kinds of layer, their config keys, default types and stacking order are fixed in
`packet_interface/layers/__init__.py`, as `LayerKind` objects: `ADDRESSING`, then `STACK_ORDER` of `ENCRYPTION`,
`RELIABILITY`, `FRAGMENTATION` and `SESSION`. The order is part of the design, so it is not pluggable.

Each implementation is a module in `packet_interface/layers/` that registers itself with its kind; its C++ files stay
in `packet_interface/`:

```python
@RELIABILITY.register("hdlc", schema=HDLC_SCHEMA, single_peer=lambda config: True)
async def build_hdlc(config: ConfigType, lower: MockObj) -> MockObj: ...
```

- **Registration.** `packet_interface/__init__.py` imports every module in `layers/`, as `lvgl` does for its
  widgets, and only then builds the schemas, so they are ordinary schemas that the schema extractor can read. A new
  layer type is a new module and its C++ files; nothing else changes.
- **Schema.** A kind's schema is a `typed_schema` of its registered types, with a default type. A kind may take a
  shorthand, such as `encryption: my key`. A type whose schema contains other layers, as the addressing peers do,
  registers a `schema_factory` instead, called once every type is registered.
- **single_peer.** Says whether a configuration keeps state for one peer, such as `hdlc`, `basic` session, or `xxtea`
  with the rolling code. Validation uses it to reject these with broadcast addressing, and on a link that every
  device hears (broadcast UDP, broadcast ESP-NOW) without addressing, without naming the layers.
- **Building.** `build(config, lower)` creates the layer above `lower` and returns it; `build_stack()` calls the
  builders of one stack in `STACK_ORDER`. An addressing type's builder returns a list of (port, config) pairs, one
  for each stack, and `new_packet_interface()` builds a stack on each.
- **Contract for addressing types.** Their configuration has either `destination` or `peers`, each peer with `id`,
  `destination` and the layers of its stack, so the generic validation and application checks work for any of them.

The layer schemas are added to every link platform by `packet_interface_schema()`. A link platform only adds its
own options. Each layer is registered as a component, and its defines are added by its builder, so the C++ of unused
types compiles to nothing.

The ID in the configuration is declared with the link layer's class, but an application needs the top of the stack.
Each layer's constructor records itself as the layer above the one it is given, and `PacketInterface::top()` walks up
from the link layer to the top. Applications call `top()` in `setup()`, so the order in which code generation creates
the objects does not matter.

## Open questions

- **Mismatched stacks.** Two devices with different layers configured cannot understand each other. Each layer
  counts packets in a row that it cannot make sense of (encryption: a bad length or failed check; reliability: a short
  frame or bad CRC; session: an unknown type or a bad keep-alive), and logs a warning at 10, once per run; any packet
  it understands resets the count. Nothing changes on the wire. Addressing and fragmentation do not count: a packet
  for another device is normal on a shared channel, and a fragment header cannot be checked. A byte on every packet
  saying which layers are in use would detect every mismatch, but costs a byte per packet, needs every link and
  every peer stack to carry it, and breaks the UART link's compatibility with `packet_transport`'s framing.
- **Best effort packets.** HDLC unnumbered information (UI, control byte `0x03`) frames could carry data that is
  only useful while fresh, such as streamed readings, without a sequence number, queue slot or retry. A flag in
  `PacketMetaData` would select them. A UI frame must not be sent while I-frames are unacknowledged, or it could
  arrive before older data that is still being resent.
- **Ciphers.** Which ciphers are offered besides XXTEA, and how much each adds to the packet size.
- **Half-duplex radio links.** Testing `sx126x` and `sx127x` links between two boards found that the radio itself
  loses about 1.5% of packets, but the reliability layer timed out often and reset the link a few times a minute. A
  radio cannot receive while it transmits, so an RR sent about one loop after a frame collided with a second frame the
  peer sent straight after the first, and go-back-N resent bursts that collided again. Three changes are done:
  1. **Listen before talk:** the radio links refuse to send while the radio is receiving a packet (SX126x valid
     header or sync word flags, SX127x modem status, or a received packet not yet read), for at most 2 s.
  2. **Refused frames sent again promptly** by the reliability layer, as described above.
  3. **Acknowledgement delay:** `ack_delay`, about one frame time on a radio link.

  With `timeout: 500ms` and `ack_delay: 100ms` at SF7 and 125 kHz, two boards each sending a value every 2 s for
  3 minutes, over addressing, encryption with the rolling code, reliability and session, lost no values and did not
  reset the link; there were a few timeouts a minute. Before these changes the same test reset the link several times
  and lost values.

  **Checkpointing was tried and dropped.** On a timeout, instead of resending every unacknowledged frame, the layer
  polled the peer for its N(R) and resent only from there. Two forms were tested in the same setup: a poll as an RR
  with the poll bit, and a poll on the oldest frame, resent with the poll bit. Neither lost values or reset the link,
  but neither had fewer timeouts than go-back-N: at SF7 a frame takes about 70 ms, so resending the window is cheap.
  Both added a failure mode: when the poll or its answer collides, recovery waits a whole extra timeout, and the
  oldest-frame form, being a longer frame, went unanswered about a third of the time. The answer also had to be a
  plain RR, not one with the final bit as in HDLC: HDLC tells a final from a poll by the address field, and without one
  a late answer was taken for a new poll and the two devices answered each other forever. It may still be worth
  measuring at high spreading factors, where each frame takes hundreds of milliseconds.
- **Polled buses.** On RS485, a bus where one device polls the others would avoid collisions altogether. This would
  be a different link or addressing implementation.
- **Session sensor.** With several peers, each peer that has a session layer has its own session and its own
  binary sensor. A sensor for the whole channel is not offered.
