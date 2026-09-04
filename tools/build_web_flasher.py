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


def packaged_commit(a_path: Path) -> str | None:
    manifest = a_path.parent / "manifest.json"
    if not manifest.is_file():
        return None
    value = load_json(manifest)
    commit = value.get("commit") if isinstance(value, dict) else None
    return commit.lower() if isinstance(commit, str) and commit else None


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
        draft = bool(release.get("isDraft", False))
        label = release.get("name") or f"RFScout {tag}"
        if draft:
            label = f"{label} (draft)"
        entries.append(
            {
                "id": f"release-{tag}",
                "kind": "release",
                "label": label,
                "version": manifest_version(manifest),
                "manifest": f"firmware/releases/{tag}/manifest.web.json",
                "commit": packaged_commit(manifest),
                "draft": draft,
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
        source_kind = candidate.get("source", "pull_request")
        pr = candidate.get("pr")
        sha = candidate.get("sha")
        run_url = candidate.get("run_url")
        if not isinstance(sha, str) or not sha:
            continue
        if source_kind == "pull_request":
            if not isinstance(pr, int) or pr <= 0:
                continue
            source_name = f"pr-{pr}"
            label = f"PR #{pr} candidate"
        elif source_kind == "main":
            source_name = "main"
            label = f"Development — main @ {sha[:12]}"
            pr = None
        else:
            continue

        source_root = a_candidate_root / source_name
        manifests = sorted(source_root.rglob("manifest.web.json"))
        if not manifests:
            continue

        source = manifests[0].parent
        destination = a_site_dir / "firmware" / "candidates" / source_name
        if destination.exists():
            shutil.rmtree(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, destination)

        entry = {
            "id": f"{source_name}-{sha[:12]}",
            "kind": "candidate",
            "source": source_kind,
            "label": label,
            "version": manifest_version(destination / "manifest.web.json"),
            "manifest": f"firmware/candidates/{source_name}/manifest.web.json",
            "commit": sha.lower(),
            "run_url": run_url if isinstance(run_url, str) and run_url else None,
        }
        if pr is not None:
            entry["pr"] = pr
        entries.append(entry)

    return sorted(
        entries,
        key=lambda entry: (entry.get("source") != "main", -(entry.get("pr") or 0)),
    )


def deduplicate(entries: list[dict]) -> list[dict]:
    release_commits = {
        entry["commit"]
        for entry in entries
        if entry.get("kind") == "release" and entry.get("commit")
    }
    return [
        entry
        for entry in entries
        if not (
            entry.get("kind") == "candidate"
            and entry.get("source") == "main"
            and entry.get("commit") in release_commits
        )
    ]


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

    releases_entries = release_entries(args.site_dir, releases)
    candidates_entries = candidate_entries(args.site_dir, args.candidate_root, candidates)
    entries = deduplicate(candidates_entries + releases_entries)

    write_json(args.site_dir / "catalog.json", {"schema_version": 1, "builds": entries})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
