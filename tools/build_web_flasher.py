#!/usr/bin/env python3
"""Assemble the static RFScout browser-flasher catalog."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


def load_json(a_path: Path) -> object:
    return json.loads(a_path.read_text(encoding="utf-8"))


def write_json(a_path: Path, a_value: object) -> None:
    a_path.write_text(
        json.dumps(a_value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def manifest_version(a_path: Path) -> str:
    value = load_json(a_path)
    if not isinstance(value, dict) or not isinstance(value.get("version"), str):
        raise ValueError(f"invalid ESP Web Tools manifest: {a_path}")
    return value["version"]


def release_entries(a_site_dir: Path, a_releases: list[dict]) -> list[dict]:
    entries: list[dict] = []
    for release in a_releases:
        tag = release.get("tagName")
        if not isinstance(tag, str):
            continue
        directory = a_site_dir / "firmware" / "releases" / tag
        manifest = directory / "manifest.web.json"
        if not manifest.is_file():
            continue
        entries.append(
            {
                "id": f"release-{tag}",
                "kind": "release",
                "label": release.get("name") or f"RFScout {tag}",
                "version": manifest_version(manifest),
                "manifest": f"firmware/releases/{tag}/manifest.web.json",
                "prerelease": bool(release.get("isPrerelease", False)),
                "published_at": release.get("publishedAt"),
            }
        )
    return entries


def candidate_entry(
    a_site_dir: Path,
    a_candidate_dir: Path,
    a_sha: str,
    a_event: str,
    a_pr: str,
    a_run_url: str,
) -> dict | None:
    manifests = sorted(a_candidate_dir.rglob("manifest.web.json"))
    if not manifests:
        return None

    source = manifests[0].parent
    destination = a_site_dir / "firmware" / "candidate"
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)

    short_sha = a_sha[:12] if a_sha else "unknown"
    if a_event == "pull_request" and a_pr:
        label = f"PR #{a_pr} candidate"
        identifier = f"pr-{a_pr}-{short_sha}"
        pr_number: int | None = int(a_pr)
    else:
        label = f"{a_event or 'workflow'} candidate {short_sha}"
        identifier = f"candidate-{short_sha}"
        pr_number = None

    return {
        "id": identifier,
        "kind": "candidate",
        "label": label,
        "version": manifest_version(destination / "manifest.web.json"),
        "manifest": "firmware/candidate/manifest.web.json",
        "commit": a_sha or None,
        "pr": pr_number,
        "run_url": a_run_url or None,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site-dir", type=Path, required=True)
    parser.add_argument("--releases-json", type=Path, required=True)
    parser.add_argument("--candidate-dir", type=Path, required=True)
    parser.add_argument("--candidate-sha", default="")
    parser.add_argument("--candidate-event", default="")
    parser.add_argument("--candidate-pr", default="")
    parser.add_argument("--candidate-run-url", default="")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    releases = load_json(args.releases_json)
    if not isinstance(releases, list):
        raise ValueError("release metadata must be a JSON array")

    entries = release_entries(args.site_dir, releases)
    candidate = candidate_entry(
        args.site_dir,
        args.candidate_dir,
        args.candidate_sha,
        args.candidate_event,
        args.candidate_pr,
        args.candidate_run_url,
    )
    if candidate is not None:
        entries.insert(0, candidate)

    write_json(
        args.site_dir / "catalog.json",
        {
            "schema_version": 1,
            "builds": entries,
        },
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
