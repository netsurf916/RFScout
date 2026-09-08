# RFScout capabilities

This document summarizes behavior implemented on the current development line. Architecture/invariants live in [ARCHITECTURE.md](ARCHITECTURE.md); planned work and release status live only in [ROADMAP.md](ROADMAP.md).

## Supported hardware and application availability

RFScout builds one ESP32-S3 firmware image for **LILYGO T-Embed CC1101 Plus** and **LILYGO T-Deck Plus**. A minimal early I2C fingerprint selects an immutable runtime profile before ordinary board GPIO/peripheral initialization. Unknown hardware remains awake and inert for recovery flashing.

The T-Embed remains the fully hardware-validated baseline. Initial T-Deck hardware validation covers runtime profile detection, 320x240 display orientation, the managed 320x170 app view and 320x70 system region, trackball/center input, GT911 coordinate orientation, persistent touch calibration, GPS acquisition/time synchronization, SD mounting/logging, voltage-only battery telemetry, standard-I2S speaker output, and the ES7210 audio-capture provider. Extended shared-SPI stress behavior, ES7210 field validation, and the final T-Embed regression pass remain validation items.

Applications consume semantic service availability rather than board identity. Current T-Deck capability filtering therefore leaves the existing CC1101 and nRF24 apps unavailable rather than probing illegal providers, while Recorder and Audio Spectrum are admitted through the audio-capture service. Profile capability is the legality gate; provider probes independently verify expected hardware before an app is advertised.

The canonical application registry order remains:

0. System Events — optional, disabled by default
1. Home / Device Settings
2. WiFi / BLE
3. CC1101 — when the active profile/provider supports it
4. nRF24 — when the active profile/provider supports it
5. Recorder — when the current audio-capture provider is available
6. Thermal — when enabled/detected
7. Audio Spectrum — when the current audio-capture provider is available

Disabled or unavailable applications are skipped by global navigation and by the T-Deck tab strip.

System Events is a read-only view of the RAM-only structured event ring. Center enters/exits a stable Browse snapshot; semantic navigation scrolls only while Browse is active.

## Managed application view and T-Deck navigation

Each app session owns one managed `rfscout_view_t`. The current app view is deliberately **320x170 on both supported boards** so existing application layouts retain the T-Embed geometry while the abstraction is established.

On T-Embed that view is the full LCD. On T-Deck it occupies the upper 320x170 portion of the physical 320x240 LCD; the lower 320x70 region is system-owned. The system region renders available application tabs and touch/calibration status and is repainted after app rendering.

Calibrated GT911 finger-down events inside the tab strip are consumed by global navigation and switch applications through the normal transactional app lifecycle. Ordinary touch inside the managed application view is ignored. When a peripheral temporarily makes the physical primary Select source unavailable—currently T-Deck ES7210 capture—the system translates an app-view touch into the existing semantic Select event. This preserves Recorder stop control during the GPIO0 conflict without establishing a general pointer-specific application API. Trackball/center remains the ordinary physical Select source whenever it is available.

## GPS, location, and time

T-Deck exposes a board-neutral GPS service over UART1/NMEA. The service distinguishes provider initialization from receiver detection and exposes independent validity for location and GPS UTC time. A receiver snapshot is not available until RFScout has observed a checksum-valid supported RMC/GGA sentence.

A fresh location fix exposes latitude/longitude, speed, satellite count, altitude data when supplied, UTC epoch, and age metadata. Location/time validity expires when the corresponding fix data becomes stale; consumers must check the validity flags rather than treating cached values as current.

The global status bar shows a GPS indicator only after receiver detection. The indicator uses the semantic warning color while the receiver has no fresh location fix and success color while a fresh fix is present. Fix acquisition/loss is logged diagnostically and, when SD logging is active, to the normal durable log stream.

