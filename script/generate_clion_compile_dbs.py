#!/usr/bin/env python3
"""Write a compile_commands.json per PlatformIO environment, for use as CLion projects.

Usage:
    script/generate_clion_compile_dbs.py                 Every environment in platformio.ini
    script/generate_clion_compile_dbs.py esp32p4-idf ... Only the named environments

Each database is written to <output>/<env>/compile_commands.json, by default under ~/clion-dbs.
Open one in CLion as a project, then use Tools > Compilation Database > Change Project Root to
select the repository. An environment that fails
is reported and skipped, and its existing database is kept. Rerun after adding a source file or
changing platformio.ini.

PlatformIO shares .pio/build and managed_components between environments and replaces them when
another environment is configured. The headers each database needs from those two folders are
therefore copied to <output>/<env>/snapshot.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = Path.home() / "clion-dbs"
ENV_PREFIX = "env:"
MANAGED_COMPONENTS = REPO / "managed_components"
CXX_SUFFIXES = {".cc", ".cpp"}
C_SUFFIXES = {".c"}


def run_pio(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["pio", *args], cwd=REPO, capture_output=True, text=True, check=False
    )


def list_environments() -> list[str]:
    result = run_pio("project", "config", "--json-output")
    if result.returncode != 0:
        sys.exit(f"Could not read platformio.ini:\n{result.stderr}")
    return [
        section.removeprefix(ENV_PREFIX)
        for section, _ in json.loads(result.stdout)
        if section.startswith(ENV_PREFIX)
    ]


def read_metadata(env: str) -> dict:
    with tempfile.TemporaryDirectory() as temp_dir:
        path = Path(temp_dir) / "metadata.json"
        result = run_pio(
            "project",
            "metadata",
            "-e",
            env,
            "--json-output",
            "--json-output-path",
            str(path),
        )
        if result.returncode != 0 or not path.exists():
            lines = (result.stderr or result.stdout).strip().splitlines()
            raise RuntimeError(lines[-1] if lines else "pio project metadata failed")
        return json.loads(path.read_text(encoding="utf-8"))[env]


def repair_flags(flags: list[str]) -> list[str]:
    """Put each file name back beside -include.

    The espressif32 platform sorts its flags, which separates -include from its file. The result
    is an invalid command line, so the compiler cannot be queried.
    """
    files = [flag for flag in flags if not flag.startswith("-")]
    if "-include" not in flags or len(files) != flags.count("-include"):
        return flags
    repaired = [flag for flag in flags if flag.startswith("-") and flag != "-include"]
    for file in files:
        repaired += ["-include", file]
    return repaired


def source_files() -> list[Path]:
    sources = [
        path
        for path in (REPO / "esphome").rglob("*")
        if path.suffix in CXX_SUFFIXES | C_SUFFIXES and path.is_file()
    ]
    return sorted(sources) + [REPO / "tests" / "dummy_main.cpp"]


def snapshot_include(path: str, build_dir: Path, snapshot: Path) -> str:
    """Copy an include folder that another environment would replace, and return its new path."""
    include = Path(path)
    if include.is_relative_to(MANAGED_COMPONENTS):
        # Copy the whole component because its headers can include each other by relative path
        relative = include.relative_to(REPO)
        source = REPO / Path(*relative.parts[:2])
        target = snapshot / Path(*relative.parts[:2])
    elif include.is_relative_to(build_dir):
        relative = Path("build") / include.relative_to(build_dir)
        source = include
        target = snapshot / relative
    else:
        return path
    if source.is_dir():
        shutil.copytree(source, target, dirs_exist_ok=True)
    return str(snapshot / relative)


def build_entries(metadata: dict, sources: list[Path], snapshot: Path) -> list[dict]:
    defines = [f"-D{define}" for define in metadata["defines"]]
    includes = metadata["includes"]
    build_dir = REPO / ".pio" / "build" / metadata["env_name"]
    include_flags = [
        f"-I{snapshot_include(path, build_dir, snapshot)}"
        for path in includes["build"] + includes["compatlib"]
    ]
    cxx = [
        metadata["cxx_path"],
        *repair_flags(metadata["cxx_flags"]),
        *defines,
        *include_flags,
    ]
    cc = [
        metadata["cc_path"],
        *repair_flags(metadata["cc_flags"]),
        *defines,
        *include_flags,
    ]
    return [
        {
            "directory": str(REPO),
            "arguments": [
                *(cxx if source.suffix in CXX_SUFFIXES else cc),
                "-c",
                str(source),
            ],
            "file": str(source),
        }
        for source in sources
    ]


def generate(env: str, sources: list[Path], output_root: Path) -> Path:
    env_dir = output_root / env
    env_dir.mkdir(parents=True, exist_ok=True)
    metadata = read_metadata(env)

    snapshot = env_dir / "snapshot"
    shutil.rmtree(snapshot, ignore_errors=True)
    snapshot.mkdir()
    entries = build_entries(metadata, sources, snapshot)

    output = env_dir / "compile_commands.json"
    temporary = output.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(entries, indent=1), encoding="utf-8")
    temporary.replace(output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "environments", nargs="*", help="default: all in platformio.ini"
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    envs = args.environments or list_environments()

    sources = source_files()
    failed: dict[str, str] = {}
    for index, env in enumerate(envs, start=1):
        print(f"[{index}/{len(envs)}] {env}: ", end="", flush=True)
        started = time.monotonic()
        try:
            output = generate(env, sources, args.output)
        except (RuntimeError, KeyError, ValueError, OSError) as err:
            failed[env] = str(err)
            print(f"FAILED: {err}", flush=True)
            continue
        print(f"{output} ({time.monotonic() - started:.0f}s)", flush=True)

    print(f"\n{len(envs) - len(failed)} written, {len(failed)} failed")
    for env, reason in failed.items():
        print(f"  {env}: {reason}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
