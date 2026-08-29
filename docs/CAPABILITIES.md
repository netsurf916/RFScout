# RFScout capabilities

This document describes capabilities implemented on current `main`. Planned features and design ideas belong in [ROADMAP.md](ROADMAP.md).

## Supported hardware and application order

The currently supported board is **LILYGO T-Embed CC1101 Plus**. RFScout now uses a board-target/capability architecture intended to support additional boards without turning each firmware image into a universal bundle of all board drivers.

RFScout starts on **Home** by default. Top-level navigation order is:

1. WiFi / BT
2. CC1101
3. nRF24
4. Recorder
5. Thermal, when enabled and detected
6. Home / Device Settings

Disabled applications are skipped. Home/Device is always available. Device Settings can select any listed application as the preferred startup target; a disabled or unavailable target falls back to Home for the current boot without erasing the saved preference.

The application layer consumes board-neutral semantic input through `platform_input_get()` rather than directly depending on a rotary encoder.

## WiFi / BT

Implemented behavior includes passive Wi-Fi and BLE observation, rolling recent-device state, IEEE vendor attribution using MA-L/MA-M/MA-S/IAB longest-prefix matching, explicit `RANDOM` handling for private/local addresses, explicit `UNKNOWN` handling for unresolved public addresses, surveillance-oriented multi-signal classification, retained surveillance history, and `RESET DETECTIONS`.

Ordinary newly observed Wi-Fi and BLE devices can produce lightweight general detection feedback. Wi-Fi and BLE discovery cues are independently configurable. Classified surveillance observations use application-specific notification categories instead and do not also generate the ordinary client-discovery cue.

WiFi / BT owns its notification settings: a master notification gate, the two ordinary discovery switches, and Morse mappings for its surveillance categories. Leaving WiFi / BT stops the Wi-Fi and BLE collectors.

## CC1101

Implemented behavior includes direct-RSSI sub-GHz sweeps, configurable profiles/range/step, optional slow peak hold, a 128-sample sweep limit, conservative RF-shape detection categories, app-owned audible notifications when counts advance, and `RESET COUNTS`.

The CC1101 settings menu includes its notification page. The categories describe observed RF behavior and are not protocol/product decodes.

## nRF24

Implemented behavior includes configurable channel bounds and dwell time, fast activity history, optional slow peak hold, passive Enhanced ShockBurst classification when validated observations exist, address-based count deduplication, conservative detection categories, app-owned notifications, and `RESET COUNTS`.

The nRF24 settings menu includes its notification page. Spectrum-only activity that does not support a specific device identity remains broadly categorized.

## Recorder

Recorder replaces the former standalone Audio spectrum analyzer.

Implemented behavior includes:

- 16-bit mono PCM WAV recording under `/sdcard/audio/`;
- SD-required recording and playback with nonfatal `NO SD CARD` behavior;
- automatic recording-directory creation;
- asynchronous capture and save processing outside the UI loop;
- runtime PDM-clock measurement and delivered-PCM-rate measurement;
- WAV metadata based on the measured effective PCM rate;
- 300 ms head and 600 ms tail trimming, DC removal, and bounded normalization;
- validated recording enumeration, duration/size metadata, newest-first ordering, deletion, and immediate list refresh after save;
- asynchronous WAV playback through I2S1;
- Recorder-specific persistent playback volume independent of notification volume;
- a `MONITOR` mode with live microphone FFT display;
- persistent fixed microphone input gain from 0 through +24 dB in 3 dB steps.

Recorder has a native audio-centric policy: incidental interaction/general-event feedback is suppressed while intentional content audio remains available. The source tree retains the historical `audio_spectrum` compatibility interface because the application enum/navigation ABI still uses `RFSCOUT_APP_AUDIO`.

## Thermal

Thermal is available when an MLX90640 is detected on the external I2C/Qwiic interface and the application is enabled. The sensor's native 32x24 frame is interpolated for the LCD. Thermal currently has no discrete detection classifier. Its labels and chrome follow the active display theme while the thermal image itself retains its literal sensor palette.

## Home / Device Settings

Home is a branded pass-through landing screen backed by the non-disableable Device application. Rotation continues top-level navigation; center enters Device Settings.

Implemented Device Settings include display brightness, selectable display theme, 12/24-hour clock presentation, timezone selection, preferred startup application, manual date/time, per-application enablement, SD logging, global notification mode, global notification volume, and explicit configuration save.

App-specific notification enablement and category mappings are configured from the detecting applications rather than centrally in Device Settings.

