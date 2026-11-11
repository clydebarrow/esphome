"""Send an IGC flight to XCSoar as NMEA over UDP, as a GPS would: python igc_to_udp.py HOST FILE.igc [SPEEDUP] [PORT]

Each B record becomes $GPRMC, $GPGGA and $PGRMZ (pressure altitude); speed and track come from the next fix.
SPEEDUP sends the fixes that much faster than real time (default 1); PORT defaults to 4353, XCSoar's UDP port.
"""

import math
import pathlib
import socket
import sys
import time


def checksum(body: str) -> str:
    value = 0
    for c in body:
        value ^= ord(c)
    return f"${body}*{value:02X}\r\n"


def parse(path: str) -> tuple[str, list[dict]]:
    date = "010100"
    fixes = []
    for line in (
        pathlib.Path(path).read_text(encoding="ascii", errors="replace").splitlines()
    ):
        if line.startswith("HFDTE"):
            digits = "".join(c for c in line if c.isdigit())
            date = digits[:6]
        if not line.startswith("B") or len(line) < 35:
            continue
        lat = int(line[7:9]) + (int(line[9:11]) + int(line[11:14]) / 1000) / 60
        if line[14] == "S":
            lat = -lat
        lon = int(line[15:18]) + (int(line[18:20]) + int(line[20:23]) / 1000) / 60
        if line[23] == "W":
            lon = -lon
        fixes.append(
            {
                "time": line[1:7],
                "seconds": int(line[1:3]) * 3600 + int(line[3:5]) * 60 + int(line[5:7]),
                "lat": lat,
                "lon": lon,
                "valid": line[24] == "A",
                "pressure_alt": int(line[25:30]),
                "gps_alt": int(line[30:35]),
            }
        )
    return date, fixes


def nmea_angle(value: float, positive: str, negative: str, degree_digits: int) -> str:
    hemisphere = positive if value >= 0 else negative
    value = abs(value)
    degrees = int(value)
    minutes = (value - degrees) * 60
    return f"{degrees:0{degree_digits}d}{minutes:07.4f},{hemisphere}"


def main() -> None:
    host = sys.argv[1]
    date, fixes = parse(sys.argv[2])
    speedup = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    port = int(sys.argv[4]) if len(sys.argv) > 4 else 4353
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"{len(fixes)} fixes, {date}", flush=True)
    for i, fix in enumerate(fixes):
        following = fixes[min(i + 1, len(fixes) - 1)]
        dt = max(following["seconds"] - fix["seconds"], 1)
        north = (following["lat"] - fix["lat"]) * 111195
        east = (
            (following["lon"] - fix["lon"])
            * 111195
            * math.cos(math.radians(fix["lat"]))
        )
        knots = math.hypot(north, east) / dt * 1.943844
        track = math.degrees(math.atan2(east, north)) % 360
        lat = nmea_angle(fix["lat"], "N", "S", 2)
        lon = nmea_angle(fix["lon"], "E", "W", 3)
        status = "A" if fix["valid"] else "V"
        quality = 1 if fix["valid"] else 0
        sentences = [
            checksum(
                f"GPRMC,{fix['time']}.00,{status},{lat},{lon},{knots:.1f},{track:.1f},{date},,,A"
            ),
            checksum(
                f"GPGGA,{fix['time']}.00,{lat},{lon},{quality},08,1.0,{fix['gps_alt']:.1f},M,0.0,M,,"
            ),
            checksum(f"PGRMZ,{fix['pressure_alt'] * 3.28084:.0f},f,3"),
        ]
        sock.sendto("".join(sentences).encode(), (host, port))
        if i % 60 == 0:
            print(f"{fix['time']} {i}/{len(fixes)}", flush=True)
        time.sleep(dt / speedup)
    print("done", flush=True)


main()
