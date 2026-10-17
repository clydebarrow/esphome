# Packet interface design

This document describes where the `packet_interface` component is going. Only the link layers (`uart`, `udp`) and
the `packet_transport` bridge exist today. The other layers described here are planned.

## Goals

- Split packet handling into layers. Each layer does one job.
- Let any link layer use any of the other layers, without the link layer knowing about them.
- Let the user configure the layers they want on one `packet_interface` entry, without knowing the order in which
  they must be stacked.
- Give an application, such as `packet_transport`, one fully configured interface to use.
- Pass data between layers without copying it where possible, using `PacketBuffer`.

## Layers

From the bottom up:

| Layer        | Job                                                                         |
|--------------|-----------------------------------------------------------------------------|
| Link         | Moves packets over a real channel: UART, UDP, and later radio links.        |
| Addressing   | Adds source and destination addresses; drops packets not addressed to us.   |
| Encryption   | Encrypts and authenticates packets with a shared key.                       |
| Reliability  | Acknowledges packets and resends those not acknowledged in time.            |
| Session      | Sends keep-alive packets and reports whether the peer can be reached.       |
| Application  | Uses the packets, for example `packet_transport`. Not part of the stack.    |

Every layer except the application is a `PacketInterface`. A layer above the link layer holds a pointer to the layer
below it and registers itself as that layer's listener. To the layer above, it looks like any other
`PacketInterface`.

### Layer order

The order is fixed by the code, not by the user. The proposed order and the reasons for it:

- **Addressing below encryption.** A device can drop packets meant for another device on a shared bus (such as
  RS485) without spending time decrypting them.
- **Encryption below reliability.** Acknowledgements are encrypted too, so they cannot be forged, and a resent
  packet is identical to the original.
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
4. Addressing, last. Until it exists, reliability and session work only between two devices on one channel.

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

## Applications

`packet_transport` keeps its own encryption, rolling code and ping-pong features for now, and is used through the
`packet_interface` transport platform. It will later be replaced by a new component (perhaps `entity_transport`)
that relies on the packet interface layers for these features.

## Code generation

The layer schemas are defined once in `packet_interface/__init__.py` and added to every link platform by
`packet_interface_schema()`. A link platform only adds its own options.

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
  provides it.
- **Address size.** The example uses 16-bit addresses. Decide whether this is fixed, and whether broadcast is a
  reserved address.
- **Ciphers.** Which ciphers are offered besides XXTEA, and how much each adds to the packet size.
- **Reliability with several peers.** How acknowledgements and the send queue work when there is more than one
  destination address.
- **Session sensor.** With several peers, whether there is one sensor per peer or one for the whole channel.
