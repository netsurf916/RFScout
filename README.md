# RFScout

RFScout is a handheld **RF situational-awareness** instrument for the LILYGO T-Embed CC1101 Plus / ESP32-S3. It combines passive Wi-Fi/BLE observation, sub-GHz spectrum awareness, 2.4 GHz nRF24 activity analysis, microphone spectrum analysis, optional thermal imaging, battery/status telemetry, SD logging, and device-wide configuration in a rotary-encoder-driven interface.

<p align="center">
  <img src="assets/device.jpg" alt="RFScout running on a LILYGO T-Embed CC1101 Plus" width="800">
</p>

<p align="center"><em>A functional RFScout device running on T-Embed CC1101 Plus hardware.</em></p>

RFScout is intentionally passive wherever the underlying radio APIs allow it. It is designed to answer "what electronic activity is around me?" without requiring the user to inspect raw packets continuously.

## Current applications

Rotating the encoder switches applications in this order, skipping applications disabled in Device and optional hardware that is unavailable:

1. **WiFi / BT**
2. **CC1101**
3. **nRF24**
4. **Audio**
5. **Thermal** — shown only when enabled and an MLX90640 is detected on the shared I2C/Qwiic bus
6. **Device** — global settings, wall-clock control, application enablement, notifications, and persistent configuration

The center button opens an application's settings/action menu where one exists. Every menu opens with **Back** selected first, so an accidental center press can be undone with another center press. Radio and sensor subsystems are stopped when their application is left where practical so inactive views do not continue consuming RF, CPU, and power resources.

Device is a pass-through landing screen: rotating onto Device does not trap the encoder in settings. Another rotary tick continues normal app navigation; center must be pressed deliberately to enter Device settings.

### WiFi / BT

The WiFi / BT application is the primary nearby-device view. It passively observes Wi-Fi and BLE traffic, maintains a rolling set of recently seen devices, resolves IEEE vendor information using longest-prefix matching across MA-L (24-bit), MA-M (28-bit), MA-S/OUI-36 (36-bit), and legacy IAB assignments where that attribution is defensible, and shows ranked nearby vendors.

The right-side surveillance panel uses fixed categories: zero-count rows remain subdued and detected categories turn red. Random/private MAC addresses are not assigned an IEEE vendor and contribute to the fixed `RANDOM` row. Globally administered/public addresses with no matching IEEE assignment contribute to `UNKNOWN` instead of disappearing from the census.

Ordinary nearby-device observations expire after the live RF window; surveillance detections are retained longer so brief events remain visible. Leaving this application stops both the Wi-Fi monitor and BLE scanner. Its action menu includes **RESET DETECTIONS**, which clears the current device/OUI census and retained surveillance detections without restarting the collectors.

Surveillance detections participate in RFScout's global audible-notification system. Curated prefix surveillance metadata is resolved by the same identity database used for IEEE vendor attribution and contributes one evidence source to the classifier; BLE names, manufacturer IDs, services, Remote ID, and other protocol evidence remain independent classifier inputs. Notification mode, volume, and Morse mappings are configured in Device rather than in the WiFi/BT menu.

### CC1101

The CC1101 application is a direct-RSSI sub-GHz spectrum view. The display is split at the same point as the nRF24 view:

- the left side shows the most recent completed RSSI sweep;
- brighter markers can provide a slow-decay peak hold;
- the right side shows cumulative **DETECTIONS** category counts.

The current 433 MHz preset defaults to **432.800–433.200 MHz at 5 kHz steps**. Available manual step sizes include 1, 5, 10, 25, 50, 100, 200, 250, 500, and 1000 kHz. A sweep is limited to **128 samples** to keep update time bounded; an attempted LOW/HIGH/STEP change that would exceed that budget is refused and indicated in the settings UI rather than silently changing another setting.

Detection categories are deliberately conservative (for example remote/sensor, bursty RF, carrier, narrowband, and wide RF). These are RF-behavior classifications, not claims that RFScout has decoded a specific product. When a category count advances, RFScout can produce that category's configured global notification. `RESET COUNTS` clears the tally and notification baseline and immediately returns to the live spectrum. Leaving the application clears its runtime spectrum/peak and count state while retaining selected tuning configuration.

### nRF24

The nRF24 application scans the 2.4 GHz nRF24 channel range and displays:

- a fast-decay activity histogram;
- an optional brighter slow-decay peak hold;
- a right-side **DETECTIONS** category count panel.

Validated Enhanced ShockBurst observations can contribute to `Logitech?`, `Input?`, `Sensor?`, and generic `ESB device` counts. Broader spectrum-only patterns are grouped under `Other 2.4 RF`. Question marks are intentional: except where evidence is strong, these are behavior-based inferences rather than definitive product identification.

ESB addresses are counted as identities rather than packet totals, and later classification can move an identity from a generic category into a more specific one without double-counting it. Underlying Wi-Fi-like and BLE/hopping-RF-like spectrum categories remain distinct for notification-character mapping even when combined in the visible `Other 2.4 RF` row. The settings menu provides channel bounds, dwell time, peak-hold control, and `RESET COUNTS`; leaving the app clears runtime histogram, peak, classifier, and count state.

