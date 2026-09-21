#!/usr/bin/env python3
"""Assemble the static RFScout GitHub Pages firmware catalogs."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

OTA_HARDWARE_TARGET = "t-embed-cc1101-plus"
OTA_IDF_TARGET = "esp32s3"
OTA_PARTITION_LAYOUT = "ab-v1"
OTA_CATALOG_MAX_BUILDS = 32
OTA_CATALOG_MAX_BYTES = 16384
OTA_BUILD_ID_MAX_LENGTH = 63
OTA_BUILD_LABEL_MAX_LENGTH = 95
OTA_VERSION_MAX_LENGTH = 31
OTA_ARTIFACT_PATH_MAX_LENGTH = 191


def load_json(a_path: Path) -> object:
    return json.loads(a_path.read_text(encoding="utf-8"))


def write_json(a_path: Path, a_value: object) -> None:
    a_path.parent.mkdir(parents=True, exist_ok=True)
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

        web_manifest = destination / "manifest.web.json"
        packaged = packaged_commit(web_manifest)
        if packaged is None:
            raise ValueError(f"candidate is missing packaged commit metadata: {destination}")

        entry = {
            "id": f"{source_name}-{sha[:12]}",
            "kind": "candidate",
            "source": source_kind,
            "label": label,
            "version": manifest_version(web_manifest),
            "manifest": f"firmware/candidates/{source_name}/manifest.web.json",
            "commit": packaged,
            "source_commit": sha.lower(),
            "run_url": run_url if isinstance(run_url, str) and run_url else None,
        }
        if pr is not None:
            entry["pr"] = pr
        entries.append(entry)

    return sorted(
        entries,
        key=lambda entry: (entry.get("source") != "main", -(entry.get("pr") or 0)),
    )


def ota_display_label(a_browser_entry: dict, a_version: str, a_commit: str) -> str:
    kind = a_browser_entry.get("kind")
    if kind == "release":
        suffix = ""
        if bool(a_browser_entry.get("draft", False)):
            suffix = " (draft)"
        elif bool(a_browser_entry.get("prerelease", False)):
            suffix = " (prerelease)"
        return f"RFScout {a_version}{suffix}"

    source = a_browser_entry.get("source")
    source_commit = a_browser_entry.get("source_commit")
    display_commit = (
        source_commit[:12]
        if isinstance(source_commit, str) and source_commit
        else a_commit[:12]
    )
    if source == "main":
        return f"main @ {display_commit}"
    if source == "pull_request":
        pr = a_browser_entry.get("pr")
        if not isinstance(pr, int) or pr <= 0:
            raise ValueError("pull-request OTA build is missing PR number")
        return f"PR #{pr} @ {display_commit}"
    raise ValueError(f"unsupported OTA candidate source: {source!r}")


def file_sha256(a_path: Path) -> str:
    digest = hashlib.sha256()
    with a_path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ota_build_entry(a_site_dir: Path, a_browser_entry: dict) -> dict | None:
    manifest_path = a_browser_entry.get("manifest")
    if not isinstance(manifest_path, str) or not manifest_path:
        raise ValueError("browser catalog entry is missing its manifest path")

    directory = a_site_dir / Path(manifest_path).parent
    metadata_path = directory / "ota-image.json"
    if not metadata_path.is_file():
        if a_browser_entry.get("kind") == "candidate":
            raise ValueError(f"candidate is missing OTA image metadata: {directory}")
        return None

    metadata = load_json(metadata_path)
    if not isinstance(metadata, dict) or metadata.get("schema_version") != 1:
        raise ValueError(f"invalid OTA image metadata: {metadata_path}")

    expected = {
        "project": "rfscout",
        "hardware_target": OTA_HARDWARE_TARGET,
        "idf_target": OTA_IDF_TARGET,
        "partition_layout": OTA_PARTITION_LAYOUT,
    }
    for key, value in expected.items():
        if metadata.get(key) != value:
            raise ValueError(
                f"unexpected OTA {key} in {metadata_path}: {metadata.get(key)!r}"
            )

    version = metadata.get("version")
    commit = metadata.get("commit")
    if version != a_browser_entry.get("version"):
        raise ValueError(
            f"OTA version {version!r} does not match browser catalog "
            f"{a_browser_entry.get('version')!r}: {metadata_path}"
        )
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-fA-F]{40}", commit):
        raise ValueError(f"invalid OTA commit in {metadata_path}")
    browser_commit = a_browser_entry.get("commit")
    if isinstance(browser_commit, str) and browser_commit and commit.lower() != browser_commit.lower():
        raise ValueError(
            f"OTA commit {commit!r} does not match browser catalog {browser_commit!r}: "
            f"{metadata_path}"
        )

    artifact = metadata.get("artifact")
    if not isinstance(artifact, dict):
        raise ValueError(f"missing OTA artifact metadata: {metadata_path}")
    filename = artifact.get("file")
    expected_size = artifact.get("size")
    expected_sha256 = artifact.get("sha256")
    if (
        not isinstance(filename, str)
        or "/" in filename
        or "\\" in filename
        or not isinstance(expected_size, int)
        or expected_size <= 0
        or not isinstance(expected_sha256, str)
        or not re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha256)
    ):
        raise ValueError(f"invalid OTA artifact metadata: {metadata_path}")

    image = directory / filename
    if not image.is_file():
        raise ValueError(f"missing OTA application image: {image}")
    if image.stat().st_size != expected_size:
        raise ValueError(f"OTA application size mismatch: {image}")
    if file_sha256(image).lower() != expected_sha256.lower():
        raise ValueError(f"OTA application SHA-256 mismatch: {image}")

    kind = a_browser_entry.get("kind")
    stable = (
        kind == "release"
        and not bool(a_browser_entry.get("draft", False))
        and not bool(a_browser_entry.get("prerelease", False))
    )
    build_id = a_browser_entry.get("id")
    label = ota_display_label(a_browser_entry, version, commit)
    if (
        not isinstance(build_id, str)
        or not build_id
        or len(build_id.encode("ascii", errors="ignore")) != len(build_id)
        or len(build_id) > OTA_BUILD_ID_MAX_LENGTH
        or not isinstance(version, str)
        or len(version) > OTA_VERSION_MAX_LENGTH
        or len(label) > OTA_BUILD_LABEL_MAX_LENGTH
    ):
        raise ValueError(f"OTA build identity exceeds device bounds: {metadata_path}")

    artifact_path = (Path(manifest_path).parent / filename).as_posix()
    if len(artifact_path) > OTA_ARTIFACT_PATH_MAX_LENGTH:
        raise ValueError(f"OTA artifact path exceeds device bounds: {artifact_path}")

    result = {
        "id": build_id,
        "kind": kind,
        "label": label,
        "version": version,
        "commit": commit.lower(),
        "stable": stable,
        "project": expected["project"],
        "hardware_target": expected["hardware_target"],
        "idf_target": expected["idf_target"],
        "partition_layout": expected["partition_layout"],
        "artifact": {
            "path": artifact_path,
            "size": expected_size,
            "sha256": expected_sha256.lower(),
        },
    }
    for key in ("source", "source_commit", "pr", "draft", "prerelease"):
        if key in a_browser_entry:
            result[key] = a_browser_entry[key]
    return result


def build_ota_catalog(a_site_dir: Path, a_browser_entries: list[dict]) -> dict:
    entries = []
    for browser_entry in a_browser_entries:
        entry = ota_build_entry(a_site_dir, browser_entry)
        if entry is not None:
            entries.append(entry)

    if len(entries) > OTA_CATALOG_MAX_BUILDS:
        raise ValueError(
            f"OTA catalog has {len(entries)} builds; maximum is {OTA_CATALOG_MAX_BUILDS}"
        )

    catalog = {"schema_version": 1, "builds": entries}
    payload = json.dumps(catalog, sort_keys=True, separators=(",", ":")) + "\n"
    encoded = payload.encode("utf-8")
    if len(encoded) > OTA_CATALOG_MAX_BYTES:
        raise ValueError(
            f"OTA catalog is {len(encoded)} bytes; maximum is {OTA_CATALOG_MAX_BYTES}"
        )
    catalog_path = a_site_dir / "ota" / "catalog.json"
    catalog_path.parent.mkdir(parents=True, exist_ok=True)
    catalog_path.write_bytes(encoded)
    return catalog


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
    build_ota_catalog(args.site_dir, entries)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
