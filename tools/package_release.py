#!/usr/bin/env python3
"""Build a deterministic RFScout firmware release bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path


EXPECTED_TARGET = "esp32s3"
EXPECTED_CHIP_FAMILY = "ESP32-S3"
EXPECTED_FLASH_MODE = "dio"
EXPECTED_FLASH_FREQUENCY = "80m"
EXPECTED_FLASH_SIZE = "16MB"
EXPECTED_PARTS = {
    0x0000: "bootloader/bootloader.bin",
    0x8000: "partition_table/partition-table.bin",
    0x10000: "rfscout.bin",
}
VERSION_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")
RELEASE_VERSION_RE = re.compile(
    r"^v(?:0|[1-9][0-9]*)\."
    r"(?:0|[1-9][0-9]*)\."
    r"(?:0|[1-9][0-9]*)"
    r"(?:-(?P<prerelease>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+(?P<build>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


class PackagingError(RuntimeError):
    """Raised when build output cannot form a safe release bundle."""


def validate_release_version(a_version: str) -> None:
    """Validate a v-prefixed Semantic Versioning release identifier."""
    match = RELEASE_VERSION_RE.fullmatch(a_version)
    if match is None:
        raise PackagingError(
            "release version must be v-prefixed Semantic Versioning, such as "
            "v0.1.0 or v0.1.0-rc.1"
        )

    prerelease = match.group("prerelease")
    if prerelease is None:
        return
    for identifier in prerelease.split("."):
        if identifier.isdigit() and len(identifier) > 1 and identifier.startswith("0"):
            raise PackagingError(
                "numeric prerelease identifiers must not contain leading zeroes"
            )


def sha256(a_path: Path) -> str:
    digest = hashlib.sha256()
    with a_path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(a_path: Path, a_value: object) -> None:
    a_path.write_text(
        json.dumps(a_value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def flash_option(a_args: list[str], a_name: str) -> str:
    try:
        return a_args[a_args.index(a_name) + 1]
    except (ValueError, IndexError) as error:
        raise PackagingError(f"flasher_args.json is missing {a_name}") from error


def validate_build(a_build_dir: Path) -> dict:
    metadata_path = a_build_dir / "flasher_args.json"
    if not metadata_path.is_file():
        raise PackagingError(f"missing ESP-IDF metadata: {metadata_path}")

    try:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PackagingError(f"cannot read {metadata_path}: {error}") from error

    target = metadata.get("extra_esptool_args", {}).get("chip")
    if target != EXPECTED_TARGET:
        raise PackagingError(f"expected target {EXPECTED_TARGET}, found {target!r}")

    raw_files = metadata.get("flash_files")
    if not isinstance(raw_files, dict):
        raise PackagingError("flasher_args.json does not contain flash_files")

    try:
        flash_files = {int(offset, 0): Path(path).as_posix() for offset, path in raw_files.items()}
    except (TypeError, ValueError) as error:
        raise PackagingError("flasher_args.json contains an invalid flash offset") from error

    if flash_files != EXPECTED_PARTS:
        expected = {hex(offset): path for offset, path in EXPECTED_PARTS.items()}
        actual = {hex(offset): path for offset, path in flash_files.items()}
        raise PackagingError(f"unexpected flash layout: expected {expected}, found {actual}")

    write_args = metadata.get("write_flash_args")
    if not isinstance(write_args, list) or not all(isinstance(item, str) for item in write_args):
        raise PackagingError("flasher_args.json does not contain write_flash_args")

    flash_settings = {
        "mode": flash_option(write_args, "--flash-mode"),
        "frequency": flash_option(write_args, "--flash-freq"),
        "size": flash_option(write_args, "--flash-size"),
    }
    expected_settings = {
        "mode": EXPECTED_FLASH_MODE,
        "frequency": EXPECTED_FLASH_FREQUENCY,
        "size": EXPECTED_FLASH_SIZE,
    }
    if flash_settings != expected_settings:
        raise PackagingError(
            f"unexpected flash settings: expected {expected_settings}, found {flash_settings}"
        )

    for relative_path in EXPECTED_PARTS.values():
        image_path = a_build_dir / relative_path
        if not image_path.is_file() or image_path.stat().st_size == 0:
            raise PackagingError(f"missing or empty build image: {image_path}")

    return metadata


def flashing_guide(a_version: str, a_full_image: str) -> str:
    return f"""# Flash RFScout {a_version}

This is the universal ESP32-S3 RFScout image for the supported LILYGO T-Embed CC1101 Plus and T-Deck Plus, both with 16 MB of flash. RFScout detects the supported hardware profile at startup; unsupported hardware remains awake and inert for recovery flashing.

## Clean installation

Install Python 3 and esptool, connect the device over USB, replace `PORT` with the device's serial port, and run:

```sh
python -m pip install esptool
python -m esptool --chip esp32s3 --port PORT --baud 460800 \\
    --before default-reset --after hard-reset write-flash \\
    --flash-mode dio --flash-size 16MB --flash-freq 80m \\
    0x0 {a_full_image}
```

