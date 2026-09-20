"""Tests for the RFScout browser-flasher catalog builder."""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.build_web_flasher import build_ota_catalog, candidate_entries, release_entries


class WebFlasherCatalogTest(unittest.TestCase):
    def test_release_entries_use_packaged_web_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            site = Path(temporary)
            release_dir = site / "firmware" / "releases" / "v1.2.3"
            release_dir.mkdir(parents=True)
            (release_dir / "manifest.web.json").write_text(
                json.dumps({"name": "RFScout", "version": "1.2.3", "builds": []}),
                encoding="utf-8",
            )

            entries = release_entries(
                site,
                [
                    {
                        "tagName": "v1.2.3",
                        "name": "RFScout v1.2.3",
                        "isPrerelease": False,
                        "publishedAt": "2026-08-28T00:00:00Z",
                    }
                ],
            )

            self.assertEqual(len(entries), 1)
            self.assertEqual(entries[0]["version"], "1.2.3")
            self.assertEqual(
                entries[0]["manifest"],
                "firmware/releases/v1.2.3/manifest.web.json",
            )

    def test_candidate_entries_preserve_multiple_pull_requests(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            site = root / "site"
            candidates = root / "candidates"

            for pr, version in ((42, "candidate-aaaa"), (52, "candidate-bbbb")):
                artifact = candidates / f"pr-{pr}" / "artifact"
                artifact.mkdir(parents=True)
                (artifact / "manifest.web.json").write_text(
                    json.dumps({"name": "RFScout", "version": version, "builds": []}),
                    encoding="utf-8",
                )
                (artifact / "rfscout-candidate-full.bin").write_bytes(b"firmware")

            entries = candidate_entries(
                site,
                candidates,
                [
                    {
                        "pr": 42,
                        "sha": "aaaaaaaaaaaa1111",
                        "run_url": "https://github.example/run/42",
                    },
                    {
                        "pr": 52,
                        "sha": "bbbbbbbbbbbb2222",
                        "run_url": "https://github.example/run/52",
                    },
                ],
            )

            self.assertEqual([entry["pr"] for entry in entries], [52, 42])
            self.assertEqual(entries[0]["label"], "PR #52 candidate")
            self.assertEqual(
                entries[0]["manifest"],
                "firmware/candidates/pr-52/manifest.web.json",
            )
            self.assertTrue(
                (site / "firmware" / "candidates" / "pr-42" / "rfscout-candidate-full.bin").is_file()
            )
            self.assertTrue(
                (site / "firmware" / "candidates" / "pr-52" / "rfscout-candidate-full.bin").is_file()
            )

    def test_candidate_entries_skip_missing_or_invalid_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            entries = candidate_entries(
                root / "site",
                root / "candidates",
                [
                    {"pr": 12, "sha": "123456789abc", "run_url": ""},
                    {"pr": 0, "sha": "bad"},
                    {"pr": 13, "sha": ""},
                ],
            )
            self.assertEqual(entries, [])


    def test_ota_catalog_builds_published_release_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            site = root / "site"
            staging = root / "ota-staging"
            release = staging / "v0.5.0"
            release.mkdir(parents=True)
            image = release / "rfscout-v0.5.0-esp32s3-app.bin"
            image.write_bytes(b"application image")
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            (release / "ota-release.json").write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "project": "RFScout",
                        "version": "0.5.0",
                        "minimum_source_version": "0.4.0",
                        "hardware_target": "t-embed-cc1101-plus",
                        "idf_target": "esp32s3",
                        "partition_layout": "ab-v1",
                        "artifact": {
                            "file": image.name,
                            "size": image.stat().st_size,
                            "sha256": digest,
                        },
                    }
                ),
                encoding="utf-8",
            )

            catalog = build_ota_catalog(site, staging)

            self.assertEqual(catalog["latest"], "0.5.0")
            self.assertEqual(catalog["releases"][0]["minimum_source_version"], "0.4.0")
            self.assertEqual(
                catalog["releases"][0]["artifact"]["path"],
                "releases/0.5.0/rfscout-v0.5.0-esp32s3-app.bin",
            )
            self.assertTrue(
                (site / "ota" / "releases" / "0.5.0" / image.name).is_file()
            )
            self.assertTrue((site / "ota" / "stable.json").is_file())

    def test_ota_catalog_rejects_bad_artifact_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            site = root / "site"
            release = root / "ota-staging" / "v0.5.0"
            release.mkdir(parents=True)
            image = release / "rfscout-v0.5.0-esp32s3-app.bin"
            image.write_bytes(b"application image")
            (release / "ota-release.json").write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "project": "RFScout",
                        "version": "0.5.0",
                        "minimum_source_version": "0.4.0",
                        "hardware_target": "t-embed-cc1101-plus",
                        "idf_target": "esp32s3",
                        "partition_layout": "ab-v1",
                        "artifact": {
                            "file": image.name,
                            "size": image.stat().st_size,
                            "sha256": "0" * 64,
                        },
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                build_ota_catalog(site, root / "ota-staging")


if __name__ == "__main__":
    unittest.main()
