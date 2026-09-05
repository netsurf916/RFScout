# RFScout capabilities

This document summarizes behavior implemented on current `main`. Architecture/invariants live in [ARCHITECTURE.md](ARCHITECTURE.md); planned work and release status live only in [ROADMAP.md](ROADMAP.md).

## Supported target and application order

The current supported target is **LILYGO T-Embed CC1101 Plus / ESP32-S3**. Application code consumes board-neutral services/capabilities rather than board names.

Canonical rotary order:

0. System Events — optional, disabled by default
1. Home / Device Settings
2. WiFi / BT
3. CC1101
4. nRF24
5. Recorder
6. Thermal — when enabled/detected

Disabled or unavailable apps are skipped. Home is always available and is the fallback when a preferred startup app cannot run.

System Events is a read-only view of the RAM-only structured event ring. Center enters/exits a stable Browse snapshot; rotary input scrolls only while Browse is active.

## WiFi / BT

WiFi / BT passively observes Wi-Fi and BLE activity, performs longest-prefix IEEE vendor attribution, distinguishes randomized/private addresses from unresolved public addresses, and applies conservative surveillance-oriented classification.

The normalized live observation store now grows in app-session PSRAM rather than reserving or enforcing a fixed 1000-item capacity. Collector ingestion uses an address/type identity index rather than rescanning the full collection per callback. Summary/presentation state is bounded independently from retained history.

Wi-Fi, BLE, and retained surveillance history currently have independent 10–300 second retention controls in 10-second steps, defaulting to 60 seconds. Wi-Fi and BLE membership age independently inside merged observations. Ordinary Wi-Fi/BLE discovery cues can be enabled independently; classified surveillance detections use app-specific alert categories.

Leaving the app transactionally drains/stops both collectors before its app-owned PSRAM is released.

## CC1101

CC1101 performs receive-only RSSI sweeps with configurable profile/range/step, live activity histogram, bounded sweep samples, conservative RF-behavior categories, notifications, and count reset. Labels are heuristics about observed RF behavior, not decoded product identity.

Its worker is session-managed and must quiesce before app-owned memory can be reclaimed.

## nRF24

nRF24 performs receive-only 2.4 GHz RPD scanning with configurable channel bounds/dwell, activity history, conservative classification, and surfaces for validated Enhanced ShockBurst observations.

RPD is threshold energy evidence, not RSSI and not packet proof. The production scanner currently produces RPD observations only. Packet list/detail surfaces consume separately validated observations and preserve channel, air rate, address, PID/ACK state, CRC state, timestamp, and full payload when such observations are provided. The browser keeps the displayed bounded snapshot stable while a row is selected.

RFScout does **not** enable auto-ACK, inject, replay, pair, probe, or actively discover nRF24 devices. Future receive-only ESB discovery/CRC validation remains roadmap work.

Acquisition and presentation use separate cadences: sweep data continues feeding classification while display snapshots are coalesced. Center-button edge capture is designed so a completed click survives temporary synchronous LCD/main-loop stalls.

## Recorder

Recorder provides SD-backed 16-bit mono PCM WAV capture/playback, runtime PDM/effective-rate measurement, measured-rate WAV metadata, asynchronous trim/DC-removal/normalization, recording browse/delete, independent playback volume, live monitor FFT, and microphone input gain from 0 through +24 dB in 3 dB steps.

Recorder content audio remains available under device Quiet policy; incidental feedback can be suppressed by the app's native policy. Worker/channel shutdown is session-managed while exact recording/monitor/playback rollback state remains semantic app state.

## Thermal

Thermal is available when an enabled MLX90640 is detected on the external I2C/Qwiic interface. Native 32x24 data is interpolated for the LCD; literal thermal imagery bypasses semantic theme translation while chrome/labels remain theme-aware.

Absolute-temperature calibration improvements remain roadmap work.

## Home / Device Settings

Implemented global settings include:

- brightness, display dim timeout, screen-off timeout, and visual theme;
- independent Normal / Quiet Audio policy;
- UI, Event, and Alert presentation levels/policy;
- configurable short/long power-button actions;
- low-battery and high-internal-temperature protective power policy;
- 12/24-hour clock, timezone, and manual date/time;
- startup application and per-app enablement;
- SD logging;
- explicit configuration save.

Small finite choices use direct center-to-cycle behavior; numeric ranges use edit/rotate. Per-app alert mappings and WiFi / BT retention remain in their owning app menus.

Visual themes are **Indoor**, **Outdoor**, and **Night** and affect colors only. **Quiet** suppresses incidental UI/Event/Alert presentation without rewriting saved app notification gates, mappings, mode, or volume. Explicit application Content audio such as Recorder playback remains available.

## Power, display, and sleep

Short power-button actions may be None, Screen off, Sleep, or Power off; long press may be None or Power off, subject to board capabilities. A dim/off display consumes the entire first wake gesture before ordinary input or power semantics.

Automatic screen-off affects presentation only. The ST7789 enters hardware sleep and framebuffer transfers are suppressed while apps, logging, RF acquisition, and audio continue.

Protective policy can act on sustained low battery or high ESP32 internal temperature. Low-battery action requires fresh gauge and charger evidence; high-temperature Power off falls back to Sleep when external-power absence cannot be freshly established. Detailed persisted values/semantics are in [CONFIGURATION.md](CONFIGURATION.md).

Deep sleep retains only a small RTC-backed session record (active app and input lock). Menus/edit state/queued audio/active Recorder work are not resumed.

## Feedback and notifications

RFScout distinguishes semantic interaction feedback, app-owned detection alerts, and requested Content audio. Global alert presentation is OFF, TONE, or MORSE plus volume; each detecting app owns its alert gate/mappings.

The device-wide Normal/Quiet policy is independent of visual theme. Speaker output is asynchronous; disposable feedback/alerts are not queued behind active Content audio.

## Configuration, storage, and logging

Persistent preferences use `/sdcard/rfscout.json` schema version **3**. Loading begins with defaults and overlays recognized valid fields; v1/v2 migration is best effort while RFScout remains pre-1.0. Future schema versions are rejected rather than destructively rewritten. See [CONFIGURATION.md](CONFIGURATION.md).

RFScout can operate without SD for radio/sensor use. SD provides canonical persistence, Recorder files under `/sdcard/audio/`, and asynchronous rotating logs. The conservative board-tested baseline is a FAT-compatible SanDisk card of 32 GB or less. Logs rotate through 100 files of up to 4 MiB each while preserving a free-space reserve.

## Hardware and memory boundaries

LCD/microSD/CC1101/nRF24 share SPI resources; power telemetry/external sensors share I2C; Recorder microphone uses I2S0; speaker output uses I2S1.

Usable external **PSRAM is required**. App-scoped memory/session infrastructure is implemented: growable app-owned state can consume PSRAM during the session and is reclaimed after platform services prove they no longer reference it.

## Not currently implemented

Major v0.4.0 work still outstanding includes Instant/Timed/Never WiFi/BLE retention semantics, semantic Wi-Fi/BLE entity/correlation views, dynamic surveillance-history migration, broader app-by-app PSRAM/history improvements, the calibrated shared Audio Input service, thermal calibration, and release-level stress/fault-injection validation. The canonical checklist and status are in [ROADMAP.md](ROADMAP.md).