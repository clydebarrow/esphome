"""A simulated tow for the tug logger, as GDL90 over UDP like a SkyEcho: python fly.py HOST [--climb-ft 2000]

Sends a heartbeat, ownship, GPS altitude and one traffic target each second: on the ground, a takeoff run, a climb, a
descent and a landing. Keep the real SkyEcho off, so that the device stays on a network this computer can reach.

GDL90 arriving marks the network it came over as a SkyEcho, on which the device neither uploads nor looks for the real
one. Afterwards forget that network (the forget_network action of tug-d1001-test.yaml, or tap it in Settings).

--towed adds a target 60 m behind the tug from the takeoff run to release, with --towed-category 9 for a glider.
"""

import argparse
import math
import socket
import struct
import time

PORT = 4000
FIELD_FT = 1500  # what the "GPS" reads on the ground, unlike the field elevation in the tug's file
LAT, LON = -28.2, 152.03
TRACK_BYTE = 90  # in steps of 360/256 degrees
ROPE_M = 60


def _crc_table() -> list[int]:
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
        table.append(crc)
    return table


TABLE = _crc_table()


def frame(message: bytes) -> bytes:
    crc = 0
    for byte in message:
        crc = TABLE[crc >> 8] ^ ((crc << 8) & 0xFFFF) ^ byte
    out = bytearray([0x7E])
    for byte in message + struct.pack("<H", crc):
        if byte in (0x7D, 0x7E):
            out += bytes([0x7D, byte ^ 0x20])
        else:
            out.append(byte)
    out.append(0x7E)
    return bytes(out)


def heartbeat() -> bytes:
    now = time.gmtime()
    seconds = now.tm_hour * 3600 + now.tm_min * 60 + now.tm_sec
    status2 = 0x01 | (0x80 if seconds & 0x10000 else 0)
    return frame(
        bytes([0, 0x81, status2]) + struct.pack("<H", seconds & 0xFFFF) + b"\0\0"
    )


def _coord(value: float) -> bytes:
    return struct.pack(">i", round(value * 0x1000000 / 360) << 8)[:3]


def position(
    kind: int,
    address: int,
    callsign: bytes,
    lat: float,
    lon: float,
    alt_ft: float,
    knots: float,
    category: int = 1,
) -> bytes:
    alt = max(0, min(0xFFE, round((alt_ft + 1000) / 25)))
    speed = round(knots)
    return frame(
        bytes([kind, 0])
        + address.to_bytes(3, "big")
        + _coord(lat)
        + _coord(lon)
        + bytes(
            [
                alt >> 4,
                ((alt & 0xF) << 4) | 0x09,
                0xA9,
                speed >> 4,
                (speed & 0xF) << 4,
                0,
                TRACK_BYTE,
                category,
            ]
        )
        + callsign.ljust(8)
        + b"\0"
    )


def behind(metres: float) -> tuple[float, float]:
    """The position this far behind the tug, along its track."""
    track = math.radians(TRACK_BYTE * 360 / 256)
    return (
        LAT - metres * math.cos(track) / 111320,
        LON - metres * math.sin(track) / (111320 * math.cos(math.radians(LAT))),
    )


def geo_altitude(alt_ft: float) -> bytes:
    return frame(bytes([11]) + struct.pack(">h", round(alt_ft / 5)) + b"\0\x0a")


def profile(climb_ft: float):
    """(knots, height in feet), one a second."""
    yield from ((0, 0) for _ in range(20))
    yield from ((20 + i * 6, 0) for i in range(10))  # the takeoff run
    yield from ((70, climb_ft * i / 60) for i in range(61))
    yield from ((70, climb_ft) for _ in range(5))  # release at the top
    yield from ((90, climb_ft * (60 - i) / 60) for i in range(61))
    yield from ((max(5, 60 - i * 6), 0) for i in range(10))  # the landing run
    yield from ((5, 0) for _ in range(30))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("--climb-ft", type=float, default=2000)
    parser.add_argument("--towed", help="callsign of a target behind the tug")
    parser.add_argument("--towed-category", type=int, default=9)
    args = parser.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.host, PORT)
    release_step = 20 + 10 + 61 + 5
    for step, (knots, height) in enumerate(profile(args.climb_ft)):
        alt = FIELD_FT + height
        data = heartbeat()
        data += position(10, 0x7C1234, b"VHTEST", LAT, LON, alt, knots)
        data += geo_altitude(alt)
        data += position(
            20, 0x7C5678, b"QFA123", LAT + 0.03, LON + 0.02, alt + 400, 120
        )
        if args.towed and 20 <= step < release_step:
            lat, lon = behind(ROPE_M)
            data += position(
                20,
                0x7C9ABC,
                args.towed.encode(),
                lat,
                lon,
                alt + 50,
                knots,
                args.towed_category,
            )
        sock.sendto(data, target)
        if step % 10 == 0:
            print(f"{step:4d} s  {knots:3.0f} kt  {height:5.0f} ft", flush=True)
        time.sleep(1)


if __name__ == "__main__":
    main()
