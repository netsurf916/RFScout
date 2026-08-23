#!/usr/bin/env python3
"""Generate RFScout's compiled IEEE MA-L/OUI vendor table."""

import csv
import io
import pathlib
import re
import time
import urllib.error
import urllib.request

URL = "https://standards-oui.ieee.org/oui/oui.csv"
OUT = pathlib.Path(__file__).resolve().parents[1] / "main" / "oui_database.inc"
USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/127.0 Safari/537.36"
)


def clean_brand(value: str) -> str:
    """Normalize an IEEE organization name for the compact LCD UI."""
    value = re.sub(r"[\\\"]", "", value).strip()
    value = re.sub(r"\s+", " ", value)
    for suffix in (
        ", Inc.", " Inc.", " Incorporated", " Corporation", " Corp.",
        " LLC", " Ltd.", " Co.,Ltd."
    ):
        if value.endswith(suffix):
            value = value[:-len(suffix)]
            break
    return value[:31] or "Unknown"


def download_registry() -> str:
    """Download the current IEEE public OUI registry with bounded retries."""
    request = urllib.request.Request(
        URL,
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
                time.sleep(2 ** attempt)

    raise RuntimeError(f"Unable to download IEEE OUI registry: {last_error}")


def main() -> None:
    """Generate main/oui_database.inc in deterministic prefix order."""
    text = download_registry()
    records = {}
    reader = csv.DictReader(io.StringIO(text))
    expected = {"Registry", "Assignment", "Organization Name", "Organization Address"}
    if reader.fieldnames is None or not expected.issubset(set(reader.fieldnames)):
        raise RuntimeError(f"Unexpected IEEE CSV header: {reader.fieldnames}")

    for row in reader:
        assignment = row.get("Assignment", "").replace("-", "").strip()
        if len(assignment) != 6:
            continue
        records[int(assignment, 16)] = clean_brand(row.get("Organization Name", ""))

    if len(records) < 30000:
        raise RuntimeError(f"IEEE registry unexpectedly small: {len(records)} records")

    lines = [
        "/* Generated from the IEEE public OUI registry. */",
        "static const oui_record_t oui_records[] = {",
    ]
    for prefix, brand in sorted(records.items()):
        lines.append(f'    {{ 0x{prefix:06X}, "{brand}" }},')
    lines += [
        "};",
        "#define OUI_RECORD_COUNT ( sizeof( oui_records ) / sizeof( oui_records[0] ) )",
        "",
    ]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {len(records)} OUI records to {OUT}")


if __name__ == "__main__":
    main()
