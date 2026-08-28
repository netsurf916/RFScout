# RFScout

RFScout is a handheld **RF situational-awareness** instrument for the LILYGO T-Embed CC1101 Plus / ESP32-S3. It combines passive Wi-Fi/BLE observation, sub-GHz spectrum awareness, 2.4 GHz nRF24 activity analysis, SD-backed audio recording and monitoring, optional thermal imaging, battery/status telemetry, SD logging, and device-wide configuration in a rotary-encoder-driven interface.

<p align="center">
  <img src="assets/device.jpg" alt="RFScout running on a LILYGO T-Embed CC1101 Plus" width="800">
</p>

<p align="center"><em>A functional RFScout device running on T-Embed CC1101 Plus hardware.</em></p>

RFScout is intentionally passive wherever the underlying radio APIs allow it. It is designed to answer "what electronic activity is around me?" without requiring the user to inspect raw packets continuously.

## Current applications

RFScout currently starts in **WiFi / BT** unless that application is disabled. Rotating the encoder switches applications in this order, skipping applications disabled in Device and optional hardware that is unavailable:

1. **WiFi / BT**
2. **CC1101**
3. **nRF24**
4. **Recorder**
5. **Thermal** — shown only when enabled and an MLX90640 is detected
6. **Device** — global settings, wall-clock control, application enablement, notifications, and persistent configuration

The center button opens an application's settings/action surface where one exists. Settings menus use **Back** as the first entry. Device is a pass-through landing screen: rotating onto Device does not trap the encoder in settings; center must be pressed deliberately to enter configuration.

### WiFi / BT

WiFi / BT passively observes Wi-Fi and BLE traffic, maintains a rolling set of recently seen devices, and resolves IEEE vendor information using longest-prefix matching across MA-L (24-bit), MA-M (28-bit), MA-S/OUI-36 (36-bit), and legacy IAB assignments. Random/private addresses contribute to the fixed `RANDOM` row rather than receiving misleading vendor attribution; globally administered addresses without a matching assignment contribute to `UNKNOWN`.

Surveillance classification combines curated prefix metadata with higher-layer evidence such as BLE names, manufacturer IDs, services, and Remote ID. The action menu provides **RESET DETECTIONS**. Leaving the application stops both Wi-Fi and BLE collection.

### CC1101

CC1101 provides direct-RSSI sub-GHz spectrum sweeps with optional slow-decay peak hold and a fixed **DETECTIONS** category panel. The settings menu exposes profile, low/high frequency, step size, peak hold, and count reset. Sweeps are bounded to 128 samples; invalid range/step combinations are rejected instead of silently changing another setting.

Detection labels are conservative RF-behavior classifications rather than decoded product identities. Detection-count advances can trigger the configured global notification.

### nRF24

nRF24 scans the 2.4 GHz channel range using RPD activity, a fast-decay histogram, optional slow peak hold, and conservative passive Enhanced ShockBurst classification when validated observations are available. The fixed **DETECTIONS** panel includes categories such as `ESB device`, `Input?`, `Sensor?`, `Logitech?`, and broader 2.4 GHz activity.

The settings menu exposes low/high channel, dwell time, peak hold, and count reset. Runtime activity/classifier state is cleared when the application is left.

### Recorder

Recorder uses the internal PDM microphone through ESP32-S3 I2S0 and requires mounted SD storage for recording and playback.

Recorder currently provides:

- 16-bit mono PCM WAV recording under `/sdcard/audio/`;
- runtime PDM-clock measurement and delivered-PCM-rate measurement rather than assuming the nominal I2S request equals the physical result;
- WAV headers stamped with the measured effective PCM rate;
- 300 ms head and 600 ms tail trimming, DC removal, and bounded normalization during asynchronous save processing;
- immediate recording-list refresh after a completed save;
- playback through the onboard I2S1 speaker with a Recorder-specific persistent playback volume;
- deletion of recordings from the browser;
- a **MONITOR** mode with a live microphone FFT spectrum;
- persistent fixed microphone input gain from 0 through +24 dB in 3 dB steps, adjusted with the rotary encoder while monitoring.

The current I2S request is 48 kHz, but the effective PCM rate is measured at runtime and may differ. Recorder accepts supported 16-bit mono WAV files with sample rates from 8 through 48 kHz. It is not a calibrated sound-level meter.

### Thermal

Thermal becomes available when an **MLX90640** is detected on the external I2C/Qwiic connection and the application is enabled. The renderer interpolates the sensor's native 32x24 frame for the LCD. Thermal currently presents sensor data rather than a discrete detection classifier.

### Device

Device has a pass-through landing screen and a center-entered settings editor. Current settings include display brightness, 12/24-hour clock presentation, timezone selection, manual date/time, application enablement, SD logging, global notification mode and volume, one Morse character for each notification category, Recorder playback volume/input gain through Recorder persistence, and explicit configuration save.

