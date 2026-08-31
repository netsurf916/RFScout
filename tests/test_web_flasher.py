"""Tests for the RFScout browser-flasher catalog builder."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.build_web_flasher import candidate_entries, release_entries


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


if __name__ == "__main__":
    unittest.main()
