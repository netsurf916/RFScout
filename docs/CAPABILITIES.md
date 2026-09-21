# RFScout capabilities

This document summarizes behavior implemented on the current development line. Architecture/invariants live in [ARCHITECTURE.md](ARCHITECTURE.md); planned work and release status live only in [ROADMAP.md](ROADMAP.md).

## Supported hardware and application availability

RFScout currently supports the **LILYGO T-Embed CC1101 Plus**. A minimal early I2C fingerprint selects its immutable runtime profile before ordinary board GPIO/peripheral initialization. Unknown hardware remains awake and inert for recovery flashing.

Applications consume semantic service availability rather than board identity. The runtime profile remains the legality gate for GPIO, buses, display geometry, input, RF, audio, storage, battery, and power behavior, so another board can be added later without changing application contracts.

The canonical application registry order remains:

1. System Events
2. Home / Device Settings
3. WiFi / BLE
4. CC1101
5. nRF24
6. Recorder
7. Thermal
8. Audio Spectrum

Disabled or unavailable applications are skipped by semantic navigation.

System Events is a read-only view of the RAM-only structured event ring. Center enters/exits a stable Browse snapshot; semantic navigation scrolls only while Browse is active.

Firmware Update is available from **Home / Device Settings → Update** rather than as a top-level application. The nested menu performs passive AP discovery, retains up to eight OTA-owned infrastructure-network credentials, highlights remembered networks seen in the latest scan, and requires an explicit per-network action before association. Saved-network management, rotary password entry, explicit disconnect/forget, DHCP status, and deterministic teardown are implemented; leaving the Update menu stops acquisition/installer/STA activity and clears transient driver credentials/results. Password entry rotates through printable characters plus explicit **Done** and **Backspace** actions, while the semantic Back gesture cancels entry. A remembered SSID that was not observed passively can be selected with **Connect anyway**, which performs an SSID-targeted active search only after that explicit user action.

After explicit association, the Update menu exposes **Check for update** and **Available builds**. The catalog is fetched from the static HTTPS origin through a cancellable platform service using ESP-IDF certificate-bundle validation and growable app-owned PSRAM; stable checking selects the newest compatible published stable release, while the same validated Pages catalog carries main, pull-request, draft-release, prerelease, and published-release builds for explicit/manual installation.

A selected stable or explicitly chosen update streams directly over HTTPS into the ESP-IDF-selected inactive OTA partition. The installer is transport-neutral and owns partition selection, exact byte-count enforcement, streaming SHA-256, ESP-IDF image validation, embedded project/version checks, exact flash-readback SHA-256, and boot-partition commit. It never stages firmware on SD or LittleFS. A successfully committed image reboots immediately; on first boot, RFScout confirms a pending image only after bounded mandatory common startup has completed. Optional storage, GPS, notification, battery, and similar peripherals are not OTA health dependencies.

## Managed application view

Each app session owns one managed `rfscout_view_t`. The current view is **320x170**, matching the full T-Embed LCD. Applications render only inside that view and remain independent from physical board identity.

## GPS, location, and time

The GPS service remains board-neutral and capability-gated for future profiles or hardware integrations. The current T-Embed profile does not advertise an onboard GPS provider.

`device_time` continues to own RTC-backed wall-clock validity/source, Auto/Manual synchronization policy, timezone conversion, and manual time.

## Battery status

T-Embed uses its BQ27220 fuel gauge and BQ25896 charger through the shared I2C service. Battery percentage, voltage/current, charging state, and authoritative external-power state feed the common status and protective-power policy.

Automatic low-battery actions require fresh percentage plus authoritative evidence that external power is absent. External power cancels a pending low-battery action and the condition rearms only after recovery above the configured threshold.

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

Recorder provides SD-backed 16-bit mono PCM WAV capture/playback, asynchronous post-processing, recording browse/delete, and independent playback volume. It consumes the board-neutral `audio_capture` service rather than an I2S channel. The current T-Embed provider uses PDM capture with runtime clock/effective-rate measurement and measured-rate WAV metadata.

Capture is not software pre-amplified. After capture completes, Recorder trims startup/tail artifacts, removes DC/very-low-frequency energy with the same shared one-pole high-pass primitive used by Audio Spectrum, then applies bounded normalization before finalizing the WAV.

While Recorder is actively recording or finalizing a recording, app transitions and terminal sleep/power-off transitions are intentionally blocked. The physical center control stops recording; saving completes before ordinary navigation/power transitions resume. Playback may be stopped normally when Recorder is left or a terminal power transition is prepared.

## Audio Spectrum

Audio Spectrum is a microphone-only live FFT/spectrum app and does not require an SD card. It uses the board-neutral audio-capture service and the current T-Embed PDM provider, then applies continuous DC/high-pass filtering and automatic display scaling without changing Recorder capture data.

## Thermal

Thermal is available when an enabled MLX90640 is detected on the shared external I2C interface. Native 32x24 data is interpolated for the LCD; literal thermal imagery bypasses semantic theme translation while chrome/labels remain theme-aware.

Absolute-temperature calibration improvements remain roadmap work.

## Home / Device Settings

Home remains the low-allocation device/settings surface. Current settings include display brightness/idle policy, visual theme, independent Normal/Quiet audio policy, clock/timezone/manual time, startup/app enablement, SD logging, breadcrumb policy, power behavior, and notification mappings.

## Power, display, and sleep

Semantic power actions are filtered by runtime profile capability. T-Embed advertises Screen off, Sleep, and Power off with its validated dedicated user/power key behavior. Display dim/off is presentation policy; deep sleep and physical power-off remain explicit semantic transitions.

Protective low-battery action requires fresh battery percentage and authoritative evidence that external power is absent. High-temperature policy is likewise constrained by the actions advertised by the active profile.

## Feedback and notifications

The validated T-Embed speaker provider handles semantic UI/event/alert feedback and Recorder playback through the shared audio-output service. Visual themes and Normal/Quiet audio policy remain independent.

## Configuration, storage, and logging

Ordinary configuration changes are persisted automatically; there is no user-facing **Save config** action. Time synchronization policy and breadcrumb settings are bounded system records separate from normal app configuration. SD remains removable/bulk storage for logs, recordings, breadcrumbs, and future explicit transfer packages rather than configuration authority.

## Hardware and memory boundaries

The supported T-Embed profile requires usable external PSRAM. App-scoped memory/session infrastructure allows growable app-owned state to consume PSRAM during a session and reclaims it only after platform services prove they no longer reference it.

T-Embed LCD/microSD/CC1101/nRF24 share SPI; battery/power devices and compatible external sensors share I2C; PDM capture uses I2S0 behind the audio-capture service; speaker output uses I2S1.

## Not currently implemented

Additional board profiles are not currently supported. New hardware should be added only through the existing profile/capability/provider boundaries after a concrete use case and hardware validation justify the maintenance cost.
