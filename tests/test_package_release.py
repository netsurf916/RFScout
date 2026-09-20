"""Tests for the RFScout release packager."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

from package_release import (  # noqa: E402
    PackagingError,
    package_release,
    validate_release_version,
)


class PackageReleaseTest(unittest.TestCase):
    def test_accepts_semantic_release_versions(self) -> None:
        for version in (
            "v0.0.0",
            "v1.2.3",
            "v1.2.3-rc.1",
            "v1.2.3-alpha-1",
            "v1.2.3+build.01",
            "v1.2.3-rc.1+build.01",
        ):
            with self.subTest(version=version):
                validate_release_version(version)

    def test_rejects_malformed_semantic_release_versions(self) -> None:
        for version in (
            "1.2.3",
            "v01.0.0",
            "v1.02.0",
            "v1.0.03",
            "v1.0.0-01",
            "v1.0.0-rc..1",
            "v1.0.0-.rc",
            "v1.0.0-rc.",
            "v1.0.0+build..1",
        ):
            with self.subTest(version=version):
                with self.assertRaises(PackagingError):
                    validate_release_version(version)

    def make_build(
        self,
        a_root: Path,
        *,
        a_chip: str = "esp32s3",
        a_project_version: str = "0.1.0",
    ) -> tuple[Path, Path]:
        build = a_root / "build"
        (build / "bootloader").mkdir(parents=True)
        (build / "partition_table").mkdir()
        (build / "bootloader" / "bootloader.bin").write_bytes(b"bootloader")
        (build / "partition_table" / "partition-table.bin").write_bytes(b"partitions")
        (build / "ota_data_initial.bin").write_bytes(b"ota metadata")
        (build / "rfscout.bin").write_bytes(b"application")
        merged = build / "rfscout-full.bin"
        merged.write_bytes(b"combined firmware")
        metadata = {
            "extra_esptool_args": {"chip": a_chip},
            "write_flash_args": [
                "--flash-mode",
                "dio",
                "--flash-freq",
                "80m",
                "--flash-size",
                "16MB",
            ],
            "flash_files": {
                "0x0": "bootloader/bootloader.bin",
                "0x8000": "partition_table/partition-table.bin",
                "0xd000": "ota_data_initial.bin",
                "0x10000": "rfscout.bin",
            },
        }
        (build / "flasher_args.json").write_text(json.dumps(metadata), encoding="utf-8")
        (build / "project_description.json").write_text(
            json.dumps(
                {
                    "project_name": "rfscout",
                    "project_version": a_project_version,
                }
            ),
            encoding="utf-8",
        )
        return build, merged

    def test_package_contains_release_and_web_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build, merged = self.make_build(root)
            output = root / "dist"
            files = package_release(
                build,
                merged,
                output,
                "v0.1.0",
                "0123456789abcdef0123456789abcdef01234567",
                "ESP-IDF v6.1-dev",
            )

            names = {path.name for path in files}
            self.assertIn("rfscout-v0.1.0-esp32s3-full.bin", names)
            self.assertIn("rfscout-v0.1.0-esp32s3.zip", names)
            self.assertIn("ota-release.json", names)
            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["target"], "esp32s3")
            self.assertEqual([part["offset"] for part in manifest["parts"]], [0, 0x8000, 0x10000])
            ota_release = json.loads((output / "ota-release.json").read_text(encoding="utf-8"))
            self.assertEqual(ota_release["version"], "0.1.0")
            self.assertEqual(ota_release["hardware_target"], "t-embed-cc1101-plus")
            self.assertEqual(ota_release["idf_target"], "esp32s3")
            self.assertEqual(ota_release["partition_layout"], "ab-v1")
            self.assertEqual(
                ota_release["artifact"]["file"],
                "rfscout-v0.1.0-esp32s3-app.bin",
            )
            web = json.loads((output / "manifest.web.json").read_text(encoding="utf-8"))
            self.assertEqual(web["builds"][0]["chipFamily"], "ESP32-S3")
            self.assertEqual(web["builds"][0]["parts"][0]["offset"], 0)

            with zipfile.ZipFile(output / "rfscout-v0.1.0-esp32s3.zip") as archive:
                self.assertIn("manifest.web.json", archive.namelist())
                self.assertIn("SHA256SUMS", archive.namelist())

    def test_zip_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build, merged = self.make_build(
                root, a_project_version="candidate-0123456789ab"
            )
            hashes = []
            for name in ("first", "second"):
                output = root / name
                package_release(
                    build,
                    merged,
                    output,
                    "candidate-0123456789ab",
                    "0123456789abcdef0123456789abcdef01234567",
                    "ESP-IDF v6.1-dev",
                )
                archive = output / "rfscout-candidate-0123456789ab-esp32s3.zip"
                hashes.append(hashlib.sha256(archive.read_bytes()).hexdigest())
            self.assertEqual(hashes[0], hashes[1])

    def test_rejects_mismatched_embedded_version(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build, merged = self.make_build(root, a_project_version="0.0.9")
            with self.assertRaisesRegex(PackagingError, "embedded project version"):
                package_release(
                    build,
                    merged,
                    root / "dist",
                    "v0.1.0",
                    "0123456789abcdef0123456789abcdef01234567",
                    "ESP-IDF v6.1-dev",
                )

    def test_rejects_unexpected_target(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build, merged = self.make_build(root, a_chip="esp32")
            with self.assertRaisesRegex(PackagingError, "expected target esp32s3"):
                package_release(
                    build,
                    merged,
                    root / "dist",
                    "v0.1.0",
                    "0123456789abcdef0123456789abcdef01234567",
                    "ESP-IDF v6.1-dev",
                )

    def test_rejects_unexpected_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build, merged = self.make_build(root)
            metadata_path = build / "flasher_args.json"
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            metadata["flash_files"]["0x20000"] = metadata["flash_files"].pop("0x10000")
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
            with self.assertRaisesRegex(PackagingError, "unexpected flash layout"):
                package_release(
                    build,
                    merged,
                    root / "dist",
                    "v0.1.0",
                    "0123456789abcdef0123456789abcdef01234567",
                    "ESP-IDF v6.1-dev",
                )


if __name__ == "__main__":
    unittest.main()
