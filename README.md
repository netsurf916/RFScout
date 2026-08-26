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
6. **Device** — global settings, wall-clock control, application enablement, and persistent configuration

The center button opens an application's settings/action menu where one exists. Radio and sensor subsystems are stopped when their application is left where practical so inactive views do not continue consuming RF, CPU, and power resources. Device owns the encoder while global settings are being edited and provides an explicit Back action to resume application navigation.

### WiFi / BT

The WiFi / BT application is the primary nearby-device view. It passively observes Wi-Fi and BLE traffic, maintains a rolling set of recently seen devices, resolves IEEE OUI/vendor information for addresses where that attribution is defensible, and shows ranked nearby vendors.

The right-side surveillance panel uses fixed categories: zero-count rows remain subdued and detected categories turn red. Random/private MAC addresses are not assigned an OUI vendor, and unresolved devices contribute to the visible `UNKNOWN` count instead of disappearing from the census.

Ordinary nearby-device observations expire after the live RF window; surveillance detections are retained longer so brief events remain visible. Leaving this application stops both the Wi-Fi monitor and BLE scanner.

### CC1101

The CC1101 application is a direct-RSSI sub-GHz spectrum view. The display is split at the same point as the nRF24 view:

- the left side shows the most recent completed RSSI sweep;
- brighter markers can provide a slow-decay peak hold;
- the right side shows cumulative **Likely Devices** category counts.

The current 433 MHz preset defaults to **432.800–433.200 MHz at 5 kHz steps**. Available manual step sizes include 1, 5, 10, 25, 50, 100, 200, 250, 500, and 1000 kHz. A sweep is limited to **128 samples** to keep update time bounded; an attempted LOW/HIGH/STEP change that would exceed that budget is refused and indicated in the settings UI rather than silently changing another setting.

Likely-device categories are deliberately conservative (for example remote/sensor, bursty RF, carrier, narrowband, and wide RF). These are RF-behavior classifications, not claims that RFScout has decoded a specific product. `RESET COUNTS` clears the category tally and immediately returns to the live spectrum. Leaving the application clears its runtime spectrum/peak and count state while retaining the selected tuning configuration.

### nRF24

The nRF24 application scans the 2.4 GHz nRF24 channel range and displays:

- a fast-decay activity histogram;
- an optional brighter slow-decay peak hold;
- a right-side **Likely Devices** category count panel.

Validated Enhanced ShockBurst observations can contribute to `Logitech?`, `Input?`, `Sensor?`, and generic `ESB device` counts. Broader spectrum-only patterns are grouped under `Other 2.4 RF`. Question marks are intentional: except where the evidence is strong, these are behavior-based inferences rather than definitive product identification.

ESB addresses are counted as identities rather than packet totals, and later classification can move an identity from a generic category into a more specific one without double-counting it. The settings menu provides channel bounds, dwell time, peak-hold control, `RESET COUNTS`, and `BACK`; reset counts returns directly to the live display. Leaving the nRF24 application clears its runtime histogram, peak, classifier, and count state.

### Audio

The Audio application uses the T-Embed CC1101 Plus internal PDM microphone through ESP-IDF's I2S PDM receive path and DMA buffering. It performs a Hann-windowed FFT and displays an automatically scaled spectrum without requiring filesystem or display work in the acquisition task.

Available focused ranges are **Voice**, **Mid**, **High**, **Audible**, and **Ultrasonic**. Voice and Mid use filtered decimation to improve useful frequency resolution while the higher ranges retain the native 48 kHz PCM rate. The settings menu also exposes the PDM receive slot as `LEFT` or `RIGHT` for hardware/debug validation and allows peak hold to be enabled or disabled. Peak hold defaults off for Audio so the live trace remains visually responsive.

The current audio implementation remains an analyzer rather than a calibrated sound-level instrument. Follow-up work is still expected around microphone/PDM interpretation and FFT validation before tuner or DTMF features are treated as reliable.

### Thermal

The Thermal application becomes available dynamically when an **MLX90640** is detected on the external I2C/Qwiic connection and Thermal is enabled in Device. RFScout periodically probes for optional thermal hardware while other applications are active. The renderer interpolates the sensor's native 32x24 thermal frame for a more useful full-screen image while retaining the underlying sensor data rate and resolution limits.

### Device

The Device application manages global settings rather than analyzer-specific tuning. It currently provides LCD brightness, 12/24-hour clock presentation, timezone selection, manual date/time, application enablement, SD logging enablement, configuration save, and configuration/time-source status.

RFScout always has compiled sane defaults. When an SD card is available, persistent preferences are stored in `/sdcard/rfscout.json`; without SD, Device remains usable for the current powered session but settings cannot be persisted. The Device application itself cannot be disabled.

See [Device configuration and wall clock](docs/CONFIGURATION.md) for the JSON schema, validation behavior, wall-clock lifecycle, timezone definitions, and future RTC integration model.

## Shared UI and status bar

Application titles use subdued styling so the content remains visually dominant. CC1101 and nRF24 use the same left/right divider and the same `LIKELY DEVICES` visual language: fixed category rows, subdued zero values, and red nonzero counts.

The top status area is shared across application views and sits between the application title and the battery indicator. It shows RFScout's current local wall clock plus right-aligned status icons. A mounted SD card is shown with a subdued SD icon; filesystem/logging errors make that icon red and sticky for the runtime. Optional hardware such as the thermal sensor can also advertise availability through the status area. When wall time has not been established, the clock is shown as `--:--` rather than inventing a date.

