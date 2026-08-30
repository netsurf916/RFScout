# Architecture

This document describes architecture implemented on current `main`. Forward-looking architecture and feature planning belong in [ROADMAP.md](ROADMAP.md).

## Board and platform boundary

RFScout is structured as one source tree producing board-specific firmware images. The current supported target is **LILYGO T-Embed CC1101 Plus**. `RFSCOUT_BOARD` selects the board target, whose physical definitions live under `main/boards/<target>/`.

Application code should depend on services, semantic input, and capabilities rather than board model names. `platform_input_get()` is the board-neutral input contract; the current T-Embed implementation maps its rotary encoder and buttons into navigation/select/back/long-select events.

See [BOARD_PORTING.md](BOARD_PORTING.md) for the board contract and future porting model.

## Application manager and lifecycle

Top-level application order is:

```text
WiFi / BT -> CC1101 (when enabled and detected) -> nRF24 (when enabled and detected) -> Recorder (when enabled and microphone detected) -> Thermal (when enabled and present) -> Home / Device Settings
```

RFScout defaults to Home at startup. `device_config` persists a preferred startup application through `apps.startup`; `app_manager` validates that preference against application enablement and runtime hardware availability. A disabled or unavailable target falls back to Home for the current boot without rewriting the saved preference.

CC1101, nRF24, and the board PDM microphone are treated as board-population capabilities rather than hot-plug peripherals. `app_manager` probes each once during startup and includes its application in navigation only when the corresponding hardware is present. The microphone probe briefly starts the PDM clock and checks for data transitions, then immediately releases I2S0. RFScout does not add status-bar indicators for this fixed hardware. Thermal remains different: it is an explicitly attachable external sensor, so its runtime probe and availability feedback remain dynamic.

Lifecycle rules are explicit:

- leaving WiFi / BT stops Wi-Fi and BLE collectors;
- leaving CC1101 stops scanning and clears runtime spectrum/classification state;
- leaving nRF24 stops scanning and clears runtime activity/classification state;
- leaving Recorder stops capture, monitor, or playback activity;
- Thermal acquisition runs only while Thermal is active;
- Home owns no radio/sensor worker merely by being active.

## Semantic event boundary

RFScout distinguishes physical input from accepted application meaning.

```text
board input
    -> platform_input_get()
    -> application/menu interpretation
    -> accepted state/action
    -> rfscout_event_emit()
    -> feedback / other observers
```

`event.c/.h` implements a small synchronous fixed-observer dispatcher. Events include navigation, activation, enter/back, enable/disable, input lock/unlock, theme change, detection, notification, warning, and error. Event handlers must remain short and nonblocking.

The important semantic rule is that success-style feedback is emitted after an action has actually been accepted. Raw encoder movement or button contact does not itself imply a sound. The two-second input-lock gesture follows the same rule: lock/unlock feedback occurs only when the platform commits the new state.

## Feedback, notification, and content-audio separation

RFScout treats three sound classes differently:

1. **interaction/general feedback** from semantic events;
2. **application-owned detection notifications**;
3. **intentional application content audio**, currently Recorder playback/monitor behavior.

`feedback.c/.h` subscribes to semantic events. It combines the source application's native `app_audio_policy` with the active theme sound policy and submits disposable feedback to `audio_output` when appropriate.

`app_audio_policy.c/.h` describes native application traits: whether interaction/general-event feedback makes sense, whether notifications are supported and enabled by default, and whether the app owns content audio. Recorder is intentionally quiet for incidental feedback while retaining content audio.

Detection categories are owned by `notification.c/.h`. Detecting apps have their own notification enable gate and Morse mappings in canonical configuration. Global notification presentation remains Off/Tone/Morse plus volume. WiFi / BT additionally controls ordinary Wi-Fi and BLE discovery cues independently.

`audio_output.c/.h` owns asynchronous speaker execution and class arbitration. UI feedback is disposable rather than backlog-producing. Notification cancellation and UI feedback do not treat intentional Recorder playback as an ordinary alert.

## Theme policy

`ui_theme.c/.h` owns semantic visual palettes and environmental sound policy. Current themes are Indoor, Outdoor, Night, and Discipline.

Discipline deliberately reuses the Night visual palette and disables incidental interaction and notification audio, making it both a light- and noise-discipline mode. Explicit application content audio remains available.

Themes are persisted in canonical `/sdcard/rfscout.json` configuration under `display.theme`; they are no longer a separate NVS preference.

## Wi-Fi / BLE path

```text
Wi-Fi monitor ---\
                  +--> normalized observation --> prefix identity --> surveillance classifier
BLE monitor -----/              |                        |
                                v                        v
                         rolling device store       retained detections
                                |                        |
                                +-----------> WiFi / BT UI
```

The normalized observation store retains per-source last-seen timestamps for Wi-Fi and Bluetooth. Their source memberships expire independently using app-owned configuration, so an observation associated with both sources remains live while either source is still within its configured window. The retained surveillance history has a third independent timeout. All three controls default to 60 seconds and accept 10–300 seconds in 10-second increments.

Housekeeping remains monotonic-time based and independent of which application owns the display. Retention policy belongs to WiFi / BT configuration; the UI does not maintain a second cache merely to keep rows visible.

Ordinary newly observed Wi-Fi/BLE devices can emit a lightweight `DETECTED` semantic event when the WiFi / BT master notification gate and source-specific event switch allow it. Surveillance classifications instead use their application-specific notification category and do not also emit the ordinary discovery cue.

