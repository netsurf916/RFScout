# microSD card behavior

This document describes current storage behavior. Future storage/capture ideas are tracked in [ROADMAP.md](ROADMAP.md).

## Compatibility baseline

RFScout uses the microSD/TF interface on the LILYGO T-Embed CC1101 Plus. LILYGO documents SanDisk cards of 32 GB or less as its tested baseline. A 32 GB-or-smaller FAT32-formatted SanDisk card is therefore the conservative known-good choice; other cards and larger capacities may work but are outside that stated baseline.

RFScout uses ESP-IDF's FAT SD-SPI mounting path. Cards commonly shipped as exFAT may require reformatting to a supported FAT filesystem.

## Detection and failure behavior

The current board mapping does not expose a separate SD card-detect GPIO. Storage is considered available only after successful initialization and mount.

Mount failure is nonfatal. Radio/sensor applications continue operating. Recorder remains navigable but recording and playback are unavailable without mounted storage. The global SD status icon appears only after successful mount; filesystem/logging failures are reflected as a sticky error state.

## Configuration persistence

When mounted, `/sdcard/rfscout.json` is the canonical store for persistent user preferences. Current schema-v2 state includes:

- display brightness, clock format, and theme;
- application enablement and preferred startup application;
- SD logging preference;
- global notification mode and volume;
- per-application notification enablement and Morse mappings;
- WiFi / BT ordinary Wi-Fi/BLE discovery-feedback switches;
- Recorder playback volume and microphone input gain;
- timezone definitions/selection;
- saved time metadata when applicable.

Configuration loading starts from current compiled defaults and overlays recognized valid fields. Schema v1 files are accepted and translated where supported. Unknown/obsolete keys are ignored and are pruned on the next canonical save; fields absent from older files receive current defaults. Future schema versions are rejected instead of being rewritten by older firmware.

Configuration writes use temporary/backup replacement so an interrupted save can recover. Dirty configuration is flushed before deep sleep and SD unmount.

The small RTC-backed deep-sleep session record (currently active app and input-lock state) is not stored on the SD card.

## Recorder storage

Recorder uses `/sdcard/audio/`, creating the directory when needed.

RFScout records **16-bit mono PCM WAV**. The PDM configuration currently begins with a 48 kHz request, but Recorder measures the delivered PCM rate at runtime and writes that effective rate into the WAV header rather than assuming the request is exact. Supported file validation/playback accepts 16-bit mono PCM WAV rates from 8 through 48 kHz.

Recorder's save path asynchronously applies the retained-window processing currently used by the application: 300 ms head trim, 600 ms tail trim, DC removal, and bounded normalization. The recording browser is refreshed after save completion. Malformed/unsupported WAV files are ignored; validated recordings can be played or deleted from the UI.

## Unified logging

Applications feed the same asynchronous log writer. Wi-Fi/BLE can emit detailed lifecycle records; application-level logging adds ENTER/EXIT and rate-limited telemetry records.

With valid wall time, records begin with UTC ISO-8601 timestamps, for example:

```text
2026-08-28T18:04:21Z APP RECORDER state=IDLE files=3 bytes=0
```

Before wall time has been established, records use an explicit monotonic marker such as:

```text
UP+123456 APP RECORDER state=IDLE files=3 bytes=0
```

The exact structured log format remains subject to the current log-format roadmap item; consumers should not assume undocumented token positions.

## Logging footprint

The logger uses up to 100 rotating files of 4 MiB each (`rfscout-00.log` through `rfscout-99.log`), for an approximate normal maximum of 400 MiB. RFScout also attempts to preserve at least 64 MiB of free filesystem space.
