#!/usr/bin/env python3
"""Check RFScout hand-maintained C sources with the repository clang-format policy."""

from __future__ import annotations

import argparse
import difflib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".h"}
SOURCE_ROOT = ROOT / "main"

# PR #17 introduced clang-format after a substantial hand-formatting cleanup.
# These exact blobs are accepted as the migration baseline so that the initial
# policy change does not reflow thousands of already-reviewed lines. Once any
# baseline file changes, it must match clang-format exactly. --all deliberately
# ignores this baseline and reports every non-canonical file.
FORMAT_BASELINE_BLOBS = {
    "main/app_logging.c": "097efaf73aad8a44f2be1470211587ca37efb959",
    "main/app_logging.h": "8105af89931a8257bbea54bdd65da04cbb1697ca",
    "main/app_manager.c": "d8086b9d6a401d46d92123dc42c0f77ccb185eb6",
    "main/app_manager.h": "e2aeddba6a41b7da765c940b3aa7dc09a62762f6",
    "main/audio_spectrum.c": "dbabab5fd90852b978c67af6da359be41fff3422",
    "main/audio_spectrum.h": "879168abcfaf943865a092c48b5ecb8c5ea7d158",
    "main/audio_view.c": "65904080e149df29afd505954b7aad2973fd8e3a",
    "main/audio_view.h": "8b27fd1c371c293d11cf3eb77b156c5fb5db6ff7",
    "main/ble_monitor.h": "4390b060523acdf7f15839c816efbcfcf6a3740d",
    "main/cc1101.h": "4a90d98de7e9f1c514cf0b9246969cb962586653",
    "main/cc1101_activity.h": "a9ef367a9674d8ed55e3e181fd594e1ee9e51a1e",
    "main/nrf24.h": "7fd07d5fdc889d40c076afeb40fb2934319a9986",
    "main/nrf24_activity.c": "b02f75802df22d5f32fab7799255384ae7643e01",
    "main/nrf24_activity.h": "bbfd217a3f81f7f5ff699e2a8af18cc19316bcb0",
    "main/radio_app_view.c": "fad8dfe42f3218cc6205dac41cf6b5bc6f47aac7",
    "main/radio_app_view.h": "2ff0a8fb9a52dd81161e6061bd146c7fe340e8b5",
    "main/rfscout.c": "8117b0c7083519522fe4e49742b90914ba0afe48",
}


def run_git(*arguments: str) -> str:
    """Run git in the repository and return stripped stdout."""
    result = subprocess.run(
        ["git", *arguments],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def is_source(path: str) -> bool:
    """Return whether a repository path is a hand-maintained firmware source."""
    candidate = ROOT / path
    return (
        candidate.is_file()
        and candidate.is_relative_to(SOURCE_ROOT)
        and candidate.suffix in SOURCE_SUFFIXES
    )


def all_sources() -> list[str]:
    """Return all tracked hand-maintained C and header files."""
    paths = run_git("ls-files", "main/*.c", "main/*.h").splitlines()
    return sorted(path for path in paths if is_source(path))


def changed_sources(base: str | None) -> list[str]:
    """Return source files changed from a base ref or in the local working tree."""
    paths: set[str] = set()

    if base:
        merge_base = run_git("merge-base", base, "HEAD")
        paths.update(
            run_git(
                "diff",
                "--name-only",
                f"{merge_base}...HEAD",
                "--",
                "main/*.c",
                "main/*.h",
            ).splitlines()
        )

    paths.update(
        run_git("diff", "--name-only", "HEAD", "--", "main/*.c", "main/*.h").splitlines()
    )
    paths.update(
        run_git("diff", "--cached", "--name-only", "--", "main/*.c", "main/*.h").splitlines()
    )

    untracked = run_git(
        "ls-files", "--others", "--exclude-standard", "main/*.c", "main/*.h"
    )
    paths.update(untracked.splitlines())
    return sorted(path for path in paths if is_source(path))


def format_output(clang_format: str, path: str) -> bytes:
    """Return clang-format output for one source file."""
    result = subprocess.run(
        [clang_format, "--style=file", path],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return result.stdout


def current_blob(path: str) -> str:
    """Return the git blob SHA for the current file contents."""
    return run_git("hash-object", path)


def baseline_accepted(path: str) -> bool:
    """Return whether a file still exactly matches the migration baseline."""
    expected = FORMAT_BASELINE_BLOBS.get(path)
    return expected is not None and current_blob(path) == expected


def print_format_diff(path: str, original: bytes, formatted: bytes) -> None:
    """Print a concise unified diff showing clang-format's required changes."""
    original_lines = original.decode("utf-8").splitlines(keepends=True)
    formatted_lines = formatted.decode("utf-8").splitlines(keepends=True)
    sys.stderr.writelines(
        difflib.unified_diff(
            original_lines,
            formatted_lines,
            fromfile=path,
            tofile=f"{path} (clang-format)",
        )
    )


def main() -> int:
    """Check selected sources and report files that require clang-format."""
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument(
        "--all",
        action="store_true",
        help="check every tracked main/*.c and main/*.h file without baseline exemptions",
    )
    selection.add_argument("--base", help="check source files changed relative to this git ref")
    parser.add_argument("--fix", action="store_true", help="rewrite selected files with clang-format")
    arguments = parser.parse_args()

    clang_format = shutil.which("clang-format")
    if clang_format is None:
        print(
            "error: clang-format is required for RFScout formatting checks. "
            "Install clang-format or configure with -DRFSCOUT_FORMAT_CHECK=OFF only for an emergency build.",
            file=sys.stderr,
        )
        return 2

    paths = all_sources() if arguments.all else changed_sources(arguments.base)
    if not paths:
        print("RFScout format check: no changed C/header files to check")
        return 0

    failures: list[str] = []
    baseline_count = 0
    for path in paths:
        if not arguments.all and not arguments.fix and baseline_accepted(path):
            baseline_count += 1
            continue

        formatted = format_output(clang_format, path)
        source_path = ROOT / path
        original = source_path.read_bytes()
        if original == formatted:
            continue

        if arguments.fix:
            source_path.write_bytes(formatted)
            print(f"formatted {path}")
        else:
            failures.append(path)
            print_format_diff(path, original, formatted)

    if failures:
        print("RFScout format check failed for:", file=sys.stderr)
        for path in failures:
            print(f"  {path}", file=sys.stderr)
        print("Run: python3 tools/check_format.py --fix", file=sys.stderr)
        return 1

    suffix = f" ({baseline_count} migration-baseline file(s))" if baseline_count else ""
    print(f"RFScout format check: {len(paths)} file(s) clean{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
