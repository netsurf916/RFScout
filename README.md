# RFScout

RFScout is a handheld **RF situational-awareness** instrument. The currently supported board is the LILYGO T-Embed CC1101 Plus / ESP32-S3, while the firmware now uses a board-target and capability boundary intended to support additional hardware without compiling every board implementation into one universal image.

RFScout combines passive Wi-Fi/BLE observation, sub-GHz spectrum awareness, 2.4 GHz nRF24 activity analysis, SD-backed audio recording and monitoring, optional thermal imaging, battery/status telemetry, SD logging, device-wide configuration, semantic event feedback, and application-scoped notifications in a compact user interface.

<p align="center">
  <img src="assets/device.jpg" alt="RFScout running on a LILYGO T-Embed CC1101 Plus" width="800">
</p>

<p align="center"><em>A functional RFScout device running on T-Embed CC1101 Plus hardware.</em></p>

RFScout is intentionally passive wherever the underlying radio APIs allow it. It is designed to answer "what electronic activity is around me?" without requiring the user to inspect raw packets continuously.

## Current applications

RFScout starts on **Home** by default. Device Settings can select another preferred startup application; if that target is disabled or unavailable at boot, RFScout falls back to Home for that boot. Rotating the primary navigation control switches applications in this order, skipping disabled applications and optional hardware that is unavailable:

1. **WiFi / BT**
2. **CC1101**
3. **nRF24**
4. **Recorder**
5. **Thermal** — shown only when enabled and an MLX90640 is detected
6. **Home / Device Settings** — branded pass-through landing screen plus global settings and persistent configuration

The board-neutral application layer consumes `platform_input_get()` semantic input events rather than depending on a rotary encoder directly. On the current T-Embed target, the rotary encoder and center button are adapted into navigation/select/back semantics.

Holding the encoder center button continuously for about **2 seconds** toggles the device-wide input lock. Feedback is emitted only after the lock transition has actually been accepted. While locked, ordinary navigation/select input is ignored and a padlock appears in the status bar. The separate physical power button remains available.

### WiFi / BT

WiFi / BT passively observes Wi-Fi and BLE traffic, maintains a rolling set of recently seen devices, and resolves IEEE vendor information using longest-prefix matching across MA-L (24-bit), MA-M (28-bit), MA-S/OUI-36 (36-bit), and legacy IAB assignments. Random/private addresses contribute to the fixed `RANDOM` row rather than receiving misleading vendor attribution; globally administered addresses without a matching assignment contribute to `UNKNOWN`.

Surveillance classification combines curated prefix metadata with higher-layer evidence such as BLE names, manufacturer IDs, services, and Remote ID. Ordinary newly observed Wi-Fi and BLE devices can emit lightweight general detection feedback. Those two discovery cues can be enabled or disabled independently in the WiFi / BT notification settings. Surveillance classifications use their own application-specific notification categories and do not also emit the ordinary discovery cue.

The action menu provides **NOTIFICATIONS** and **RESET DETECTIONS**. Leaving the application stops both Wi-Fi and BLE collection.

### CC1101

CC1101 provides direct-RSSI sub-GHz spectrum sweeps with optional slow-decay peak hold and a fixed **DETECTIONS** category panel. The settings menu exposes profile, low/high frequency, step size, peak hold, application notification settings, and count reset. Sweeps are bounded to 128 samples; invalid range/step combinations are rejected instead of silently changing another setting.

Detection labels are conservative RF-behavior classifications rather than decoded product identities. Detection-count advances can produce application-owned notifications subject to the app notification gate, current global notification mode/volume, active theme sound policy, and speaker availability.

### nRF24

nRF24 scans the 2.4 GHz channel range using RPD activity, a fast-decay histogram, optional slow peak hold, and conservative passive Enhanced ShockBurst classification when validated observations are available. The fixed **DETECTIONS** panel includes categories such as `ESB device`, `Input?`, `Sensor?`, `Logitech?`, and broader 2.4 GHz activity.