`device_time` owns clock validity/source, **Auto / Manual** synchronization policy, and timezone conversion. In Auto mode, a fix-valid GPS UTC timestamp synchronizes the RTC-backed system wall clock; Manual mode ignores GPS clock offers. Once synchronized, the wall clock remains valid even if the current GPS fix later disappears. Timezone remains a presentation policy applied by `device_time`, not by GPS. Time-policy changes are persisted before the new mode is published, so a failed policy write cannot silently create different runtime and reboot semantics.

## Battery status

T-Embed retains its validated fuel-gauge/charger path, including authoritative external-power state.

T-Deck uses the board battery ADC on GPIO4 with the profile-owned 2:1 divider. RFScout filters measured voltage and estimates LiPo state of charge for the battery indicator. The estimate is useful telemetry, not a fuel-gauge measurement. T-Deck does not fabricate charging, charge-complete, or USB/external-power state because no authoritative digital source has been established.

Automatic low-battery sleep/power-off requires authoritative external-power telemetry and is therefore disabled on the current voltage-only T-Deck profile. Battery percentage remains visible.

## WiFi / BLE

WiFi / BLE passively observes Wi-Fi and BLE activity, performs longest-prefix IEEE vendor attribution, distinguishes randomized/private addresses from unresolved public addresses, and applies conservative surveillance-oriented classification.

The normalized live observation store grows in app-session PSRAM rather than reserving or enforcing a fixed item capacity. Collector ingestion uses an address/type identity index rather than rescanning the full collection per callback. Both the general summary and surveillance view are projections of this one canonical active-device collection.

Wi-Fi and BLE source membership age independently using their respective 10–300 second retention controls, defaulting to 60 seconds. Surveillance classification is an attribute of the canonical device record and expires with that record; there is no separate surveillance retention list or timeout.

Curated surveillance signatures detect from the first observation that meets the configured **High / Medium / Low** sensitivity threshold, defaulting to High. Repeats for the same active device refresh its canonical observation rather than building confidence. Only currently active surveillance signatures are shown, ordered by recent activity with overflow compacted when necessary. Alerts are configured by semantic category such as ALPR, body camera, drone, camera, smart glasses, and acoustic sensor.

Ordinary Wi-Fi/BLE discovery cues can be enabled independently. Leaving the app transactionally drains/stops both collectors before its app-owned PSRAM is released.

## CC1101

CC1101 performs receive-only RSSI sweeps with configurable profile/range/step, live activity histogram, bounded sweep samples, conservative RF-behavior categories, notifications, and count reset. Labels are heuristics about observed RF behavior, not decoded product identity.

Its worker is session-managed and must quiesce before app memory can be reclaimed. The provider is currently advertised only by the T-Embed profile.

## nRF24

nRF24 performs receive-only 2.4 GHz RPD scanning with configurable channel bounds/dwell, activity history, conservative classification, and surfaces for validated Enhanced ShockBurst observations.

RPD is threshold energy evidence, not RSSI and not packet proof. The production scanner currently produces RPD observations only. Packet list/detail surfaces consume separately validated observations and preserve channel, air rate, address, PID/ACK state, CRC state, timestamp, and full payload when such observations are provided. The browser keeps the displayed bounded snapshot stable while a row is selected.

RFScout does **not** enable auto-ACK, inject, replay, pair, probe, or actively discover nRF24 devices. Future receive-only ESB discovery/CRC validation remains roadmap work. The provider is currently advertised only by the T-Embed profile.

## Recorder

Recorder provides SD-backed 16-bit mono PCM WAV capture/playback, asynchronous post-processing, recording browse/delete, and independent playback volume. It consumes the board-neutral `audio_capture` service rather than an I2S channel. That service owns provider selection, profile-gated probing, open/read/close, and capture transport lifetime. T-Embed retains its PDM provider with runtime PDM/effective-rate measurement and measured-rate WAV metadata. T-Deck uses the onboard ES7210 codec through the shared board I2C service while the ES7210 provider exclusively owns I2S0/DMA; the selected MIC1/LEFT stream is normalized to ordinary mono `int16_t` PCM.

