"""Regression checks for safe default firmware selection and Pages readiness."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_web_flasher import deduplicate, ota_catalog_priority  # noqa: E402


class FirmwarePublicationSafetyTests(unittest.TestCase):
    def test_stable_release_precedes_newer_development_candidates(self):
        entries = [
            {"id": "main", "kind": "candidate", "source": "main", "commit": "a" * 40, "created_at": "2026-10-19"},
            {"id": "draft", "kind": "release", "draft": True, "commit": "b" * 40, "created_at": "2026-10-20"},
            {"id": "stable", "kind": "release", "draft": False, "prerelease": False, "commit": "c" * 40, "published_at": "2026-10-01"},
        ]
        ordered = deduplicate(ota_catalog_priority(entries))
        self.assertEqual(ordered[0]["id"], "stable")
        self.assertEqual({item["id"] for item in ordered}, {"main", "draft", "stable"})

    def test_candidate_remains_available_without_stable_release(self):
        entries = [{"id": "main", "kind": "candidate", "source": "main", "commit": "a" * 40}]
        self.assertEqual(deduplicate(ota_catalog_priority(entries)), entries)

    def test_pages_requires_successful_main_validation(self):
        workflow = (ROOT / ".github" / "workflows" / "web-flasher.yml").read_text()
        self.assertIn('main_success=$(jq', workflow)
        self.assertIn('select(.status == "completed" and .conclusion == "success")', workflow)
        self.assertIn('elif [[ "$main_success" -eq 0 ]]; then', workflow)


if __name__ == "__main__":
    unittest.main()
