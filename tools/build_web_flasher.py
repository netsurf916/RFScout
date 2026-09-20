#!/usr/bin/env python3
"""Assemble the static RFScout GitHub Pages firmware catalogs."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

OTA_VERSION_RE = re.compile(
    r"^(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)$"
)
OTA_HARDWARE_TARGET = "t-embed-cc1101-plus"
OTA_IDF_TARGET = "esp32s3"
OTA_PARTITION_LAYOUT = "ab-v1"


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




def file_sha256(a_path: Path) -> str:
    digest = hashlib.sha256()
    with a_path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def semver_key(a_version: str) -> tuple[int, int, int]:
    if not OTA_VERSION_RE.fullmatch(a_version):
        raise ValueError(f"invalid stable OTA version: {a_version!r}")
    return tuple(int(part) for part in a_version.split("."))


def ota_release_entry(a_directory: Path) -> dict:
    metadata_path = a_directory / "ota-release.json"
    metadata = load_json(metadata_path)
    if not isinstance(metadata, dict) or metadata.get("schema_version") != 1:
        raise ValueError(f"invalid OTA release metadata: {metadata_path}")

    version = metadata.get("version")
    minimum = metadata.get("minimum_source_version")
    if not isinstance(version, str) or not isinstance(minimum, str):
        raise ValueError(f"missing OTA release version/waypoint metadata: {metadata_path}")
    version_key = semver_key(version)
    minimum_key = semver_key(minimum)
    if minimum_key > version_key:
        raise ValueError(
            f"minimum source {minimum} is newer than OTA release {version}: {metadata_path}"
        )

    expected = {
        "project": "RFScout",
        "hardware_target": OTA_HARDWARE_TARGET,
        "idf_target": OTA_IDF_TARGET,
        "partition_layout": OTA_PARTITION_LAYOUT,
    }
    for key, value in expected.items():
        if metadata.get(key) != value:
            raise ValueError(
                f"unexpected OTA {key} in {metadata_path}: {metadata.get(key)!r}"
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

    image = a_directory / filename
    if not image.is_file():
        raise ValueError(f"missing OTA application image: {image}")
    if image.stat().st_size != expected_size:
        raise ValueError(f"OTA application size mismatch: {image}")
    if file_sha256(image).lower() != expected_sha256.lower():
        raise ValueError(f"OTA application SHA-256 mismatch: {image}")

    return {
        "version": version,
        "minimum_source_version": minimum,
        "hardware_target": OTA_HARDWARE_TARGET,
        "idf_target": OTA_IDF_TARGET,
        "partition_layout": OTA_PARTITION_LAYOUT,
        "artifact": {
            "file": filename,
            "size": expected_size,
            "sha256": expected_sha256.lower(),
        },
    }


def build_ota_catalog(a_site_dir: Path, a_ota_release_root: Path) -> dict:
    output_root = a_site_dir / "ota"
    if output_root.exists():
        shutil.rmtree(output_root)
    releases_output = output_root / "releases"
    releases_output.mkdir(parents=True)

    entries: list[dict] = []
    if a_ota_release_root.is_dir():
        for directory in sorted(
            path for path in a_ota_release_root.iterdir() if path.is_dir()
        ):
            metadata_path = directory / "ota-release.json"
            if not metadata_path.is_file():
                continue
            entry = ota_release_entry(directory)
            version = entry["version"]
            destination = releases_output / version
            if destination.exists():
                raise ValueError(f"duplicate stable OTA version: {version}")
            destination.mkdir()
            artifact = entry["artifact"]
            shutil.copyfile(
                directory / artifact["file"], destination / artifact["file"]
            )
            shutil.copyfile(metadata_path, destination / "ota-release.json")
            entries.append(
                {
                    "version": version,
                    "minimum_source_version": entry["minimum_source_version"],
                    "hardware_target": entry["hardware_target"],
                    "idf_target": entry["idf_target"],
                    "partition_layout": entry["partition_layout"],
                    "artifact": {
                        "path": f"releases/{version}/{artifact['file']}",
                        "size": artifact["size"],
                        "sha256": artifact["sha256"],
                    },
                }
            )

    entries.sort(key=lambda entry: semver_key(entry["version"]))
    catalog = {
        "schema_version": 1,
        "channel": "stable",
        "latest": entries[-1]["version"] if entries else "0.0.0",
        "releases": entries,
    }
    write_json(output_root / "stable.json", catalog)
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
    parser.add_argument("--ota-release-root", type=Path, required=True)
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
    build_ota_catalog(args.site_dir, args.ota_release_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
