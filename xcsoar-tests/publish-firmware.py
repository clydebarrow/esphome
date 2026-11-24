"""Build the public firmware images and write them, with their manifest, into a checkout of the web flasher repo.

    python xcsoar-tests/publish-firmware.py --out ../xcsoar-firmware [--variant tab5-v1 ...] [--skip-build]

Run it from the root of this worktree. Each variant is built from its release-*.yaml, which leaves out the Wi-Fi
credentials, the API and OTA. Before an image is copied it is searched for every value in the my-devices secrets
files; any match stops the script. The images go into firmware/ of the output directory, and firmware/manifest.json
lists them for the page. Nothing is committed or pushed.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

TESTS = Path(__file__).resolve().parent
ROOT = TESTS.parent
MY_DEVICES = ROOT.parent.parent / "my-devices"
SECRET_FILES = ("secrets.yaml", "wifi.yaml", "ota.yaml")
# Values shorter than this, such as "16db" or "true", would match by chance
MIN_SECRET_LENGTH = 6
# Values that are also ordinary strings in ESPHome and lwIP, such as an SSID named after ESPHome
PUBLIC_VALUES = {b"esphome", b"ESPHome", b".local"}

# esp_app_desc_t follows the image header (24 bytes) and the first segment header (8 bytes)
APP_DESC_OFFSET = 32
APP_DESC_MAGIC = 0xABCD5432


@dataclass(frozen=True)
class Variant:
    key: str
    title: str
    description: str
    # The device name, which the page reads from the image on the board to recognise it
    device_name: str
    xcsoar_path: str


VARIANTS = (
    Variant(
        "tab5-v1",
        "M5Stack Tab5, first version",
        "The first Tab5 units",
        "xcsoar-tab5-v1",
        "../xcsoar-src-flarm",
    ),
    Variant(
        "tab5-st7121",
        "M5Stack Tab5 with the ST7121 panel",
        "A Tab5 whose display is the ST7121 panel",
        "xcsoar-tab5-st7121",
        "../xcsoar-src-flarm",
    ),
    Variant(
        "tab5-st7123",
        "M5Stack Tab5 with the ST7123 panel",
        "A Tab5 whose display is the ST7123 panel",
        "xcsoar-tab5-ble",
        "../xcsoar-src-flarm",
    ),
    Variant(
        "t-display-p4",
        "LilyGO T-Display-P4",
        "The LilyGO T-Display-P4 AMOLED board",
        "xcsoar-t-display-p4",
        "../xcsoar-src-flarm",
    ),
)


def git_version(path: Path) -> str:
    """The short commit of a checkout, with "+changes" when it has uncommitted changes."""
    commit = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--short", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    dirty = subprocess.run(
        ["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    return f"{commit}+changes" if dirty else commit


def secret_values() -> list[bytes]:
    """Every value in the my-devices secrets files that is long enough to search for."""
    values = []
    for name in SECRET_FILES:
        path = MY_DEVICES / name
        if not path.exists():
            continue
        for line in path.read_text().splitlines():
            match = re.match(r"^\s*-?\s*[\w.-]+\s*:\s*(.+?)\s*$", line)
            if match is None:
                continue
            value = match.group(1).strip("'\"")
            if (
                value.startswith("!")
                or len(value) < MIN_SECRET_LENGTH
                or value.encode() in PUBLIC_VALUES
            ):
                continue
            values.append(value.encode())
    return values


def check_secrets(image: bytes, secrets: list[bytes], label: str) -> None:
    for secret in secrets:
        if secret in image:
            sys.exit(
                f"{label}: a value from the my-devices secrets files is in the image; not published"
            )


def read_app_desc(app: bytes) -> dict[str, str]:
    """The device name, ESPHome version and ELF hash from the app description of an app image."""
    desc = app[APP_DESC_OFFSET : APP_DESC_OFFSET + 256]
    if int.from_bytes(desc[0:4], "little") != APP_DESC_MAGIC:
        sys.exit("No app description in the app image")

    def text(start: int, size: int) -> str:
        return desc[start : start + size].split(b"\0")[0].decode()

    return {
        "esphome_version": text(16, 32),
        "device_name": text(48, 32),
        "idf_version": text(112, 32),
        "elf_sha256": desc[144:176].hex(),
    }


def build(variant: Variant, skip_build: bool) -> Path:
    config = TESTS / f"release-{variant.key}.yaml"
    if not skip_build:
        print(f"Building {variant.key}", flush=True)
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "compile", str(config.relative_to(ROOT))],
            cwd=ROOT,
            check=False,
        )
        if result.returncode != 0:
            sys.exit(f"{variant.key}: the build failed")
    return TESTS / ".esphome" / "build" / f"release-{variant.key}" / "build"


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--out", required=True, type=Path, help="checkout of the web flasher repo"
    )
    parser.add_argument(
        "--variant",
        action="append",
        choices=[v.key for v in VARIANTS],
        help="only these variants",
    )
    parser.add_argument(
        "--skip-build", action="store_true", help="publish the images of the last build"
    )
    args = parser.parse_args()

    firmware = args.out / "firmware"
    firmware.mkdir(parents=True, exist_ok=True)
    manifest_path = firmware / "manifest.json"
    manifest = (
        json.loads(manifest_path.read_text())
        if manifest_path.exists()
        else {"builds": []}
    )
    builds = {build["variant"]: build for build in manifest.get("builds", [])}

    secrets = secret_values()
    if not secrets:
        sys.exit(f"No secrets found in {MY_DEVICES}; the images cannot be checked")

    now = dt.datetime.now(dt.UTC)
    version = now.strftime("%Y.%m.%d-%H%M")
    sources = {"esphome": git_version(ROOT)}

    for variant in VARIANTS:
        if args.variant and variant.key not in args.variant:
            continue
        build_dir = build(variant, args.skip_build)
        factory = (build_dir / "firmware.factory.bin").read_bytes()
        app = (build_dir / f"{variant.device_name}.bin").read_bytes()
        check_secrets(factory, secrets, variant.key)
        desc = read_app_desc(app)
        if desc["device_name"] != variant.device_name:
            sys.exit(
                f"{variant.key}: the image is for {desc['device_name']}, not {variant.device_name}"
            )
        xcsoar = git_version((ROOT / variant.xcsoar_path).resolve())
        sources["xcsoar"] = xcsoar

        file_name = f"{variant.key}.bin"
        shutil.copyfile(build_dir / "firmware.factory.bin", firmware / file_name)
        builds[variant.key] = {
            "variant": variant.key,
            "title": variant.title,
            "description": variant.description,
            "device_name": variant.device_name,
            "chip": "ESP32-P4",
            "version": version,
            "esphome": sources["esphome"],
            "xcsoar": xcsoar,
            "file": file_name,
            "size": len(factory),
            "sha256": hashlib.sha256(factory).hexdigest(),
            "elf_sha256": desc["elf_sha256"],
            "esphome_version": desc["esphome_version"],
            "idf_version": desc["idf_version"],
        }
        print(f"{variant.key}: {len(factory)} bytes, version {version}")

    manifest = {
        "name": "XCSoar for ESP32-P4",
        "updated": now.isoformat(timespec="seconds"),
        "builds": [builds[v.key] for v in VARIANTS if v.key in builds],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Wrote {manifest_path}")


if __name__ == "__main__":
    main()