RFScout always has compiled defaults and remains usable without an SD card. When storage is mounted, persistent preferences live in `/sdcard/rfscout.json`. Device itself cannot be disabled.

## Shared UI and status

Application views use shared top chrome for title placement, wall-clock/status presentation, and battery indication. Long titles are constrained by the shared chrome so they do not overlap the right-side status region. Application content remains app-specific, and there is no globally reserved footer area; data-heavy views can use the bottom of the display.

The status area shows local wall time plus global state such as mounted SD storage and sticky filesystem/logging errors. When wall time is unavailable, the clock displays `--:--`. Battery state-of-charge and charging telemetry come from the board power-management devices through the shared I2C layer; missing telemetry is nonfatal.

## Audible detection notifications

RFScout uses a device-wide notification policy: **OFF**, **TONE**, or **MORSE**. Morse notifications emit one configured character for the detection category. Notification volume is independent of Recorder playback volume and is persisted in `/sdcard/rfscout.json`.

Speaker output is asynchronous through I2S1. Volume uses a perceptual nonlinear mapping so low percentages are materially quieter while `0%` remains mute. Each queued alert captures the volume active when it was requested.

## SD logging and configuration

RFScout mounts an available microSD card at `/sdcard`; mount failure is nonfatal. Logging is asynchronous and uses a bounded ring of `rfscout-00.log` through `rfscout-99.log`, with 4 MiB per segment and an approximate normal maximum of 400 MiB. With valid wall time records use UTC ISO-8601 timestamps; before time is established they use an explicit `UP+<milliseconds>` monotonic fallback.

LILYGO documents SanDisk cards of 32 GB or less as the tested baseline for the T-Embed CC1101 family. See [SD card behavior](docs/SD_CARD.md).

## Hardware/resource ownership

The LCD, microSD, CC1101, and nRF24 share board SPI resources managed by `board_spi.c/.h`. Battery/power telemetry and optional external I2C devices share the board I2C layer. The internal PDM microphone uses I2S0 while Recorder is recording or monitoring; the onboard speaker uses I2S1 through the asynchronous audio-output service.

The current Recorder still owns microphone configuration directly. A future shared calibrated Audio Input service is tracked in the [roadmap](docs/ROADMAP.md); it is not yet a production module.

## OUI / prefix identity database

RFScout compiles IEEE MA-L, MA-M, MA-S/OUI-36, and legacy IAB assignment data. Runtime lookup uses longest-prefix precedence: 36-bit, then 28-bit, then 24-bit. CID is intentionally excluded from ordinary vendor attribution. Curated surveillance-prefix metadata is generated separately and consumed through the same identity lookup layer without being represented as authoritative IEEE ownership.

Refresh generated tables locally with:

```sh
python3 tools/update_oui.py
python3 tools/update_surveillance.py
```

The **Refresh IEEE OUI database** GitHub Action keeps the generated identity data synchronized.

## Build, releases, and browser flashing

RFScout targets ESP32-S3 with ESP-IDF 6.x; CI currently builds with ESP-IDF 6.1 and enforces clang-format 18. Managed-component versions are pinned by the tracked `dependencies.lock` file.

```sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Ready-for-review pull requests and `main` builds produce short-lived candidate bundles. Published releases are created deliberately through the **Prepare firmware release** workflow. The **Publish browser flasher** workflow deploys published releases plus the candidate bundle from the most recent triggering eligible build to GitHub Pages for Web Serial flashing from Chromium-based desktop browsers.

See [Getting started](docs/GETTING_STARTED.md) and [Firmware releases](docs/RELEASING.md).

## Documentation

Production documentation describes behavior that exists on current `main`. Forward-looking design, priorities, and brainstorming are kept in the roadmap rather than duplicated across operational documents.

- [Capabilities](docs/CAPABILITIES.md) — implemented user-visible capabilities and limitations
- [Architecture](docs/ARCHITECTURE.md) — current module ownership and resource boundaries
- [Device configuration and wall clock](docs/CONFIGURATION.md) — current persistent configuration and time behavior
- [SD card behavior](docs/SD_CARD.md) — storage, Recorder, and logging behavior
- [Getting started](docs/GETTING_STARTED.md) — local development/build/flash workflow
- [Firmware releases](docs/RELEASING.md) — candidate, release, and browser-flasher workflow
- [Prefix identity](docs/PREFIX_IDENTITY.md) — IEEE/curated prefix identity behavior
- [Coding style](docs/CODING_STYLE.md) — source-formatting/documentation conventions
- [Roadmap](docs/ROADMAP.md) — completed work, active plans, and future ideas

## License

MIT