The settings menu exposes low/high channel, dwell time, peak hold, application notification settings, and count reset. Runtime activity/classifier state is cleared when the application is left.

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

Recorder is intentionally treated as an audio-centric application: incidental interaction/general-event sounds are suppressed by its native app audio policy while explicit content audio remains available. The current I2S request is 48 kHz, but the effective PCM rate is measured at runtime and may differ. Recorder accepts supported 16-bit mono WAV files with sample rates from 8 through 48 kHz. It is not a calibrated sound-level meter.

### Thermal

Thermal becomes available when an **MLX90640** is detected on the external I2C/Qwiic connection and the application is enabled. The renderer interpolates the sensor's native 32x24 frame for the LCD. Thermal currently presents sensor data rather than a discrete detection classifier.

### Home / Device Settings

Home is the non-disableable Device application's branded pass-through landing screen. Center enters Device Settings; ordinary navigation continues top-level application selection.

Current Device Settings include display brightness, display theme, 12/24-hour clock presentation, timezone selection, preferred startup application, manual date/time, application enablement, SD logging, global notification mode and volume, and explicit configuration save. App-specific notification enablement and Morse mappings live in the detecting applications rather than in the global settings screen.

Four display themes are currently available:

- **Indoor** — normal high-contrast orange/white RFScout presentation;
- **Outdoor** — prioritizes daylight contrast;
- **Night** — shifts UI emphasis toward red/orange tones;
- **Discipline** — uses the same visual palette as Night and suppresses incidental interaction and notification audio, providing both light and noise discipline while leaving explicitly requested application content audio available.

Theme changes apply immediately across application chrome, text, status UI, graphs that use shared UI colors, and Home branding.

## Semantic events, feedback, and notifications

RFScout distinguishes physical input, accepted semantic events, user/app policy, and speaker execution.

Accepted actions such as navigation, activation, enter/back, enable/disable, input lock/unlock, detections, warnings, and errors are emitted through the lightweight `event.c/.h` dispatcher. Delivery is synchronous and handlers are expected to remain short and nonblocking.

`feedback.c/.h` subscribes to semantic events and resolves incidental feedback through both the native application audio profile and active theme sound policy. UI feedback is disposable; it is not allowed to build a long backlog behind more important audio.

Detection notifications are application-owned. Global notification presentation remains **OFF**, **TONE**, or **MORSE**, with a global notification volume. Each detecting app has its own notification gate and Morse mappings. WiFi / BT additionally has independent toggles for ordinary Wi-Fi-device and BLE-device discovery beeps. The master app notification gate suppresses those general discovery cues as well as app-specific surveillance notifications without discarding the individual settings.

Application content audio, such as Recorder playback, is distinct from incidental feedback and detection notifications.

## Configuration and deep-sleep state

RFScout always starts from compiled defaults. When an SD card is mounted, persistent user preferences live in `/sdcard/rfscout.json` using **schema version 2**. The active theme is part of that canonical file rather than a separate NVS preference.

The loader starts from current defaults and overlays only recognized, valid fields. Schema v1 files are accepted and translated where needed. Unknown or obsolete keys are ignored; because saves build a new canonical document rather than rewriting the input tree, those keys disappear on the next save. Fields introduced by newer firmware retain their current compiled defaults when absent and are emitted on the next save. Configuration files claiming a schema newer than the running firmware are rejected instead of being destructively rewritten.

Deep sleep is a reboot-like transition, so RFScout separately retains a small RTC-backed session envelope. The currently active app and input-lock state survive a valid deep-sleep wake. Transient menu/edit state and active Recorder operations are intentionally not resumed. Dirty canonical configuration is flushed before SD unmount/deep sleep.

See [Device configuration and wall clock](docs/CONFIGURATION.md) for the schema and migration rules.

## Shared UI and status

