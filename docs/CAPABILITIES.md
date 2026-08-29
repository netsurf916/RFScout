# RFScout capabilities

This document describes capabilities implemented on this PR branch. Planned features and design ideas belong in [ROADMAP.md](ROADMAP.md).

## Application order

RFScout starts on **Home** by default. Top-level navigation order is:

1. WiFi / BT
2. CC1101
3. nRF24
4. Recorder
5. Thermal, when enabled and detected
6. Home / Device Settings

Disabled applications are skipped. Home/Device is always available. Device Settings can select any listed application as the preferred startup target; a disabled or unavailable target falls back to Home for the current boot without erasing the saved preference.

## WiFi / BT

Implemented behavior includes passive Wi-Fi and BLE observation, rolling recent-device state, IEEE vendor attribution using MA-L/MA-M/MA-S/IAB longest-prefix matching, explicit `RANDOM` handling for private/local addresses, explicit `UNKNOWN` handling for unresolved public addresses, surveillance-oriented multi-signal classification, retained surveillance history, global detection notifications, and a `RESET DETECTIONS` action.

Leaving WiFi / BT stops the Wi-Fi and BLE collectors.

## CC1101

Implemented behavior includes direct-RSSI sub-GHz sweeps, configurable profiles/range/step, optional slow peak hold, a 128-sample sweep limit, conservative RF-shape detection categories, device-wide audible notifications when counts advance, and `RESET COUNTS`.

The categories describe observed RF behavior and are not protocol/product decodes.

## nRF24

Implemented behavior includes configurable channel bounds and dwell time, fast activity history, optional slow peak hold, passive Enhanced ShockBurst classification when validated observations exist, address-based count deduplication, conservative detection categories, device-wide notifications, and `RESET COUNTS`.

Spectrum-only activity that does not support a specific device identity remains broadly categorized.

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

The nominal PDM request is currently 48 kHz, but the physical/delivered rate is measured and may differ. Supported playback validation accepts 16-bit mono WAV rates from 8 through 48 kHz. Recorder is not a calibrated sound-level instrument.

The source tree temporarily retains the historical `audio_spectrum` application compatibility interface because the application enum/navigation ABI still uses `RFSCOUT_APP_AUDIO`. That adapter is not the removed spectrum analyzer implementation.

## Thermal

Thermal is available when an MLX90640 is detected on the external I2C/Qwiic interface and the application is enabled. The sensor's native 32x24 frame is interpolated for the LCD. Thermal currently has no discrete detection classifier.

## Home / Device Settings

Home is a branded pass-through landing screen backed by the non-disableable Device application. The RFScout mark is drawn procedurally on the LCD rather than embedded as a second large bitmap. Rotation continues top-level navigation; center enters Device Settings.

Implemented Device Settings include display brightness, 12/24-hour clock presentation, timezone selection, preferred startup application, manual date/time, per-application enablement, SD logging, global notification mode, global notification volume, one Morse character per notification category, and explicit configuration save.

The startup preference is persisted as `apps.startup` in `/sdcard/rfscout.json`. RFScout writes `home`, `wifi_bt`, `cc1101`, `nrf24`, `recorder`, or `thermal`; historical internal aliases `device` and `audio` remain accepted when loading. Home is the compiled default.

Runtime defaults remain usable without an SD card. Recorder playback volume and microphone input gain are also persisted in device configuration, but are adjusted through Recorder rather than Device Settings.

## Shared UI/status behavior

Application views use shared top chrome for title placement, local time/status icons, and battery indication. The shared chrome constrains long titles so they do not collide with the right-side status region. App content below the header remains application-owned; no footer area is globally reserved.

Implemented status information includes local wall time, mounted SD state, sticky filesystem/logging error state, battery state of charge, and charging/power telemetry when available.

## Audible notifications and speaker output

Detection-producing applications use a device-wide `OFF`, `TONE`, or `MORSE` policy. Morse mode emits one configured character for the detection category. Notification volume is persisted separately from Recorder playback volume.

Speaker output runs asynchronously on I2S1. The volume curve is perceptual/nonlinear so low percentages are materially quieter; `0%` is mute and `100%` is full configured amplitude. Queued alert commands retain the volume active when they were requested.

## Storage and logging

RFScout can operate without an SD card. When mounted, storage provides configuration persistence, Recorder files, and asynchronous logs. Logging uses `rfscout-00.log` through `rfscout-99.log`, up to 4 MiB per segment, with an approximate normal maximum of 400 MiB and a free-space reserve policy.

With valid wall time, records use UTC ISO-8601 timestamps. Before time is established, records use an explicit `UP+<milliseconds>` monotonic marker.

## Time

The ESP32 system wall clock is authoritative for display, log timestamps, and normal filesystem calendar metadata. Monotonic timers remain authoritative for debounce, RF dwell, retries, timeouts, and housekeeping.

Manual time setting and timezone selection are implemented in Device Settings. Orderly hard power-off can persist a shutdown epoch/source to SD as a cold-boot continuity aid. Deep sleep preserves RTC-running time separately.

## Hardware/resource boundaries

- SPI2 is shared by LCD, microSD, CC1101, and nRF24 through the board SPI layer.
- Board I2C is shared by battery/power telemetry and optional external I2C peripherals.
- I2S0 is used by Recorder for PDM microphone recording/monitoring.
- I2S1 is used by the asynchronous speaker service.
- Recorder currently owns microphone configuration directly; the planned shared Audio Input service is not yet implemented.

## Browser flashing and releases

Ready-for-review PRs and `main` builds can produce short-lived candidate bundles. The browser-flasher GitHub Pages deployment exposes the candidate from the most recent triggering eligible build plus published releases through ESP Web Tools/Web Serial. The flasher uses the RFScout logo and orange/white branding. Durable GitHub Releases are prepared deliberately through the manual release workflow and remain draft until explicitly published.

See [RELEASING.md](RELEASING.md).

## Not currently implemented

NFC/PN532 tools, IR tools, GPS UI/location, a standalone CW sender, shared Audio Input service, external RTC integration, active RF transmission/replay, and other future concepts are not current applications. Their status and planning live in [ROADMAP.md](ROADMAP.md).