T-Deck ES7210 presence probing is a non-invasive I2C transaction and does not start capture, claim I2S0, or suspend GPIO0. T-Embed PDM retains its active clock/data-edge probe because it has no separate control-plane identity. In both cases, `audio_capture_open()` is the final operational validation before PCM is read.

Capture is not software pre-amplified. After capture completes, Recorder trims startup/tail artifacts, removes DC/very-low-frequency energy with the same shared one-pole high-pass primitive used by Audio Spectrum, then applies bounded normalization before finalizing the WAV.

While Recorder is actively recording or finalizing a recording, app transitions and terminal sleep/power-off transitions are intentionally blocked. On T-Embed the user stops recording with the physical center control. On T-Deck, ES7210 activation temporarily owns the hardware path that conflicts with GPIO0/trackball-center, so the input service suspends that physical Select source and calibrated app-view touch supplies the same semantic Select action only for that interval. Saving completes before ordinary navigation/power transitions resume. Playback may be stopped normally when Recorder is left or a terminal power transition is prepared.

T-Deck speaker output remains independently supported for semantic UI/event/alert audio and Recorder playback through the existing audio-output provider.

## Audio Spectrum

Audio Spectrum is a microphone-only live FFT/spectrum app and does not require an SD card. It uses whichever compatible audio-capture provider the active board advertises: T-Embed PDM or T-Deck ES7210.

The spectrum uses raw microphone PCM with the shared continuous high-pass/DC-block filter and a smoothed automatic dB display range, avoiding pre-FFT digital amplification and clipping. The live `AUTO N dB` readout reports the current display-window span rather than microphone gain. The automatic minimum span is 18 dB, and frequency labels use measured delivered PCM throughput after the monitor has accumulated a rate measurement, falling back to the configured request rate only at startup.

## Thermal

Thermal is available when an enabled MLX90640 is detected on the shared external I2C interface. Native 32x24 data is interpolated for the LCD; literal thermal imagery bypasses semantic theme translation while chrome/labels remain theme-aware.

Absolute-temperature calibration improvements remain roadmap work.

## Home / Device Settings

Implemented global settings include:

- brightness, display dim timeout, screen-off timeout, and visual theme;
- independent Normal / Quiet audio policy;
- UI, Event, and Alert presentation levels/policy;
- configurable semantic power-button and protective-power policy where supported;
- a Time submenu with Auto/Manual synchronization policy, timezone, and 12/24-hour presentation;
- manual date/time fields shown only while Manual mode is selected;
- startup application and per-app enablement;
- SD logging; and
- Touch calibration when a touch provider is available.

Ordinary configuration changes are persisted automatically; there is no user-facing **Save config** action. The time synchronization policy and touch calibration are bounded system records separate from normal app configuration.

Visual themes are **Indoor**, **Outdoor**, and **Night** and affect colors only. **Quiet** suppresses incidental UI/Event/Alert presentation without rewriting saved app notification gates, mappings, mode, or volume. Explicit application Content audio such as Recorder playback remains available where supported.

## Power, display, and sleep

Semantic power actions are filtered by runtime board capability. T-Embed retains Screen off, Sleep, and Power off behavior. T-Deck currently exposes no RFScout user/power key and no semantic deep-sleep or physical-power-off capability; its GPIO0 center/BOOT control belongs to the input service except while the ES7210 capture path requires the conflicting hardware resource.

Automatic display dim/off is presentation policy rather than ESP32 sleep. The ST7789 provider may enter controller low power while apps, logging, RF acquisition, and other services continue. Physical input wakes the display and the wake gesture is consumed before ordinary app semantics.

Protective low-battery action requires fresh battery percentage **and** authoritative evidence that external power is absent. This retains the T-Embed behavior while intentionally preventing voltage-only T-Deck battery estimates from causing shutdown loops when the device is actually plugged in. High-temperature policy is likewise constrained by the power actions advertised by the active profile.