Application views use shared top chrome for title placement, wall-clock/status presentation, battery indication, and semantic display colors. The common theme layer defines roles such as background, primary/secondary text, accent, selection, information, success, warning, error, graph, and logo colors. Literal data imagery such as the Thermal heat map bypasses theme translation.

The status area shows local wall time plus global state such as mounted SD storage and sticky filesystem/logging errors. When wall time is unavailable, the clock displays `--:--`. Battery state-of-charge and charging telemetry come from board power-management devices through the shared board services; missing telemetry is nonfatal.

## SD logging and storage

RFScout mounts an available microSD card at `/sdcard`; mount failure is nonfatal. Logging is asynchronous and uses a bounded ring of `rfscout-00.log` through `rfscout-99.log`, with 4 MiB per segment and an approximate normal maximum of 400 MiB. With valid wall time records use UTC ISO-8601 timestamps; before time is established they use an explicit `UP+<milliseconds>` monotonic fallback.

LILYGO documents SanDisk cards of 32 GB or less as the tested baseline for the T-Embed CC1101 family. See [SD card behavior](docs/SD_CARD.md).

## Hardware and board abstraction

RFScout currently supports the **LILYGO T-Embed CC1101 Plus**, selected through the `RFSCOUT_BOARD` Kconfig choice. Board definitions live under `main/boards/<target>/`. Application code should depend on semantic platform/service interfaces and capabilities rather than board model names.

The current board's LCD, microSD, CC1101, and nRF24 share SPI resources; battery/power telemetry and optional external I2C devices share board I2C; the internal PDM microphone uses I2S0 while Recorder is recording or monitoring; and the onboard speaker uses I2S1 through the asynchronous audio-output service.

See [Board targets and porting](docs/BOARD_PORTING.md).

## OUI / prefix identity database

RFScout compiles IEEE MA-L, MA-M, MA-S/OUI-36, and legacy IAB assignment data. Runtime lookup uses longest-prefix precedence: 36-bit, then 28-bit, then 24-bit. CID is intentionally excluded from ordinary vendor attribution. Curated surveillance-prefix metadata is generated separately and consumed through the same identity lookup layer without being represented as authoritative IEEE ownership.

Refresh generated tables locally with:

```sh
python3 tools/update_oui.py
python3 tools/update_surveillance.py
```

## Build, releases, and browser flashing

RFScout targets ESP32-S3 with ESP-IDF 6.x; CI currently builds with ESP-IDF 6.1 and enforces clang-format 18. Managed-component versions are pinned by the tracked `dependencies.lock` file.

```sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Ready-for-review pull requests and `main` builds produce short-lived candidate bundles. Published releases are created deliberately through the **Prepare firmware release** workflow. See [Getting started](docs/GETTING_STARTED.md) and [Firmware releases](docs/RELEASING.md).

## Documentation

Production documentation describes behavior that exists on current `main`. Forward-looking design, priorities, and brainstorming are kept in the roadmap.

- [Capabilities](docs/CAPABILITIES.md) — implemented user-visible capabilities and limitations
- [Architecture](docs/ARCHITECTURE.md) — current module ownership and resource boundaries
- [Board targets and porting](docs/BOARD_PORTING.md) — board-selection and capability boundary
- [Device configuration and wall clock](docs/CONFIGURATION.md) — persistent configuration, migration, sleep state, and time behavior
- [SD card behavior](docs/SD_CARD.md) — storage, Recorder, and logging behavior
- [Getting started](docs/GETTING_STARTED.md) — local development/build/flash workflow
- [Firmware releases](docs/RELEASING.md) — candidate, release, and browser-flasher workflow
- [Prefix identity](docs/PREFIX_IDENTITY.md) — IEEE/curated prefix identity behavior
- [Coding style](docs/CODING_STYLE.md) — source-formatting/documentation conventions
- [Roadmap](docs/ROADMAP.md) — completed work, active plans, and future ideas

## License

MIT
