# Packet interface design

This document describes where the `packet_interface` component is going. The link layers (`uart`, `udp`), the
reliability and session layers, and the `packet_transport` bridge exist today. The other layers described here are
planned.

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
- **Encryption below reliability.** Acknowledgements are encrypted too, so they cannot be forged, and a resent
  packet is identical to the original.
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
5. Addressing, last. Until it exists, reliability and session work only between two devices on one channel.

## Example configuration

```yaml
packet_interface:
  - platform: uart # RS485 multidrop
    id: rs485_comms
    encryption:
      secret_key: Johnny Cash
      cipher: XXTEA
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
      source_address: 0x1234 # We only receive packets addressed to us
      destination_address: [0xDEAD, 0xBEEF] # Two peers

packet_transport:
  - platform: packet_interface
    packet_interface: rs485_comms
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

`packet_transport` keeps its own encryption, rolling code and ping-pong features for now, and is used through the
`packet_interface` transport platform. It will later be replaced by a new component (perhaps `entity_transport`)
that relies on the packet interface layers for these features.

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
- **Replay protection.** Whether the encryption layer needs a counter or nonce, or whether the session layer
  provides it. The reliability layer does not help here: its CRC detects damage, not forged or replayed frames. An
  application such as `entity_transport` should not need packet_transport's ping and pong exchange if this is
  solved below it.
- **Link up event.** The session layer reports when the peer is connected, including after a restart.
  `entity_transport` could send all states once at that point, instead of resending them at a fixed interval, since
  on a reliable link states are only missed across a reset. How an application finds the session layer in the stack
  is still open: `top()` returns it only when it is the top layer.
- **Best effort packets.** HDLC unnumbered information (UI, control byte `0x03`) frames could carry data that is
  only useful while fresh, such as streamed readings, without a sequence number, queue slot or retry. A flag in
  `PacketMetaData` would select them. A UI frame must not be sent while I-frames are unacknowledged, or it could
  arrive before older data that is still being resent.
- **Address size.** The example uses 16-bit addresses. Decide whether this is fixed, and whether broadcast is a
  reserved address.
- **Ciphers.** Which ciphers are offered besides XXTEA, and how much each adds to the packet size.
- **Reliability with several peers.** How acknowledgements and the send queue work when there is more than one
  destination address.
- **Room to send.** The fragmentation layer needs to know, before it starts a packet, that the layer below will take
  all of its fragments. A `PacketInterface` method that reports how many packets can be sent now would do this. The
  reliability queue must also be at least as long as the number of fragments in the largest packet.
- **Fragment header size.** Whether the index needs more than 7 bits, which limits a packet to 128 fragments.
- **Session sensor.** With several peers, whether there is one sensor per peer or one for the whole channel.
