# Prefix identity and surveillance metadata

RFScout resolves globally administered Wi-Fi/BLE MAC addresses against the IEEE Registration Authority assignment registries and uses the same lookup layer to expose optional curated surveillance-prefix evidence to the higher-level classifier.

## IEEE registries

`tools/update_oui.py` downloads and compiles these registries:

| Registry | Prefix | Generated table | Purpose |
| --- | ---: | --- | --- |
| MA-L / OUI | 24 bits | `oui_database.inc` | Traditional large-block vendor assignments |
| MA-M | 28 bits | `oui_database_extended.inc` | Medium-block vendor assignments |
| MA-S / OUI-36 | 36 bits | `oui_database_extended.inc` | Small-block vendor assignments |
| Legacy IAB | 36 bits | `oui_database_extended.inc` | Historical small-block assignments |

Runtime vendor resolution uses longest-prefix precedence: 36-bit, then 28-bit, then 24-bit. On an exact 36-bit collision, current MA-S data takes precedence over legacy IAB data.

CID is intentionally not used for ordinary vendor attribution because CID assignments are not globally unique MAC-address allocations.

IEEE vendor attribution is skipped for multicast and locally administered addresses. The WiFi/BT UI counts privacy/randomized addresses separately as `RANDOM`; a globally administered address that has no IEEE match remains `UNKNOWN`.

## Curated surveillance prefixes

`data/surveillance_signatures.csv` remains a human-curated RFScout dataset. `tools/update_surveillance.py` generates `main/surveillance_signatures.inc`, but that generated table is consumed by `oui_database.c`, not directly by `surveillance.c`.

This distinction is intentional:

- IEEE data answers **who owns this globally assigned address block?**
- RFScout curated metadata answers **does this observed prefix contribute surveillance evidence?**
- `surveillance.c` answers **does the combined evidence cross the detection threshold?**

A curated prefix can therefore carry surveillance significance without being an IEEE vendor assignment. This is required for the field-verified locally administered Flock prefix currently present in the curated dataset.

## Classifier evidence

`oui_database_lookup_info()` returns vendor identity and optional curated surveillance metadata in one result. `surveillance_match()` consumes that prefix evidence alongside independent sources such as:

- advertised BLE names;
- BLE manufacturer/company identifiers;
- BLE service UUID patterns;
- Remote ID protocol evidence;
- future payload/protocol evidence.

Prefix metadata is therefore one evidence source, not a replacement for the multi-signal classifier. Generic camera vendors and broad consumer vendors can retain lower prefix scores while strong product-family prefixes can contribute higher confidence.

## Maintenance rules

When changing IEEE registry support, update together:

- `tools/update_oui.py`;
- `main/oui_database.c/.h`;
- `.github/workflows/update-oui.yml`;
- generator tests in `tests/test_update_oui.py`;
- this document and README if user-visible behavior changes.

When changing curated surveillance prefixes, update `data/surveillance_signatures.csv` and regenerate with `tools/update_surveillance.py`. Do not hand-edit the generated `.inc` table.

When adding a new surveillance signature/classification, also review the notification enum/descriptor registry, the WiFi/BT notification mapping in `rfscout.c`, user-visible signature labels, and documentation.

## Refresh workflow

Run locally:

```sh
python3 tools/update_oui.py
python3 tools/update_surveillance.py
python3 -m unittest discover -s tests -p 'test_*.py'
```

The scheduled **Refresh IEEE OUI database** GitHub Action performs the two generation steps and commits changes to the MA-L, extended IEEE, curated surveillance, and advisory validation outputs.
