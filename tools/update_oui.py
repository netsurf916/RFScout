#!/usr/bin/env python3
"""Generate RFScout's compiled IEEE MAC-assignment vendor tables."""

import csv
import io
import pathlib
import re
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
WIRELESS_SERVICE = ROOT / "main" / "services" / "wireless"
MA_L_OUT = WIRELESS_SERVICE / "oui_database.inc"
EXTENDED_OUT = WIRELESS_SERVICE / "oui_database_extended.inc"
REGISTRIES = {
    "ma_l": ("https://standards-oui.ieee.org/oui/oui.csv", 6),
    "ma_m": ("https://standards-oui.ieee.org/oui28/mam.csv", 7),
    "iab": ("https://standards-oui.ieee.org/iab/iab.csv", 9),
    "ma_s": ("https://standards-oui.ieee.org/oui36/oui36.csv", 9),
}
USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/127.0 Safari/537.36"
)


def clean_brand(value: str) -> str:
    """Normalize an IEEE organization name for the compact LCD UI."""
    value = re.sub(r"[\\\"]", "", value).strip()
    value = re.sub(r"\s+", " ", value)
    for suffix in (
        ", Inc.",
        " Inc.",
        " Incorporated",
        " Corporation",
        " Corp.",
        " LLC",
        " Ltd.",
        " Co.,Ltd.",
    ):
        if value.endswith(suffix):
            value = value[: -len(suffix)]
            break
    return value[:31] or "Unknown"


def download_registry(url: str) -> str:
    """Download one IEEE public registry with bounded retries."""
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "text/csv,text/plain;q=0.9,*/*;q=0.8",
            "Accept-Language": "en-US,en;q=0.9",
        },
    )

    last_error = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=45) as response:
                return response.read().decode("utf-8-sig")
        except (urllib.error.HTTPError, urllib.error.URLError) as exc:
            last_error = exc
            if attempt < 2:
                time.sleep(2**attempt)

    raise RuntimeError(f"Unable to download IEEE registry {url}: {last_error}")


def parse_registry(text: str, assignment_length: int) -> dict[int, str]:
    """Parse one IEEE CSV into an integer-prefix to compact-brand map."""
    records: dict[int, str] = {}
    reader = csv.DictReader(io.StringIO(text))
    expected = {"Registry", "Assignment", "Organization Name", "Organization Address"}
    if reader.fieldnames is None or not expected.issubset(set(reader.fieldnames)):
        raise RuntimeError(f"Unexpected IEEE CSV header: {reader.fieldnames}")

    for row in reader:
        assignment = row.get("Assignment", "").replace("-", "").strip()
        if len(assignment) != assignment_length:
            continue
        records[int(assignment, 16)] = clean_brand(row.get("Organization Name", ""))
    return records


def write_ma_l(records: dict[int, str]) -> None:
    """Write the legacy 24-bit include retained for compact binary-search lookup."""
    if len(records) < 30000:
        raise RuntimeError(f"IEEE MA-L registry unexpectedly small: {len(records)} records")

    lines = [
        "/* Generated from the IEEE public MA-L/OUI registry. */",
        "static const oui_record_t oui_records[] = {",
    ]
    for prefix, brand in sorted(records.items()):
        lines.append(f'    {{ 0x{prefix:06X}, "{brand}" }},')
    lines += [
        "};",
        "#define OUI_RECORD_COUNT ( sizeof( oui_records ) / sizeof( oui_records[ 0 ] ) )",
        "",
    ]
    MA_L_OUT.write_text("\n".join(lines), encoding="utf-8")


def write_extended(ma_m: dict[int, str], iab: dict[int, str], ma_s: dict[int, str]) -> None:
    """Write 28-bit and 36-bit tables; current MA-S wins over legacy IAB on an exact collision."""
    if len(ma_m) < 100 or len(ma_s) < 100:
        raise RuntimeError(
            f"IEEE extended registries unexpectedly small: MA-M={len(ma_m)} MA-S={len(ma_s)}"
        )

    records36 = dict(iab)
    records36.update(ma_s)

    lines = [
        "/* Generated from IEEE MA-M, MA-S/OUI-36, and legacy IAB registries. */",
        "static const oui28_record_t oui28_records[] = {",
    ]
    for prefix, brand in sorted(ma_m.items()):
        lines.append(f'    {{ 0x{prefix:07X}U, "{brand}" }},')
    lines += [
        "};",
        "#define OUI28_RECORD_COUNT ( sizeof( oui28_records ) / sizeof( oui28_records[ 0 ] ) )",
        "",
        "static const oui36_record_t oui36_records[] = {",
    ]
    for prefix, brand in sorted(records36.items()):
        lines.append(f'    {{ 0x{prefix:09X}ULL, "{brand}" }},')
    lines += [
        "};",
        "#define OUI36_RECORD_COUNT ( sizeof( oui36_records ) / sizeof( oui36_records[ 0 ] ) )",
        "",
    ]
    EXTENDED_OUT.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    """Refresh all IEEE MAC-assignment tables in deterministic prefix order."""
    parsed = {}
    for name, (url, assignment_length) in REGISTRIES.items():
        parsed[name] = parse_registry(download_registry(url), assignment_length)

    write_ma_l(parsed["ma_l"])
    write_extended(parsed["ma_m"], parsed["iab"], parsed["ma_s"])
    print(
        "wrote IEEE prefix records: "
        f"MA-L={len(parsed['ma_l'])} MA-M={len(parsed['ma_m'])} "
        f"IAB={len(parsed['iab'])} MA-S={len(parsed['ma_s'])}"
    )


if __name__ == "__main__":
    main()
