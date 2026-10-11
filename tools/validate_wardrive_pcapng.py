#!/usr/bin/env python3
"""Validate Wardriving PCAPNG framing and interface/link types (not RF fidelity)."""
from __future__ import annotations

import argparse
import mmap
from pathlib import Path
import struct
import sys

SHB = 0x0A0D0D0A
IDB = 1
EPB = 6
LINKTYPE_RADIOTAP = 127
LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR = 201


def validate(path: Path) -> dict[str, int]:
    stats = {"sections": 0, "interfaces": 0, "wifi": 0, "ble": 0, "gps": 0}
    with path.open("rb") as fp:
        if path.stat().st_size == 0:
            raise ValueError("Empty capture file")
        with mmap.mmap(fp.fileno(), 0, access=mmap.ACCESS_READ) as data:
            pos = 0
            links: list[tuple[int, int]] = []
            while pos < len(data):
                if len(data) - pos < 12:
                    raise ValueError(f"Trailing incomplete block at offset {pos}")
                block_type, total = struct.unpack_from("<II", data, pos)
                if total < 12 or total % 4 or pos + total > len(data):
                    raise ValueError(f"Invalid block length {total} at offset {pos}")
                trailer = struct.unpack_from("<I", data, pos + total - 4)[0]
                if trailer != total:
                    raise ValueError(f"Block trailer mismatch at offset {pos}")
                if block_type == SHB:
                    if total < 28 or struct.unpack_from("<I", data, pos + 8)[0] != 0x1A2B3C4D:
                        raise ValueError(f"Unsupported PCAPNG section at {pos}")
                    links.clear()
                    stats["sections"] += 1
                elif block_type == IDB:
                    if total < 20:
                        raise ValueError(f"Truncated interface block at {pos}")
                    linktype, snaplen = struct.unpack_from("<HxxI", data, pos + 8)
                    if linktype not in (LINKTYPE_RADIOTAP, LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR):
                        raise ValueError(f"Unexpected interface link type {linktype}")
                    links.append((linktype, snaplen))
                    stats["interfaces"] += 1
                elif block_type == EPB:
                    if total < 32:
                        raise ValueError(f"Truncated packet block at {pos}")
                    iid, _, _, captured, original = struct.unpack_from("<IIIII", data, pos + 8)
                    if iid >= len(links) or original < captured:
                        raise ValueError(f"Invalid interface/length at packet offset {pos}")
                    link, snaplen = links[iid]
                    if (snaplen and captured > snaplen) or captured > total - 32:
                        raise ValueError(f"Packet exceeds snaplen or block size at {pos}")
                    frame = data[pos + 28 : pos + 28 + captured]
                    if link == LINKTYPE_RADIOTAP:
                        if len(frame) < 13 or frame[0] != 0:
                            raise ValueError(f"Invalid Radiotap header at {pos}")
                        rt_length = struct.unpack_from("<H", frame, 2)[0]
                        if rt_length < 13 or rt_length >= len(frame):
                            raise ValueError(f"Invalid Radiotap length at {pos}")
                        stats["wifi"] += 1
                    else:
                        if len(frame) < 19 or frame[4:6] != b"\\x04\\x3e":
                            raise ValueError(f"Invalid BLE HCI event at {pos}")
                        if frame[6] + 7 != len(frame):
                            raise ValueError(f"Inconsistent HCI advertising-event length at {pos}")
                        stats["ble"] += 1
                    opt = pos + 28 + ((captured + 3) & ~3)
                    end_options = pos + total - 4
                    while opt + 4 <= end_options:
                        code, length = struct.unpack_from("<HH", data, opt)
                        opt += 4
                        if code == 0:
                            break
                        if opt + length > end_options:
                            raise ValueError(f"Invalid packet option at {pos}")
                        if code == 1 and data[opt:opt + length].startswith(b"GPS lat="):
                            stats["gps"] += 1
                        opt += (length + 3) & ~3
                pos += total
            if not stats["sections"] or not stats["interfaces"]:
                raise ValueError("Capture is missing a section or interfaces")
    return stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", type=Path, nargs="+", help="Wardriving .pcapng segment(s)")
    args = parser.parse_args()
    failures = 0
    for path in args.files:
        try:
            result = validate(path)
            print(f"{path}: PASS " + " ".join(f"{k}={v}" for k, v in result.items()))
        except (OSError, ValueError, struct.error) as exc:
            print(f"{path}: FAIL {exc}", file=sys.stderr)
            failures += 1
    return int(failures > 0)


if __name__ == "__main__":
    raise SystemExit(main())
