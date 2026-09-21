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
                (artifact / "manifest.json").write_text(
                    json.dumps({"commit": (str(pr)[0] * 40)}),
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
                        "created_at": "2026-09-20T12:00:00Z",
                    },
                    {
                        "pr": 52,
                        "sha": "bbbbbbbbbbbb2222",
                        "run_url": "https://github.example/run/52",
                        "created_at": "2026-09-21T12:00:00Z",
                    },
                ],
            )

            self.assertEqual([entry["pr"] for entry in entries], [52, 42])
            self.assertEqual(entries[0]["label"], "PR #52 candidate")
            self.assertEqual(entries[0]["source_commit"], "bbbbbbbbbbbb2222")
            self.assertEqual(entries[0]["created_at"], "2026-09-21T12:00:00Z")
            self.assertEqual(entries[0]["commit"], "5" * 40)
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

    def write_ota_image(
        self,
        a_directory: Path,
        *,
        a_version: str,
        a_commit: str,
        a_contents: bytes = b"application image",
    ) -> Path:
        a_directory.mkdir(parents=True, exist_ok=True)
        image = a_directory / f"rfscout-{a_version}-esp32s3-app.bin"
        image.write_bytes(a_contents)
        digest = hashlib.sha256(image.read_bytes()).hexdigest()
        (a_directory / "ota-image.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "project": "rfscout",
                    "version": a_version,
                    "commit": a_commit,
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
        return image

    def test_ota_catalog_uses_same_release_and_candidate_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            site = Path(temporary) / "site"
            release_dir = site / "firmware" / "releases" / "v0.3.0"
            candidate_dir = site / "firmware" / "candidates" / "pr-75"
            release_commit = "a" * 40
            candidate_commit = "b" * 40
            self.write_ota_image(
                release_dir, a_version="0.3.0", a_commit=release_commit
            )
            self.write_ota_image(
                candidate_dir,
                a_version="candidate-bbbbbbbbbbbb",
                a_commit=candidate_commit,
            )

            browser_entries = [
                {
                    "id": "pr-75-bbbbbbbbbbbb",
                    "kind": "candidate",
                    "source": "pull_request",
                    "pr": 75,
                    "label": "PR #75 candidate",
                    "version": "candidate-bbbbbbbbbbbb",
                    "manifest": "firmware/candidates/pr-75/manifest.web.json",
                    "commit": candidate_commit,
                    "created_at": "2026-09-21T12:00:00Z",
                },
                {
                    "id": "release-v0.3.0",
                    "kind": "release",
                    "label": "RFScout v0.3.0",
                    "version": "0.3.0",
                    "manifest": "firmware/releases/v0.3.0/manifest.web.json",
                    "commit": release_commit,
                    "draft": False,
                    "prerelease": False,
                    "published_at": "2026-09-01T12:00:00Z",
                },
            ]

            catalog = build_ota_catalog(site, browser_entries)

            self.assertEqual(len(catalog["builds"]), 2)
            self.assertEqual(catalog["builds"][0]["label"], "RFScout 0.3.0")
            self.assertEqual(catalog["builds"][1]["pr"], 75)
            self.assertEqual(catalog["builds"][1]["label"], "PR #75 @ bbbbbbbbbbbb")
            self.assertTrue(catalog["builds"][0]["stable"])
            self.assertFalse(catalog["builds"][1]["stable"])
            self.assertEqual(
                catalog["builds"][1]["artifact"]["path"],
                "firmware/candidates/pr-75/"
                "rfscout-candidate-bbbbbbbbbbbb-esp32s3-app.bin",
            )
            catalog_path = site / "ota" / "catalog.json"
            self.assertTrue(catalog_path.is_file())
            self.assertLessEqual(len(catalog_path.read_bytes()), 8192)
            self.assertNotIn(b"\n  ", catalog_path.read_bytes())

    def test_ota_catalog_rejects_bad_artifact_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            site = Path(temporary) / "site"
            directory = site / "firmware" / "releases" / "v0.3.0"
            commit = "a" * 40
            self.write_ota_image(
                directory, a_version="0.3.0", a_commit=commit
            )
            metadata_path = directory / "ota-image.json"
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            metadata["artifact"]["sha256"] = "0" * 64
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                build_ota_catalog(
                    site,
                    [
                        {
                            "id": "release-v0.3.0",
                            "kind": "release",
                            "label": "RFScout v0.3.0",
                            "version": "0.3.0",
                            "manifest": "firmware/releases/v0.3.0/manifest.web.json",
                            "commit": commit,
                            "draft": False,
                            "prerelease": False,
                        }
                    ],
                )

    def test_ota_catalog_rejects_identity_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            site = Path(temporary) / "site"
            directory = site / "firmware" / "releases" / "v0.3.0"
            commit = "a" * 40
            self.write_ota_image(directory, a_version="0.3.0", a_commit=commit)
            metadata_path = directory / "ota-image.json"
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            metadata["partition_layout"] = "legacy"
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "partition_layout"):
                build_ota_catalog(
                    site,
                    [
                        {
                            "id": "release-v0.3.0",
                            "kind": "release",
                            "label": "RFScout v0.3.0",
                            "version": "0.3.0",
                            "manifest": "firmware/releases/v0.3.0/manifest.web.json",
                            "commit": commit,
                            "draft": False,
                            "prerelease": False,
                        }
                    ],
                )

    def test_ota_catalog_trims_to_8k_and_keeps_newest_official(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            site = Path(temporary) / "site"
            entries = []
            for index in range(33):
                version = f"0.{index}.0"
                commit = f"{index:040x}"
                directory = site / "firmware" / "releases" / f"v{version}"
                self.write_ota_image(directory, a_version=version, a_commit=commit)
                entries.append(
                    {
                        "id": f"release-v{version}",
                        "kind": "release",
                        "label": f"RFScout v{version}",
                        "version": version,
                        "manifest": f"firmware/releases/v{version}/manifest.web.json",
                        "commit": commit,
                        "draft": False,
                        "prerelease": False,
                        "published_at": f"2026-09-21T00:{index:02d}:00Z",
                    }
                )

            catalog = build_ota_catalog(site, entries)
            catalog_path = site / "ota" / "catalog.json"

            self.assertLess(len(catalog["builds"]), len(entries))
            self.assertEqual(catalog["builds"][0]["version"], "0.32.0")
            self.assertLessEqual(len(catalog_path.read_bytes()), 8192)


if __name__ == "__main__":
    unittest.main()
