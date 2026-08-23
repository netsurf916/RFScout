# RFScout

RFScout is a handheld **RF situational-awareness** project for the LILYGO T-Embed CC1101 Plus.

The project combines the useful ideas from Brandalize and OUI-SPY into one passive nearby-device view:

- identify nearby devices by OUI/vendor when a globally administered MAC permits it;
- maintain a rolling view of recently observed unique devices;
- rank nearby brands on the 320x170 LCD;
- identify narrow, high-confidence surveillance-device signatures;
- provide visual, LED, and Morse-code audio alerts for new surveillance detections;
- expand the same UI to the CC1101, nRF24, NFC, IR, audio, and storage capabilities available on the T-Embed CC1101 Plus.

RFScout is intended as an awareness instrument, not an active network client. Wi-Fi/BLE collection should remain passive wherever the hardware/API permits it.

## Current display

The first LCD view is a dense live summary showing total unique devices, Wi-Fi/BLE counts, current surveillance matches, and the top nearby OUI brands. The display uses the same compact built-in 5x7 font established in OUI-SPY/Brandalize and a native ESP-IDF ST7789 driver rather than adding a large GUI dependency.

Brand rows are cyan during normal activity. A brand becomes yellow when its active-device count is greater than five. In a noisy environment, the threshold rises automatically so no more than three displayed brands are yellow. If multiple brands tie at the adaptive cutoff, tied rows remain cyan rather than being split arbitrarily by ranking position. The baseline and highlight cap are module constants intended to be tuned after field experience.

The LCD, SD card, CC1101, and nRF24 share the board SPI bus. RFScout centralizes those pin definitions in `main/board.h` and keeps all unused chip-select signals deasserted during display operation.

## Planned primary screens

1. **Nearby** — unique devices, brands, RSSI/channel/source and recent activity.
2. **Surveillance** — high-confidence OUI-SPY-style detections and alert history.
3. **RF Activity** — Wi-Fi/BLE plus future CC1101 and nRF24 activity summaries.
4. **Tools** — NFC, IR, logging, settings, mute/alert controls and device information.

## OUI database

RFScout uses the same IEEE MA-L/OUI refresh mechanism as Brandalize. Refresh the compiled vendor table locally with:

```sh
python3 tools/update_oui.py
```

The **Refresh IEEE OUI database** GitHub Action can also be run manually and runs weekly. It regenerates `main/oui_database.inc` and commits it only when the IEEE registry has changed.

The complete registry substantially increases the firmware image size. RFScout therefore configures the T-Embed's 16 MB flash and uses the custom `partitions.csv` layout described below.

## Flash and partition layout

The T-Embed CC1101 Plus has 16 MB of flash. ESP-IDF's default single-app table provides only a 1 MB factory application partition, which is too small once the full IEEE OUI registry is compiled into RFScout.

RFScout uses a custom partition table:

| Partition | Offset | Size | Purpose |
|---|---:|---:|---|
| `nvs` | `0x9000` | 24 KB | Persistent settings |
| `phy_init` | `0xF000` | 4 KB | PHY initialization data |
| `factory` | `0x10000` | 4 MB | RFScout firmware |
| `storage` | `0x410000` | 11.94 MB | Reserved SPIFFS storage for future logs and assets |

The application remains a single factory image with no OTA slots. Adding OTA later will require a different partition layout with two sufficiently large application partitions.

## Coding style

Firmware code follows the `netsurf916/libutils` conventions: readable Allman formatting, `a_` argument prefixes, explicit comparisons, strict header/implementation separation, centralized board definitions, and Doxygen documentation for public interfaces. Generated OUI data is exempt from manual formatting.

See [docs/CODING_STYLE.md](docs/CODING_STYLE.md) for the complete project rules. Existing code should be brought into compliance when it is touched; new code must follow the guide.

## Documentation

See [docs/CAPABILITIES.md](docs/CAPABILITIES.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and [docs/CODING_STYLE.md](docs/CODING_STYLE.md).

## Build

RFScout targets the ESP32-S3 used by the T-Embed CC1101 Plus:

```sh
python3 tools/update_oui.py
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

NimBLE observer support, OPI PSRAM, 16 MB flash, and the custom partition table are enabled by `sdkconfig.defaults`.

After pulling a change to `sdkconfig.defaults` or `partitions.csv`, regenerate the local configuration before building:

```sh
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

## License

MIT
