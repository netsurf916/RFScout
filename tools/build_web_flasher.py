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


def candidate_entries(
    a_site_dir: Path, a_candidate_root: Path, a_candidates: list[dict]
) -> list[dict]:
    entries: list[dict] = []
    for candidate in a_candidates:
        pr = candidate.get("pr")
        sha = candidate.get("sha")
        run_url = candidate.get("run_url")
        if not isinstance(pr, int) or pr <= 0 or not isinstance(sha, str) or not sha:
            continue

        source_root = a_candidate_root / f"pr-{pr}"
        manifests = sorted(source_root.rglob("manifest.web.json"))
        if not manifests:
            continue

        source = manifests[0].parent
        destination = a_site_dir / "firmware" / "candidates" / f"pr-{pr}"
        if destination.exists():
            shutil.rmtree(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, destination)

        short_sha = sha[:12]
        entries.append(
            {
                "id": f"pr-{pr}-{short_sha}",
                "kind": "candidate",
                "label": f"PR #{pr} candidate",
                "version": manifest_version(destination / "manifest.web.json"),
                "manifest": f"firmware/candidates/pr-{pr}/manifest.web.json",
                "commit": sha,
                "pr": pr,
                "run_url": run_url if isinstance(run_url, str) and run_url else None,
            }
        )

    return sorted(entries, key=lambda entry: entry["pr"], reverse=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site-dir", type=Path, required=True)
    parser.add_argument("--releases-json", type=Path, required=True)
    parser.add_argument("--candidate-root", type=Path, required=True)
    parser.add_argument("--candidates-json", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    releases = load_json(args.releases_json)
    if not isinstance(releases, list):
        raise ValueError("release metadata must be a JSON array")

    candidates = load_json(args.candidates_json)
    if not isinstance(candidates, list):
        raise ValueError("candidate metadata must be a JSON array")

    entries = candidate_entries(args.site_dir, args.candidate_root, candidates)
    entries.extend(release_entries(args.site_dir, releases))

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
