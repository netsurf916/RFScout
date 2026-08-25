#!/usr/bin/env python3
"""Check RFScout hand-maintained C sources with the repository clang-format policy."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".h"}
SOURCE_ROOT = ROOT / "main"


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
            run_git("diff", "--name-only", f"{merge_base}...HEAD", "--", "main/*.c", "main/*.h").splitlines()
        )

    paths.update(run_git("diff", "--name-only", "HEAD", "--", "main/*.c", "main/*.h").splitlines())
    paths.update(run_git("diff", "--cached", "--name-only", "--", "main/*.c", "main/*.h").splitlines())

    untracked = run_git("ls-files", "--others", "--exclude-standard", "main/*.c", "main/*.h")
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


def main() -> int:
    """Check selected sources and report files that require clang-format."""
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--all", action="store_true", help="check every tracked main/*.c and main/*.h file")
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

    if failures:
        print("RFScout format check failed for:", file=sys.stderr)
        for path in failures:
            print(f"  {path}", file=sys.stderr)
        print("Run: python3 tools/check_format.py --fix", file=sys.stderr)
        return 1

    print(f"RFScout format check: {len(paths)} file(s) clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