### Audio

The Audio application uses the T-Embed CC1101 Plus internal PDM microphone through ESP-IDF's I2S PDM receive path and DMA buffering. It performs a Hann-windowed FFT and displays an automatically scaled spectrum without requiring filesystem or display work in the acquisition task.

Available focused ranges are **Voice**, **Mid**, **High**, **Audible**, and **Ultrasonic**. Voice and Mid use filtered decimation to improve useful frequency resolution while the higher ranges retain the native 48 kHz PCM rate. The settings menu also exposes the PDM receive slot as `LEFT` or `RIGHT` for hardware/debug validation and allows peak hold to be enabled or disabled. Peak hold defaults off for Audio so the live trace remains visually responsive.

The current Audio implementation remains an analyzer rather than a calibrated sound-level instrument and does not yet generate a discrete detection event. Future Audio detectors can use the same global notification interface. Follow-up work is planned to add microphone recording and an interactive International Morse player.

### Thermal

The Thermal application becomes available dynamically when an **MLX90640** is detected on the external I2C/Qwiic connection and Thermal is enabled in Device. RFScout periodically probes for optional thermal hardware while other applications are active. The renderer interpolates the sensor's native 32x24 thermal frame for a more useful full-screen image while retaining the underlying sensor data rate and resolution limits.

Thermal currently presents sensor data rather than discrete detection events. Future Thermal detectors can use the same global notification interface.

### Device

The Device application has a pass-through landing screen and a center-entered global settings editor. Settings currently include LCD brightness, 12/24-hour clock presentation, timezone selection, manual date/time, application enablement, SD logging enablement, global notification mode, global notification volume, one Morse character for every defined notification category, and explicit configuration save.

RFScout always has compiled sane defaults. Notification mode defaults to **OFF** only when no usable saved value is available. Notification volume defaults to **50%** only when no usable saved value exists; valid saved values from `0` through `100` are restored on boot. When an SD card is mounted, `rfscout.json` can restore mode, volume, and every per-category character on the next boot. Default category mappings include Flock `F`, acoustic sensor `A`, camera `C`, drone `D`, and smart glasses `G`, with distinct defaults for the other surveillance, CC1101, and nRF24 categories.

The Device application itself cannot be disabled. See [Device configuration and wall clock](docs/CONFIGURATION.md) for the JSON schema, validation behavior, complete per-app settings inventory, alert-item/config-key correspondence, wall-clock lifecycle, timezone definitions, and future RTC integration model.

## Shared UI and status bar

Application titles use subdued styling so content remains visually dominant. CC1101 and nRF24 use the same left/right divider and the same **DETECTIONS** visual language: fixed category rows, subdued zero values, and red nonzero counts.

The top status area is shared across application views and sits between the application title and battery indicator. It shows RFScout's current local wall clock plus right-aligned status icons. A mounted SD card is shown with a subdued SD icon; filesystem/logging errors make that icon red and sticky for the runtime. When wall time has not been established, the clock is shown as `--:--` rather than inventing a date.

The battery indicator shows exact state of charge plus proportional fill. Battery and charger telemetry come from the board's BQ27220/BQ25896 devices through the shared board-I2C layer. Missing telemetry is nonfatal.

## Audible detection notifications

RFScout uses a device-wide audible-notification policy rather than app-local alert settings. **OFF** suppresses sounds, **TONE** emits the common short tone, and **MORSE** emits exactly one character chosen by detection category.

A global `0-100%` notification volume applies to both Tone and Morse. The compiled fallback is **50%**; a valid value in `/sdcard/rfscout.json` is restored on boot. Device adjusts volume in 10% steps. Each queued sound captures the current volume so changing the setting affects later alerts without changing commands already waiting in the speaker queue.

Current notification categories cover WiFi/BLE surveillance classifications plus CC1101 and nRF24 detection classifications. Each category has an independently editable character in Device and the mapping is persisted in `/sdcard/rfscout.json`. The shared Morse table covers the printable ASCII characters explicitly represented by ITU-R M.1677-1.

The alert enum and descriptor registry are deliberately 1:1. The descriptor owns each alert item's application, stable JSON key, label, and default character. When classifier categories change, the producer mappings in `rfscout.c` for WiFi/BT and `app_manager.c` for CC1101/nRF24 must be reviewed alongside the enum/descriptor table; Device and JSON character iteration are descriptor-driven.

Speaker playback is asynchronous. Detection paths only attempt to enqueue a bounded command; a dedicated low-priority I2S output task generates the waveform. If the queue is full or speaker initialization failed, RF collection continues and the audible notification is dropped rather than blocking acquisition.

The microphone analyzer owns I2S0 PDM receive while the speaker service uses I2S1 standard transmit, keeping input analysis and short output notifications isolated at the peripheral level.

## SD logging and configuration

RFScout mounts an available microSD card at `/sdcard`. SD failure is nonfatal and does not prevent the radio applications from operating.

LILYGO documents **SanDisk cards of 32 GB or less** as the tested baseline for the T-Embed CC1101 family; other cards and larger capacities may work but are outside that stated compatibility baseline. See [docs/SD_CARD.md](docs/SD_CARD.md).