The battery indicator shows exact state of charge plus proportional fill. Battery and charger telemetry come from the board's BQ27220/BQ25896 devices through the shared board-I2C layer. Missing telemetry is nonfatal.

## SD logging

RFScout mounts an available microSD card at `/sdcard`. SD failure is nonfatal and does not prevent the radio applications from operating.

LILYGO documents **SanDisk cards of 32 GB or less** as the tested baseline for the T-Embed CC1101 family; other cards and larger capacities may work but are outside that stated compatibility baseline. See [docs/SD_CARD.md](docs/SD_CARD.md).

Logging runs asynchronously so filesystem latency does not block RF collection. Every application can contribute compact application-owned records through the common log queue in addition to detailed Wi-Fi/BLE lifecycle records. With valid wall time, records use UTC ISO-8601 timestamps such as `2026-08-26T19:52:41Z`; before time is established, records use an explicit `UP+<milliseconds>` monotonic fallback.

Files use a bounded two-digit ring:

```text
rfscout-00.log
rfscout-01.log
...
rfscout-99.log
```

Each segment is capped at **4 MiB**, for an approximate maximum normal footprint of 400 MiB. Rotation replaces the oldest ring slot and the storage layer also attempts to preserve free filesystem space.

## Wall clock

The ESP32 system wall clock is authoritative for user-visible time, log timestamps, and normal FAT/filesystem calendar metadata. It is deliberately separate from monotonic timing: RFScout continues to use monotonic timers for RF dwell periods, retries, debounce, housekeeping, timeouts, and other intervals.

Manual date/time is always available in Device. On a normal hard power-off, RFScout drains the log queue, updates the saved epoch and source in `rfscout.json`, synchronizes the configuration, and then unmounts storage. On the next cold boot, the saved value bootstraps the system clock. Deep sleep does not perform this restore/checkpoint cycle because the ESP32-S3 RTC domain continues running while the SoC remains powered.

A saved shutdown epoch cannot account for time elapsed while the board was physically unpowered. The UI therefore tracks that a value was restored while preserving the original authority that last established it, such as Manual, GPS, NTP, or a future specific RTC source.

## Hardware/resource ownership

The LCD, microSD, CC1101, and nRF24 share board SPI resources. Pin definitions and bus ownership are centralized so application switching can stop inactive radio work cleanly rather than leaving competing peripherals active.

The board I2C controller is shared by power telemetry and optional external I2C devices such as the MLX90640. Feature modules use the shared bus layer rather than independently reinitializing the controller.

The internal microphone is acquired through the ESP32-S3 I2S/PDM peripheral using DMA; FFT processing is isolated from display rendering so LCD updates do not directly gate sample acquisition.

## OUI database

RFScout compiles an IEEE MA-L/OUI vendor table for Wi-Fi/BLE attribution. Refresh it locally with:

```sh
python3 tools/update_oui.py
```

The **Refresh IEEE OUI database** GitHub Action can also update the generated database.

## Flash layout

The T-Embed CC1101 Plus has 16 MB of flash. RFScout uses the repository's custom `partitions.csv` because the compiled OUI database and firmware exceed ESP-IDF's small default factory partition.

The application currently uses a single large factory image rather than OTA slots.

## Build

RFScout targets ESP32-S3 with ESP-IDF. `clang-format` is also required for normal development builds because `idf.py build` checks locally changed C/header files before completing.

For a complete Fedora Linux workstation setup, including system dependencies, ESP-IDF installation, serial-port setup, formatting, build, flash, and monitor commands, see [Getting started on Fedora Linux](docs/GETTING_STARTED.md).

```sh
python3 tools/update_oui.py
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

To format locally modified firmware sources before building:

```sh
python3 tools/check_format.py --fix
```

To audit the entire hand-maintained firmware tree:

```sh
python3 tools/check_format.py --all
```

After changes to `sdkconfig.defaults` or `partitions.csv`, regenerate configuration before building:

```sh
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

## Documentation

- [Getting started on Fedora Linux](docs/GETTING_STARTED.md) — dependencies, ESP-IDF setup, formatting, build, flash, and monitor workflow
- [Device configuration and wall clock](docs/CONFIGURATION.md) — persistent preferences, application enablement, timezone rules, wall-clock lifecycle, and future RTC behavior
- [Capabilities](docs/CAPABILITIES.md) — user-visible features and current limitations
- [Architecture](docs/ARCHITECTURE.md) — module ownership, application lifecycle, and shared resources
- [SD card behavior](docs/SD_CARD.md) — storage compatibility, rotation, and record behavior
- [Coding style](docs/CODING_STYLE.md) — required C formatting, documentation, and module conventions

## Coding style

All hand-maintained firmware code should follow [docs/CODING_STYLE.md](docs/CODING_STYLE.md): readable Allman formatting, `a_` argument prefixes, explicit interfaces, complete file headers, centralized hardware definitions, and documented public APIs. Generated `.inc` tables are exempt from hand formatting and should be changed through their generators.

The repository provides `.clang-format` and `.editorconfig`. Pull requests run the same clang-format gate used by normal `idf.py build`, plus a complete ESP-IDF build for ESP32-S3.

## License

MIT
