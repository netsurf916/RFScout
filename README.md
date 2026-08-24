# RFScout

RFScout is a handheld **RF situational-awareness** project for the LILYGO T-Embed CC1101 Plus.

<p align="center">
  <img src="assets/device.jpg" alt="RFScout running on a LILYGO T-Embed CC1101 Plus" width="800">
</p>

<p align="center"><em>RFScout running on real T-Embed CC1101 Plus hardware with live nearby-device and surveillance detection.</em></p>

The project combines the useful ideas from Brandalize and OUI-SPY into one passive nearby-device view:

- identify nearby devices by OUI/vendor when a globally administered MAC permits it;
- maintain a rolling view of recently observed unique devices;
- rank nearby brands on the 320x170 LCD;
- identify narrow, high-confidence surveillance-device signatures;
- provide visual, LED, and Morse-code audio alerts for new surveillance detections;
- expand the same UI to the CC1101, nRF24, NFC, IR, audio, and storage capabilities available on the T-Embed CC1101 Plus.

RFScout is intended as an awareness instrument, not an active network client. Wi-Fi/BLE collection should remain passive wherever the hardware/API permits it.

## Current display

The first LCD view is a dense live summary showing total unique devices, Wi-Fi/BLE counts, retained surveillance detections, and the highest-count nearby OUI brands. The display uses the same compact built-in 5x7 font established in OUI-SPY/Brandalize and a native ESP-IDF ST7789 driver rather than adding a large GUI dependency.

The title row contains `RFSCOUT` on the left, a compact battery indicator on the right, and a dedicated status-bar region between them. Status icons use the same subdued visual language as the normal battery indicator and populate from right to left, keeping transient/system state close to the battery while preserving clear space beside the project title. Subsystems own their state while the isolated `status_bar.c/.h` renderer decides how that state appears. An SD-card icon is the first status item: it appears in subdued teal whenever the card is mounted and turns red if the logging path encounters a write, flush, sync, close, rotation, or other filesystem error. Logging I/O errors are sticky for the current runtime so an intermittent media problem remains visible rather than disappearing before the user notices it.

The lower display is divided into a brand column and a surveillance column. The surveillance column contains a fixed list of supported type/brand signatures. Zero-count rows remain gray and detections turn red, so supported capabilities remain visible even in a quiet environment.

The brand column reserves its bottom row for an exact `UNKNOWN` count. The nine rows above it contain the highest-count resolved vendors. `UNKNOWN` includes every active observation for which RFScout has no defensible vendor attribution, including private/randomized MAC addresses and globally administered addresses that are absent from the current registry. The fixed row remains visible even when the count is zero, so unresolved devices are never silently omitted from the live summary.

The battery indicator combines an exact percentage with a proportionally filled battery body. Normal charge levels use a subdued teal rather than full-intensity cyan, and charge-complete on external power uses a muted green rather than the dominant full-brightness green used elsewhere in the UI. Low charge remains amber or red so urgency is still obvious. While charging, a bright yellow lightning bolt is drawn over a small dark backing patch inside the battery body, keeping it visible regardless of the current fill color. Unavailable telemetry is shown as a gray `--%` and empty outline.

Battery support is isolated in `battery.c/.h`, with shared board-bus ownership in `board_i2c.c/.h` and all battery rendering contained in `battery_indicator.c/.h`. The BQ27220 fuel gauge supplies state of charge, voltage, and current; the BQ25896 charger supplies external-power and charging state. Telemetry is polled once per second and cached, while the indicator consumes only the public `battery_status_t` interface. Missing power-management devices are nonfatal and do not prevent RFScout from starting.

Ordinary nearby-device and brand observations expire after 20 seconds without another sighting. Surveillance detections use a separate fixed history store with a three-times multiplier, so they remain visible for 60 seconds after their most recent packet. The surveillance counter and category list use that same retained window, while the normal device, Wi-Fi, BLE, and brand counts continue to represent the shorter live RF window. Both stores use fixed-capacity arrays and swap removal, avoiding runtime allocation churn.

Brand rows are cyan during normal activity. A brand is eligible for yellow highlighting when its active-device count is greater than five, but only the first three eligible rows in the descending ranking are highlighted. The threshold remains fixed rather than changing with environmental density, so the visual meaning of yellow stays consistent while the row cap prevents a busy screen from becoming entirely yellow. The threshold and row cap are module constants intended to be tuned after field experience.

RFScout stores canonical RGB565 values such as `0x07E0` for green and `0xFFE0` for yellow. The ST7789 is configured for little-endian 16-bit pixel data and RGB element order so those native `uint16_t` framebuffer values can be transferred directly. This differs from the earlier T-Dongle-C5 path, which used manually byte-swapped color constants.

The LCD, SD card, CC1101, and nRF24 share the board SPI bus. RFScout centralizes those pin definitions in `main/board.h` and keeps all unused chip-select signals deasserted during display operation. Board I2C is likewise centralized so the power-management and future NFC features can share one controller without duplicating bus initialization.

## SD logging

When an SD card is present, RFScout mounts it at `/sdcard` and writes normalized passive observations to `/sdcard/logs`. SD-card failure is nonfatal; the rest of RFScout continues operating when no card is installed or mounting fails. A mounted card is reflected by the SD icon in the top status bar; a logging/filesystem failure changes that icon to red for the remainder of the runtime.

LILYGO documents **SanDisk microSD cards of 32 GB or less** as the tested configuration for the T-Embed CC1101 family and notes that some other cards may not work reliably. RFScout therefore treats a 32 GB or smaller FAT32-formatted SanDisk card as the known-good baseline; larger cards and other manufacturers may work but are outside LILYGO's stated tested configuration. See [docs/SD_CARD.md](docs/SD_CARD.md) for compatibility and detection details.

Logging uses a dedicated low-priority FreeRTOS task and a fixed queue. RF callbacks only copy a compact observation into that queue and never perform filesystem I/O, keeping SD-card write latency and FAT metadata stalls away from packet collection. The writer flushes the stdio buffer approximately once per second and performs a full flush/sync/close when a segment rotates or RFScout enters sleep or powers off.

Logs use a bounded two-digit circular ring:

```text
rfscout-00.log
rfscout-01.log
...
rfscout-99.log
```

Each segment is capped at 4 MiB, limiting the normal log footprint to approximately 400 MiB. A monotonically increasing sequence marker in each file header lets RFScout reconstruct the newest and oldest segments after an unexpected reset without maintaining a separate index file. When `99` is followed by `00`, the destination slot is replaced. RFScout also attempts to preserve at least 64 MiB of free filesystem space by deleting the oldest RFScout log segments before opening a new one when necessary.

The initial format is deliberately human-readable text because logging is expected to evolve into more specialized output later. Each record includes uptime, RF source, RSSI, channel, MAC address, resolved identity, surveillance type/brand, confidence score, and evidence sources. Queue overflow is counted rather than blocking RF collection.

Storage ownership is isolated in `storage.c/.h`; asynchronous file rotation and serialization are isolated in `log_writer.c/.h`. Future packet captures or structured data exports should reuse the storage layer rather than adding filesystem operations to radio collectors.

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

See [docs/CAPABILITIES.md](docs/CAPABILITIES.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/CODING_STYLE.md](docs/CODING_STYLE.md), and [docs/SD_CARD.md](docs/SD_CARD.md).

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