The combined image contains the RFScout bootloader, partition table, and application. Treat it as a clean installation: it can replace internal NVS and other internal-flash state. It does not erase or modify the removable SD card.

If automatic reset does not enter the ROM bootloader:

- **T-Embed CC1101 Plus:** hold the encoder center button, press and release reset, then release the encoder button.
- **T-Deck Plus:** disconnect USB, hold the trackball center/BOOT button while reconnecting USB, then release it after the ROM bootloader enumerates.

Press reset after flashing if the device does not restart automatically.

## Advanced images

The separate bootloader, partition-table, and application images are included for development and recovery. RFScout does not yet promise app-only update compatibility across releases; use the combined image for normal installation.

Verify downloaded files against `SHA256SUMS` before flashing.
"""


def deterministic_zip(a_output: Path, a_files: list[Path]) -> None:
    with zipfile.ZipFile(
        a_output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for path in sorted(a_files, key=lambda item: item.name):
            info = zipfile.ZipInfo(path.name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes(), compresslevel=9)


def package_release(
    a_build_dir: Path,
    a_merged_image: Path,
    a_output_dir: Path,
    a_version: str,
    a_commit: str,
    a_idf_version: str,
) -> list[Path]:
    if not VERSION_RE.fullmatch(a_version):
        raise PackagingError(
            "version must contain only letters, numbers, dots, underscores, and hyphens"
        )
    if not re.fullmatch(r"[0-9a-fA-F]{7,40}", a_commit):
        raise PackagingError("commit must be a 7- to 40-character hexadecimal Git SHA")
    if not a_merged_image.is_file() or a_merged_image.stat().st_size == 0:
        raise PackagingError(f"missing or empty merged image: {a_merged_image}")

    validate_build(a_build_dir)

    if a_output_dir.exists():
        shutil.rmtree(a_output_dir)
    a_output_dir.mkdir(parents=True)

    stem = f"rfscout-{a_version}-esp32s3"
    image_sources = {
        f"{stem}-full.bin": a_merged_image,
        f"{stem}-app.bin": a_build_dir / EXPECTED_PARTS[0x10000],
        f"{stem}-bootloader.bin": a_build_dir / EXPECTED_PARTS[0x0000],
        f"{stem}-partition-table.bin": a_build_dir / EXPECTED_PARTS[0x8000],
    }
    copied: list[Path] = []
    for name, source in image_sources.items():
        destination = a_output_dir / name
        shutil.copyfile(source, destination)
        copied.append(destination)

    part_names = {
        0x0000: f"{stem}-bootloader.bin",
        0x8000: f"{stem}-partition-table.bin",
        0x10000: f"{stem}-app.bin",
    }
    parts = []
    for offset, name in part_names.items():
        path = a_output_dir / name
        parts.append(
            {
                "file": name,
                "offset": offset,
                "offset_hex": hex(offset),
                "sha256": sha256(path),
                "size": path.stat().st_size,
            }
        )

    full_name = f"{stem}-full.bin"
    full_path = a_output_dir / full_name
    manifest = {
        "schema_version": 1,
        "project": "RFScout",
        "version": a_version,
        "commit": a_commit.lower(),
        "target": EXPECTED_TARGET,
        "chip_family": EXPECTED_CHIP_FAMILY,
        "esp_idf_version": a_idf_version.strip(),
        "flash": {
            "frequency": EXPECTED_FLASH_FREQUENCY,
            "mode": EXPECTED_FLASH_MODE,
            "size": EXPECTED_FLASH_SIZE,
        },
        "combined_image": {
            "file": full_name,
            "offset": 0,
            "offset_hex": "0x0",
            "sha256": sha256(full_path),
            "size": full_path.stat().st_size,
        },
        "parts": parts,
    }
    manifest_path = a_output_dir / "manifest.json"
    write_json(manifest_path, manifest)

    web_manifest = {
        "name": "RFScout",
        "version": a_version.removeprefix("v"),
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": EXPECTED_CHIP_FAMILY,
                "parts": [{"path": full_name, "offset": 0}],
            }
        ],
    }
    web_manifest_path = a_output_dir / "manifest.web.json"
    write_json(web_manifest_path, web_manifest)

    guide_path = a_output_dir / "FLASHING.md"
    guide_path.write_text(flashing_guide(a_version, full_name), encoding="utf-8")

    checksum_inputs = copied + [manifest_path, web_manifest_path, guide_path]
    checksum_path = a_output_dir / "SHA256SUMS"
    checksum_path.write_text(
        "".join(
            f"{sha256(path)}  {path.name}\n"
            for path in sorted(checksum_inputs, key=lambda item: item.name)
        ),
        encoding="utf-8",
    )

    zip_inputs = checksum_inputs + [checksum_path]
    zip_path = a_output_dir / f"{stem}.zip"
    deterministic_zip(zip_path, zip_inputs)
    return sorted(zip_inputs + [zip_path], key=lambda item: item.name)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--merged-image", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--idf-version", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        files = package_release(
            args.build_dir,
            args.merged_image,
            args.output_dir,
            args.version,
            args.commit,
            args.idf_version,
        )
    except PackagingError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    for path in files:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
