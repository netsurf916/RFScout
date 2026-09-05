# RFScout capabilities

This document describes the user-visible behavior implemented in the current firmware. Planned work belongs in [ROADMAP.md](ROADMAP.md).

## Application model

RFScout presents compiled-in applications through one common runtime/session model. Applications own their local views and controls while the platform owns global navigation, resource teardown, and app-memory reclamation.

Canonical rotary order:

1. System Events — when enabled
2. Home / Device Settings
3. WiFi / BT
4. CC1101
5. nRF24
6. Recorder
7. Thermal — when enabled/detected
8. Audio Spectrum — when the microphone is available

Disabled or unavailable apps are skipped. Home is always available and is the fallback when a preferred startup app cannot run.

## WiFi / BT

WiFi / BT performs passive Wi-Fi and BLE observation with shared normalized device records. Wi-Fi and BLE source membership age independently. Retention values currently accept 10–300 seconds in 10-second steps; planned Instant/Never semantics are roadmap work.

The app can surface surveillance-oriented classifications and vendor/identity summaries without changing the passive receive-only operating model.

## CC1101

CC1101 provides receive-only sub-GHz spectrum scanning across configured profiles/ranges. Its worker is session-managed and stops before app-owned memory is reclaimed or another shared-SPI app takes ownership.

## nRF24

nRF24 provides receive-only 2.4 GHz activity scanning and packet/fingerprint views. Acquisition and presentation use separate cadences so sweep work can continue while display updates are coalesced.

## Recorder

Recorder provides SD-backed 16-bit mono PCM WAV capture/playback, runtime PDM/effective-rate measurement, measured-rate WAV metadata, asynchronous trim/DC-removal/normalization, recording browse/delete, and independent playback volume.

Live microphone spectrum monitoring is a separate **Audio Spectrum** app. While Recorder is actively recording or finalizing a recording, app transitions are intentionally blocked. The user stops recording with center, saving completes, and normal navigation then resumes. RFScout does not attempt to reconstruct or restart an interrupted recording after a failed transition. Playback may be stopped normally when Recorder is left.

Recorder content audio remains available under device Quiet policy; incidental feedback can be suppressed by the app's native policy. Worker/channel shutdown remains session-managed.

## Audio Spectrum

Audio Spectrum is a microphone-only live FFT/spectrum app and does not require an SD card. The normal view leaves rotary input available for global app navigation; center opens the app-local settings view.

The current setting is fixed digital microphone input gain from **0 through +48 dB in 3 dB steps**. Center or Back closes settings. The shared audio-capture service owns microphone/channel lifetime so Audio Spectrum uses the same ordinary app/session contract as the radio and sensor apps rather than Recorder-specific rollback semantics.

## Thermal

Thermal uses an optional MLX90640 on the shared board I2C path. Availability is hot-plug aware and loss of the active sensor returns to Home rather than cascading through another unavailable app.

## Device settings and persistence

Implemented global settings include:

- display brightness, dim/off timing, and visual theme;
- independent Normal/Quiet audio policy and global UI/Event/Alert levels;
- configurable short/long power-button actions;
- low-battery and high-internal-temperature protective power policy;
- 12/24-hour clock, timezone, and manual date/time;
- startup application and per-app enablement, including Audio Spectrum independently from Recorder;
- SD logging; and
- explicit configuration save.

Persistent preferences use the authoritative internal `/internal/rfscout.json` schema version **3**. Missing, corrupt, or incompatible internal configuration is replaced with current defaults. Internal updates use a complete temporary file followed by atomic rename; no persistent backup or quarantine copies are retained internally. See [CONFIGURATION.md](CONFIGURATION.md).

An SD card is not required to retain settings. `/sdcard/rfscout.json` is an explicit one-shot import file; a valid current-schema import replaces the internal configuration and is deleted after the internal commit. Invalid imports are preserved as `/sdcard/rfscout.json.bad`. `/sdcard/rfscout-example.json` mirrors the authoritative internal configuration when SD is available and is rewritten only when its contents differ.

RFScout can operate without SD for configuration, radio/sensor use, and Audio Spectrum. SD remains required for Recorder WAV files under `/sdcard/audio/`, rotating logs, and configuration import/example exchange. The conservative board-tested baseline is a FAT-compatible SanDisk card of 32 GB or less. Logs rotate through 100 files of up to 4 MiB each while preserving a free-space reserve.

## Hardware and memory boundaries

LCD/microSD/CC1101/nRF24 share SPI resources; power telemetry/external sensors share I2C; the shared audio-capture service uses I2S0 for the microphone; speaker output uses I2S1.

Usable external **PSRAM is required**. App-scoped memory/session infrastructure is implemented: growable app-owned state can consume PSRAM during the session and is reclaimed after platform services prove they no longer reference it.

The 16 MiB internal flash is split into an 8 MiB firmware partition plus approximately 8 MiB of SPIFFS internal persistence. Configuration currently consumes only one live JSON file (and a transient write temporary), leaving the remainder available for future system-owned persistent records/database use.

## Not currently implemented

Major v0.4.0 work still outstanding includes Instant/Timed/Never WiFi/BLE retention semantics, semantic Wi-Fi/BLE entity/correlation views, dynamic surveillance-history migration, broader app-by-app PSRAM/history improvements, the planned further calibration/generalization of the Audio Input service, thermal calibration, a generic app-facing persistent record/database service, and release-level stress/fault-injection validation. The canonical checklist and status are in [ROADMAP.md](ROADMAP.md).
