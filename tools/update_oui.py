#!/usr/bin/env python3
import csv, io, pathlib, re, time, urllib.error, urllib.request
URL = "https://standards-oui.ieee.org/oui/oui.csv"
OUT = pathlib.Path(__file__).resolve().parents[1] / "main" / "oui_database.inc"
UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/127.0 Safari/537.36"
def clean(v):
    v = re.sub(r'[\\"]', '', v).strip(); v = re.sub(r'\s+', ' ', v)
    return (v[:31] or "Unknown")
def main():
    req = urllib.request.Request(URL, headers={"User-Agent": UA, "Accept":"text/csv,text/plain;q=0.9,*/*;q=0.8"})
    err = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=45) as r: text = r.read().decode("utf-8-sig")
            break
        except (urllib.error.HTTPError, urllib.error.URLError) as e:
            err=e; time.sleep(2 ** attempt)
    else: raise RuntimeError(f"Unable to download IEEE registry: {err}")
    records={}
    for row in csv.DictReader(io.StringIO(text)):
        a=row.get("Assignment","").replace("-","").strip()
        if len(a)==6: records[int(a,16)] = clean(row.get("Organization Name",""))
    if len(records)<30000: raise RuntimeError(f"Registry unexpectedly small: {len(records)}")
    lines=["/* Generated from the IEEE public OUI registry. */","static const oui_record_t oui_records[] = {"]
    lines += [f'    {{ 0x{k:06X}, "{v}" }},' for k,v in sorted(records.items())]
    lines += ["};","#define OUI_RECORD_COUNT (sizeof(oui_records) / sizeof(oui_records[0]))",""]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {len(records)} OUI records")
if __name__ == "__main__": main()