Prefix identity keeps authoritative IEEE vendor attribution separate from curated surveillance significance. Private/random addresses are not assigned misleading IEEE vendors. The surveillance classifier can combine prefix metadata with BLE/protocol evidence.

## CC1101 path

```text
CC1101 worker -> completed RSSI sweep -> live spectrum
                                      -> one previous-sweep snapshot for temporal classification
                                      -> conservative classifier
                                      -> detection counts
                                      -> app-owned notification on count advance
```

The display consumes only the current sweep. The classifier retains one previous normalized sweep where a short temporal comparison helps distinguish burst-like behavior; there is no slow-decay peak history or user-visible peak-hold state. Sweep configuration is bounded to 128 samples. Detection categories describe RF behavior rather than decoded product identity.

## nRF24 path

```text
nRF24 RPD sweeps -> fast EWMA activity histogram -> broad spectrum classifier
validated ESB observations -> recent packet ring -> packet preview/browser
                           -> classifier/address ledger -> detection counts
                                                   |
                                                   v
                                         app-owned notification
```

The nRF24 activity path maintains only the fast EWMA histogram required for display and broad spectrum-shape classification. It does not maintain a second slow peak-hold histogram. The address ledger is separate from the small active classifier cache so classifier eviction does not cause repeated identity counts. Wi-Fi-like wide spectrum activity and hopping/BLE-like activity remain distinct classifier categories and display rows; presentation does not combine them into an `Other` count.

## Recorder path

```text
internal PDM mic -> ESP32-S3 I2S0/PDM -> Recorder worker
                                              |
                                              +-> runtime clock/rate measurement
                                              +-> PCM capture
                                              +-> async trim/DC removal/normalization
                                              +-> WAV under /sdcard/audio/
                                              +-> validated recording list
                                              +-> I2S1 speaker playback
```

`recorder.c/.h` currently owns microphone configuration and capture directly. Recorder also owns live monitor mode, microphone FFT presentation feed, fixed digital input gain, recording enumeration/deletion, and save-time post-processing. Recorder playback volume is independent from notification volume.

The historical `audio_spectrum.c/.h` interface remains as a compatibility adapter for the existing `RFSCOUT_APP_AUDIO` application/navigation ABI.

## Thermal path

MLX90640 is an optional external I2C/Qwiic device. RFScout probes for availability and exposes Thermal only when enabled and detected. The native 32x24 frame is interpolated for display. Thermal image pixels are literal data imagery and use a raw framebuffer path; Thermal chrome and labels remain theme-aware.

## Configuration and persistence boundary

`device_config.c/.h` owns compiled defaults plus optional `/sdcard/rfscout.json` persistence. Current schema version is 2.

Loading begins from current defaults and overlays recognized valid values. Schema v1 is accepted and translated. Unknown/obsolete fields are ignored. Saving generates a new canonical v2 document, which prunes unknown fields and populates newly introduced fields from current defaults. Future schema versions are rejected rather than rewritten by older firmware.

Canonical user preferences include app enablement/startup selection, display settings/theme, logging, global notification presentation, app-owned notification gates/mappings, WiFi / BT retention values, Recorder preferences, timezone data, and saved time metadata.

Setters mark configuration dirty. Dirty configuration is flushed before deep sleep and SD unmount.

## Deep-sleep session state

Deep sleep loses normal RAM, so `session_state.c/.h` retains a deliberately small RTC-backed session record containing the active app and user-input lock state. A magic/version/checksum guard protects the retained data, and it is used only for a valid sleep wake.

Transient menus, edit cursors, queued sounds, and active Recorder operations are intentionally not resumed. Broader per-app session persistence is not part of the current framework.

## Home, Device configuration, and time

Home uses the canonical `assets/logo.png` artwork, converted at configure time into a compact indexed LCD bitmap. The bitmap stores semantic color indices so Home branding follows the active theme.

`device_time.c/.h` owns wall-clock state and provenance. Wall-clock time is used for display and calendar timestamps; monotonic timers remain responsible for intervals, dwell, debounce, retries, and timeouts.

## Shared hardware boundaries

The exact physical buses and pin assignments are board-owned facts. On the current T-Embed target:

- LCD, microSD, CC1101, and nRF24 share SPI resources;
- battery/power devices and optional external sensors share board I2C;
- I2S0 is used for Recorder PDM microphone capture/monitoring;
- I2S1 is owned by the asynchronous speaker service.

Application policy must not infer those resources from board names; it should depend on platform/service interfaces and capabilities.

## Storage and logging

`storage.c/.h` owns SD mount/state. `log_writer.c/.h` owns asynchronous queued writes, segment rotation, free-space policy, and shutdown. `app_logging.c/.h` coordinates application ENTER/EXIT and rate-limited telemetry. Filesystem errors are nonfatal and surfaced through shared status UI.

## UI boundary

Application views render their own content into the framebuffer. Shared top chrome is drawn through `ui_chrome_draw()`, which owns title placement plus shared status/battery presentation. Semantic theme roles are preferred for new UI code; raw framebuffer paths remain available for literal sensor/image data.

`ui_present.c` remains the common presentation layer and applies the global power-off overlay before flushing the framebuffer. It does not own application chrome.

## Release/build boundary

Pull-request and `main` CI perform formatting/tests and eligible ESP-IDF firmware packaging. Candidate artifacts are short-lived. Durable releases are prepared only through the manual release workflow.