Logging runs asynchronously so filesystem latency does not block RF collection. Every application can contribute compact application-owned records through the common log queue in addition to detailed Wi-Fi/BLE lifecycle records. With valid wall time, records use UTC ISO-8601 timestamps; before time is established, records use an explicit `UP+<milliseconds>` monotonic fallback.

Configuration preferences, including notification mode, volume, and category characters, are stored in `/sdcard/rfscout.json`. Files use a bounded two-digit logging ring (`rfscout-00.log` through `rfscout-99.log`) with 4 MiB per segment and an approximate maximum normal log footprint of 400 MiB.

## Wall clock

The ESP32 system wall clock is authoritative for user-visible time, log timestamps, and normal FAT/filesystem calendar metadata. It is deliberately separate from monotonic timing: RFScout continues to use monotonic timers for RF dwell periods, retries, debounce, housekeeping, timeouts, and other intervals.

Manual date/time is always available in Device. On a normal hard power-off, RFScout drains the log queue, updates the saved epoch and source in `rfscout.json`, synchronizes configuration, and then unmounts storage. Deep sleep does not perform this restore/checkpoint cycle because the ESP32-S3 RTC domain continues running while the SoC remains powered.

## Hardware/resource ownership

The LCD, microSD, CC1101, and nRF24 share board SPI resources. `board_spi.c/.h` owns initialization of that shared SPI2 bus so SD/display/radio behavior does not depend on accidental peripheral initialization order. Pin definitions and chip selects remain centralized in `board.h`.

The board I2C controller is shared by power telemetry and optional external I2C devices such as the MLX90640. Feature modules use the shared bus layer rather than independently reinitializing the controller.

The internal microphone is acquired through ESP32-S3 I2S0/PDM using DMA; FFT processing is isolated from display rendering. The onboard speaker is driven independently through I2S1 standard transmit by the asynchronous audio-output service.

## OUI / prefix identity database

RFScout compiles IEEE MAC-assignment data from MA-L/OUI (24-bit), MA-M (28-bit), MA-S/OUI-36 (36-bit), and legacy IAB registries. Runtime lookup uses longest-prefix precedence: 36-bit first, then 28-bit, then 24-bit. CID is intentionally not used for vendor attribution because CID assignments are not globally unique MAC-address allocations.

The same identity layer also consumes the generated curated surveillance-prefix table. IEEE vendor identity and curated surveillance significance remain distinct metadata: a prefix may resolve a vendor without implying surveillance, while a curated prefix can contribute surveillance evidence even when it is locally administered and therefore has no IEEE vendor assignment. Higher-layer surveillance matching remains in `surveillance.c`.

Refresh the generated tables locally with:

```sh
python3 tools/update_oui.py
python3 tools/update_surveillance.py
```

The **Refresh IEEE OUI database** GitHub Action refreshes all IEEE prefix tables, regenerates curated surveillance prefix metadata, and updates the advisory cross-reference report.

## Flash layout

The T-Embed CC1101 Plus has 16 MB of flash. RFScout uses the repository's custom `partitions.csv` because the compiled OUI database and firmware exceed ESP-IDF's small default factory partition. The application currently uses a single large factory image rather than OTA slots.

## Build

RFScout targets ESP32-S3 with ESP-IDF 6.x. `clang-format` 18 is required for normal development builds because `idf.py build` checks locally changed C/header files before completing.

For a complete Fedora Linux workstation setup, see [Getting started on Fedora Linux](docs/GETTING_STARTED.md).

```sh
python3 tools/update_oui.py
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Before opening or updating a pull request:

```sh
python3 tools/check_format.py --all
idf.py build
```

## Documentation

- [Getting started on Fedora Linux](docs/GETTING_STARTED.md) — dependencies, ESP-IDF setup, formatting, build, flash, and monitor workflow
- [Device configuration and wall clock](docs/CONFIGURATION.md) — persistent preferences, notification mappings, application enablement, timezone rules, and wall-clock lifecycle
- [Capabilities](docs/CAPABILITIES.md) — user-visible features and current limitations
- [Architecture](docs/ARCHITECTURE.md) — module ownership, application lifecycle, notifications, and shared resources
- [Roadmap](docs/ROADMAP.md) — current development status and priorities
- [SD card behavior](docs/SD_CARD.md) — storage compatibility, configuration persistence, rotation, and record behavior
- [Coding style](docs/CODING_STYLE.md) — required source formatting and documentation conventions
- [Firmware releases](docs/RELEASING.md) — candidate artifacts, deliberate draft releases, bundle contents, and publication checks

## Coding style

All hand-maintained firmware code follows [docs/CODING_STYLE.md](docs/CODING_STYLE.md): readable Allman formatting, `a_` argument prefixes, explicit interfaces, complete file headers, centralized hardware definitions, and documented public APIs. Generated `.inc` tables are exempt from hand formatting and should be changed through their generators.

Pull requests run the full clang-format 18 audit plus a complete ESP32-S3 build with ESP-IDF 6.1.

## License

MIT
