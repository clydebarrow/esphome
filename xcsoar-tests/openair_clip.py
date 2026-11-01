"""Keep the airspaces of an OpenAir file that overlap an area, so XCSoar loads only those.

python openair_clip.py IN OUT --lat -29 -25 --lon 148.5 153 [--margin-km 50]

An airspace is kept when its bounding box, with circles and arcs taken at their full radius, overlaps the area
widened by the margin. Comment lines before the first airspace are kept, and a line is added saying what was cut.
"""

import argparse
import math
import pathlib
import re

NM_KM = 1.852
KM_PER_DEG_LAT = 111.32

COORD = re.compile(
    r"(\d{1,3}):(\d{1,2}(?:\.\d+)?)(?::(\d{1,2}(?:\.\d+)?))?\s*([NSEW])", re.IGNORECASE
)


def parse_coords(text: str) -> list[tuple[float, float]]:
    """The (lat, lon) pairs in a line, in degrees."""
    values = []
    for deg, minutes, seconds, hemi in COORD.findall(text):
        value = (
            float(deg) + float(minutes) / 60 + (float(seconds) / 3600 if seconds else 0)
        )
        if hemi.upper() in "SW":
            value = -value
        values.append((hemi.upper(), value))
    pairs = []
    for i in range(0, len(values) - 1, 2):
        (h1, v1), (h2, v2) = values[i], values[i + 1]
        if h1 in "NS" and h2 in "EW":
            pairs.append((v1, v2))
    return pairs


class Box:
    def __init__(self) -> None:
        self.lat_min = self.lon_min = math.inf
        self.lat_max = self.lon_max = -math.inf

    def add(self, lat: float, lon: float, radius_km: float = 0.0) -> None:
        dlat = radius_km / KM_PER_DEG_LAT
        dlon = radius_km / (KM_PER_DEG_LAT * max(math.cos(math.radians(lat)), 0.01))
        self.lat_min = min(self.lat_min, lat - dlat)
        self.lat_max = max(self.lat_max, lat + dlat)
        self.lon_min = min(self.lon_min, lon - dlon)
        self.lon_max = max(self.lon_max, lon + dlon)

    def defined(self) -> bool:
        return self.lat_min <= self.lat_max

    def overlaps(self, lat: tuple[float, float], lon: tuple[float, float]) -> bool:
        return (
            self.lat_min <= lat[1]
            and self.lat_max >= lat[0]
            and self.lon_min <= lon[1]
            and self.lon_max >= lon[0]
        )


def distance_km(a: tuple[float, float], b: tuple[float, float]) -> float:
    dlat = (b[0] - a[0]) * KM_PER_DEG_LAT
    dlon = (b[1] - a[1]) * KM_PER_DEG_LAT * math.cos(math.radians((a[0] + b[0]) / 2))
    return math.hypot(dlat, dlon)


def airspace_box(lines: list[str]) -> Box:
    box = Box()
    center = None
    for line in lines:
        record = line.strip()
        upper = record.upper()
        if upper.startswith("V ") and "X=" in upper:
            if coords := parse_coords(record):
                center = coords[0]
        elif upper.startswith("DP "):
            for lat, lon in parse_coords(record):
                box.add(lat, lon)
        elif upper.startswith("DC ") and center is not None:
            radius = float(record[3:].split()[0]) * NM_KM
            box.add(*center, radius)
        elif upper.startswith("DA ") and center is not None:
            radius = float(record[3:].split(",")[0]) * NM_KM
            box.add(*center, radius)
        elif upper.startswith("DB ") and center is not None:
            for point in parse_coords(record):
                box.add(*center, distance_km(center, point))
    return box


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument(
        "--lat", type=float, nargs=2, required=True, metavar=("MIN", "MAX")
    )
    parser.add_argument(
        "--lon", type=float, nargs=2, required=True, metavar=("MIN", "MAX")
    )
    parser.add_argument("--margin-km", type=float, default=50.0)
    args = parser.parse_args()

    mid_lat = math.radians(sum(args.lat) / 2)
    dlat = args.margin_km / KM_PER_DEG_LAT
    dlon = args.margin_km / (KM_PER_DEG_LAT * math.cos(mid_lat))
    lat = (min(args.lat) - dlat, max(args.lat) + dlat)
    lon = (min(args.lon) - dlon, max(args.lon) + dlon)

    with pathlib.Path(args.input).open(encoding="utf-8", errors="replace") as f:
        text = f.read().splitlines()

    header: list[str] = []
    blocks: list[list[str]] = []
    for line in text:
        if line.strip().upper().startswith("AC "):
            blocks.append([line])
        elif blocks:
            blocks[-1].append(line)
        else:
            header.append(line)

    kept = [
        b
        for b in blocks
        if (box := airspace_box(b)).defined() and box.overlaps(lat, lon)
    ]
    with pathlib.Path(args.output).open("w", encoding="utf-8") as f:
        f.write("\n".join(header) + "\n")
        f.write(
            f"* Clipped by openair_clip.py to latitude {args.lat[0]} to {args.lat[1]}, longitude "
            f"{args.lon[0]} to {args.lon[1]}, with {args.margin_km:g} km margin: "
            f"{len(kept)} of {len(blocks)} airspaces\n"
        )
        f.writelines("\n".join(block) + "\n" for block in kept)
    print(f"{len(kept)} of {len(blocks)} airspaces kept")


if __name__ == "__main__":
    main()