Current themes are **Indoor**, **Outdoor**, **Night**, and **Discipline**. Discipline uses the Night visual palette and additionally suppresses incidental interaction and notification audio. Explicit Recorder/content audio remains available.

Theme selection is persisted in canonical `/sdcard/rfscout.json` configuration rather than separate NVS state.

## Semantic feedback and input lock

RFScout emits semantic events after actions/state changes are accepted. Implemented event classes include navigation, activation, enter/back, enable/disable, input lock/unlock, theme change, general detection, application notification, warning, and error.

The two-second center-button hold toggles the global input lock. While locked, ordinary navigation/select input is ignored. The feedback sound is emitted only after the lock/unlock transition has committed, not merely because the button is being held.

`feedback` combines event meaning, native app audio policy, and the active theme sound policy before submitting disposable UI audio.

## Audible notifications and speaker output

Global notification presentation remains **OFF**, **TONE**, or **MORSE** plus a global volume. Application notification gates and Morse mappings are app-owned. This allows a future app to be natively quiet without weakening global notification behavior elsewhere.

Speaker output is asynchronous. UI feedback is disposable rather than queued indefinitely. Recorder/application content audio is distinct from event feedback and detection notifications.

## Configuration and migration

Persistent preferences use `/sdcard/rfscout.json` schema **version 2**. Loading starts from current firmware defaults and overlays recognized valid values. Schema v1 is accepted and translated where supported.

Unknown/obsolete fields are ignored. Saving constructs a new canonical document, so unknown keys are pruned and new fields missing from older files appear with current defaults. Configurations claiming a future schema version are rejected instead of being destructively rewritten by older firmware.

WiFi / BT ordinary Wi-Fi/BLE discovery switches default ON when absent, preserving behavior for older configs.

## Deep-sleep behavior

RFScout separately retains a small RTC-backed session envelope across deep sleep. The active application and user-input lock state survive a valid sleep wake. Retained state is versioned/checksummed and is not used as ordinary long-term user configuration.

Transient UI/menu/edit state and active Recorder operations are intentionally not resumed. Dirty canonical configuration is flushed before deep sleep.

## Shared UI/status behavior

Application views use shared top chrome for title placement, local time/status icons, battery indication, and semantic color orchestration. Theme roles cover background, text, secondary information, dividers, accent/selection, informational/success/warning/error state, graph colors, and Home-logo colors.

Raw framebuffer paths remain available for true image/sensor data such as the Thermal heat map.

Implemented status information includes local wall time, mounted SD state, sticky filesystem/logging error state, battery state of charge, and charging/power telemetry when available.

## Storage and logging

RFScout can operate without an SD card. When mounted, storage provides canonical configuration persistence, Recorder files, and asynchronous logs. Logging uses `rfscout-00.log` through `rfscout-99.log`, up to 4 MiB per segment, with an approximate normal maximum of 400 MiB and a free-space reserve policy.

With valid wall time, records use UTC ISO-8601 timestamps. Before time is established, records use an explicit `UP+<milliseconds>` monotonic marker.

## Time

The ESP32 system wall clock is authoritative for display, log timestamps, and normal filesystem calendar metadata. Monotonic timers remain authoritative for debounce, RF dwell, retries, timeouts, and housekeeping.

Manual time setting and timezone selection are implemented in Device Settings. Orderly hard power-off can persist a shutdown epoch/source to SD as a cold-boot continuity aid. Deep sleep preserves RTC-running time separately.

## Hardware/resource boundaries

Physical bus ownership is board-specific. On the current T-Embed target:

- LCD, microSD, CC1101, and nRF24 share SPI resources;
- board I2C is shared by battery/power telemetry and optional external I2C peripherals;
- I2S0 is used by Recorder for PDM microphone recording/monitoring;
- I2S1 is used by the asynchronous speaker service.

Application code should depend on capabilities and service interfaces rather than board identity.

## Browser flashing and releases

Ready-for-review PRs and `main` builds can produce short-lived candidate bundles. Durable GitHub Releases are prepared deliberately through the manual release workflow. See [RELEASING.md](RELEASING.md).

## Not currently implemented

Automatic ambient-light theme selection is not implemented. The proposed future separation of audio audibility thresholds from visual themes is also not implemented; current theme sound policy remains part of `ui_theme`.

Broader per-app session persistence/unification, NFC/PN532 tools, IR tools, GPS UI/location, a standalone CW sender, shared Audio Input service, external RTC integration, active RF transmission/replay, and other future concepts remain roadmap items rather than current applications.
