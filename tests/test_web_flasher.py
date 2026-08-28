"""Tests for the RFScout browser-flasher catalog builder."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.build_web_flasher import candidate_entry, release_entries


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

    def test_candidate_entry_copies_bundle_and_labels_pr(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            site = root / "site"
            candidate = root / "candidate" / "artifact"
            candidate.mkdir(parents=True)
            (candidate / "manifest.web.json").write_text(
                json.dumps(
                    {
                        "name": "RFScout",
                        "version": "candidate-123456789abc",
                        "builds": [],
                    }
                ),
                encoding="utf-8",
            )
            (candidate / "rfscout-candidate-full.bin").write_bytes(b"firmware")

            entry = candidate_entry(
                site,
                root / "candidate",
                "123456789abcdef0",
                "pull_request",
                "42",
                "https://github.example/run/1",
            )

            self.assertIsNotNone(entry)
            self.assertEqual(entry["label"], "PR #42 candidate")
            self.assertEqual(entry["pr"], 42)
            self.assertTrue(
                (site / "firmware" / "candidate" / "rfscout-candidate-full.bin").is_file()
            )

    def test_candidate_entry_is_optional(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            entry = candidate_entry(
                root / "site",
                root / "candidate",
                "",
                "",
                "",
                "",
            )
            self.assertIsNone(entry)


if __name__ == "__main__":
    unittest.main()
