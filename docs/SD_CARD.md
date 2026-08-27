# microSD card compatibility

RFScout uses the microSD/TF-card interface provided by the LILYGO T-Embed CC1101 Plus.

## LILYGO compatibility guidance

LILYGO's documentation for the T-Embed CC1101 family notes that its tested microSD configuration is **SanDisk cards with a capacity of 32 GB or less**. LILYGO also notes that some other cards may not work reliably and that the reason for those compatibility differences is not known.

For RFScout, a **32 GB or smaller FAT32-formatted SanDisk microSD card** should therefore be treated as the known-good baseline. Larger cards or cards from other manufacturers may work, but they are outside LILYGO's stated tested configuration and should not be assumed compatible.

In particular, cards larger than 32 GB are commonly supplied formatted as exFAT rather than FAT32. RFScout currently uses ESP-IDF's FAT filesystem SD-SPI mounting path, so filesystem format is another possible source of incompatibility independent of the physical card capacity.

## Detection behavior

The T-Embed CC1101 Plus does not expose a separate microSD card-detect GPIO in the board pin mapping used by RFScout. RFScout therefore considers storage present only after the card successfully initializes and mounts. An installed but incompatible or unmountable card is treated the same as unavailable storage.

Failure to mount an SD card is nonfatal. RFScout continues operating without logging; Recorder remains navigable but cannot record or play files, and the SD status icon is shown only after storage has mounted successfully.

## Configuration persistence

When storage is mounted, device-wide preferences are stored in `/sdcard/rfscout.json`. This includes the global audible-notification mode and each detection category's one-character Morse mapping. A valid saved notification configuration is restored on boot; compiled defaults are used when saved values are unavailable or invalid.

## Recorder storage

Recorder stores supported 16 kHz, 16-bit mono PCM WAV files in `/sdcard/audio/`. The directory is created automatically when mounted storage is available. Only WAV files that validate against the supported PCM format are shown in Recorder's playback list; unrelated or malformed files are ignored.

## Unified logging

All applications feed the same asynchronous SD writer and therefore share the same rotation, queueing, error handling, and free-space policy. Wi-Fi/BLE device lifecycle records retain their detailed `ADDED`/`DROPPED` format. Application-owned records use the form:

`<milliseconds> APP <application> <key=value details>`

Application transitions are recorded as `ENTER` and `EXIT`. While an application is active, a compact telemetry record is queued at most once per second:

- `WIFI_BT`: active observation and surveillance counts. Individual Wi-Fi/BLE device lifecycle records are also logged as they occur.
- `CC1101`: configured frequency range/step, detection count, and radio detection state.
- `NRF24`: configured channel range/dwell, detection count, and radio detection state.
- `RECORDER`: recorder state, validated file count, and active recording byte count.
- `THERMAL`: sensor detection state. More detailed calibrated temperature logging can be added when thermal calibration is implemented.

The one-second application telemetry cadence is intentionally independent of display refresh rate so fast spectrum rendering cannot flood the SD queue or steal time from acquisition tasks.

## RFScout logging footprint

The logger uses up to 100 rotating files of 4 MiB each (`rfscout-00.log` through `rfscout-99.log`), for an approximate maximum normal footprint of 400 MiB. RFScout also attempts to preserve at least 64 MiB of free filesystem space.