Deep sleep, when supported, retains only a small RTC-backed session record (active app and input lock). Menus/edit state/queued audio/active Recorder work are not reconstructed. Terminal T-Embed sleep/power-off quiesces Recorder/logging but leaves the SD volume mounted; the mount is platform-owned for the running firmware lifetime rather than participating in a shutdown/remount rollback state machine.

## Feedback and notifications

RFScout distinguishes semantic interaction feedback, app-owned detection alerts, and requested Content audio. Global alert presentation is OFF, TONE, or MORSE plus volume; each detecting app owns its alert gate/mappings.

The device-wide Normal/Quiet policy is independent of visual theme. Speaker output is asynchronous; disposable feedback/alerts are not queued behind active Content audio. Profiles without an advertised speaker never enter the current I2S output provider.

T-Embed and T-Deck both advertise the current semantic speaker provider. T-Deck uses its hardware-validated standard-I2S speaker path independently of its ES7210 microphone-capture provider.

## Configuration, storage, and logging

Persistent preferences use firmware-managed internal records. `system/config` and `app/<persistence_key>/config` are logical ownership names, not literal filenames. The persistence service hashes opaque logical keys to fixed filesystem-safe names, serializes filesystem operations internally, limits each record to 16 KiB, and atomically replaces individual records through a same-directory temporary.

Application configuration records exist only for overrides from compiled defaults; returning all app settings to defaults removes the app record. SD is not required for device/app configuration.

SD remains removable bulk storage for Recorder WAVs and asynchronous logs. The storage service owns mounting and raw-card presence-probe arbitration for the life of the running firmware. Ordinary consumers do not acquire/release the raw SD arbitration lock; that detail remains inside the storage service and its logger implementation. Physical media loss is latched unavailable rather than triggering an automatic unmount/remount recovery path.

Log records snapshot a fresh GPS latitude/longitude fix at event creation when one is available, so later movement cannot retroactively change an already queued event. Logs without a valid location simply omit the location fields.

Wall-clock epoch/source state is not ordinary configuration. RFScout uses the RTC-backed system clock for display, log timestamps, and filesystem calendar metadata. `device_time` separately persists the small Auto/Manual policy record; timezone and clock-presentation preferences remain normal durable configuration.

System Events/dmesg records internal-storage, SD, provider, and other platform diagnostics independently from removable log storage.

## Hardware and memory boundaries

Both supported profiles require usable external PSRAM. App-scoped memory/session infrastructure allows growable app-owned state to consume PSRAM during a session and reclaims it after platform services prove they no longer reference it.

T-Embed LCD/microSD/CC1101/nRF24 share SPI; battery/power devices and external sensors share I2C; PDM capture uses I2S0 behind the audio-capture service; speaker output uses I2S1.

T-Deck LCD, microSD, and the currently disabled SX1262 share SPI. Touch, ES7210 control, and other compatible board devices use the profile's shared I2C bus. Standard-I2S speaker output is enabled independently. ES7210 capture uses provider-owned I2S0 RX/DMA in ordinary Philips-I2S mode and exposes MIC1/LEFT as normalized mono PCM. Known but unimplemented keyboard and SX1262 metadata does not by itself make those capabilities available.

## Not currently implemented

For T-Deck, keyboard input, SX1262 radio plumbing, and semantic sleep/power-off behavior remain unimplemented provider slices. ES7210 capture is implemented but still requires final hardware validation of microphone/slot selection, sustained Recorder/Spectrum operation, and GPIO0 handoff behavior before the PR is considered field-validated.

Broader v0.4.0 work still outstanding includes Instant/Timed/Never WiFi/BLE retention semantics, semantic Wi-Fi/BLE entity/correlation views, broader app-by-app PSRAM/history improvements, further Audio Input calibration/generalization, thermal calibration, and release-level stress/fault-injection validation. The canonical checklist and status are in [ROADMAP.md](ROADMAP.md).
