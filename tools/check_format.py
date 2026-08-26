#!/usr/bin/env python3
"""Check RFScout hand-maintained C sources with the canonical clang-format policy."""

from __future__ import annotations

import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".h"}
SOURCE_ROOT = ROOT / "main"
REQUIRED_CLANG_FORMAT_MAJOR = 18


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
    """Return every tracked hand-maintained C and header file under main/."""
    paths = run_git("ls-files", "main").splitlines()
    return sorted(path for path in paths if is_source(path))


def changed_sources(base: str | None) -> list[str]:
    """Return source files changed from a base ref or in the local working tree."""
    paths: set[str] = set()

    if base:
        merge_base = run_git("merge-base", base, "HEAD")
        paths.update(
            run_git("diff", "--name-only", f"{merge_base}...HEAD", "--", "main").splitlines()
        )

    paths.update(run_git("diff", "--name-only", "HEAD", "--", "main").splitlines())
    paths.update(run_git("diff", "--cached", "--name-only", "--", "main").splitlines())
    paths.update(
        run_git("ls-files", "--others", "--exclude-standard", "main").splitlines()
    )
    return sorted(path for path in paths if is_source(path))


def clang_format_major(clang_format: str) -> int | None:
    """Return the clang-format major version reported by a binary."""
    result = subprocess.run(
        [clang_format, "--version"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    match = re.search(r"clang-format version\s+(\d+)", result.stdout)
    return int(match.group(1)) if match else None


def find_clang_format() -> str | None:
    """Locate clang-format 18, honoring an explicit RFSCOUT_CLANG_FORMAT override."""
    candidates = []
    override = os.environ.get("RFSCOUT_CLANG_FORMAT")
    if override:
        candidates.append(override)
    candidates.extend(["clang-format-18", "clang-format"])

    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved is None:
            continue
        if clang_format_major(resolved) == REQUIRED_CLANG_FORMAT_MAJOR:
            return resolved
    return None


def format_output(clang_format: str, path: str) -> bytes:
    """Return clang-format output for one source file."""
    result = subprocess.run(
        [clang_format, "--style=file", path],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return result.stdout


def print_format_diff(path: str, original: bytes, formatted: bytes) -> None:
    """Print a concise unified diff showing clang-format's required changes."""
    original_lines = original.decode("utf-8").splitlines(keepends=True)
    formatted_lines = formatted.decode("utf-8").splitlines(keepends=True)
    sys.stderr.writelines(
        difflib.unified_diff(
            original_lines,
            formatted_lines,
            fromfile=path,
            tofile=f"{path} (clang-format {REQUIRED_CLANG_FORMAT_MAJOR})",
        )
    )


def main() -> int:
    """Check selected sources and report files that require clang-format."""
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument(
        "--all",
        action="store_true",
        help="check every tracked hand-maintained C/header file under main/",
    )
    selection.add_argument("--base", help="check source files changed relative to this git ref")
    parser.add_argument("--fix", action="store_true", help="rewrite selected files with clang-format")
    arguments = parser.parse_args()

    clang_format = find_clang_format()
    if clang_format is None:
        print(
            "error: RFScout requires clang-format 18 for deterministic formatting. "
            "Install clang-format 18 (Fedora: clang18-tools-extra), or set "
            "RFSCOUT_CLANG_FORMAT to a clang-format 18 binary. "
            "Use -DRFSCOUT_FORMAT_CHECK=OFF only for an emergency build.",
            file=sys.stderr,
        )
        return 2

    paths = all_sources() if arguments.all else changed_sources(arguments.base)
    if not paths:
        print("RFScout format check: no C/header files to check")
        return 0

    failures: list[str] = []
    for path in paths:
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

    print(
        f"RFScout format check: {len(paths)} file(s) clean "
        f"with clang-format {REQUIRED_CLANG_FORMAT_MAJOR}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
