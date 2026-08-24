# RFScout

RFScout is a handheld **RF situational-awareness** instrument for the LILYGO T-Embed CC1101 Plus / ESP32-S3. It combines passive Wi-Fi/BLE observation, sub-GHz spectrum awareness, 2.4 GHz nRF24 activity analysis, optional thermal imaging, battery/status telemetry, and SD logging in a rotary-encoder-driven interface.

<p align="center">
  <img src="assets/device.jpg" alt="RFScout running on a LILYGO T-Embed CC1101 Plus" width="800">
</p>

<p align="center"><em>A functional RFScout device running on T-Embed CC1101 Plus hardware.</em></p>

RFScout is intentionally passive wherever the underlying radio APIs allow it. It is designed to answer "what electronic activity is around me?" without requiring the user to inspect raw packets continuously.

## Current applications

Rotating the encoder switches applications in this order:

1. **WiFi / BT**
2. **CC1101**
3. **nRF24**
4. **Thermal** — shown only when an MLX90640 is detected on the shared I2C/Qwiic bus

The center button opens an application's settings/action menu where one exists. Radio subsystems are stopped when their application is left so inactive views do not continue consuming RF, CPU, and power resources.

### WiFi / BT

The WiFi / BT application is the primary nearby-device view. It passively observes Wi-Fi and BLE traffic, maintains a rolling set of recently seen devices, resolves IEEE OUI/vendor information for addresses where that attribution is defensible, and shows ranked nearby vendors.

The right-side surveillance panel uses fixed categories: zero-count rows remain subdued and detected categories turn red. Random/private MAC addresses are not assigned an OUI vendor, and unresolved devices contribute to the visible `UNKNOWN` count instead of disappearing from the census.

Ordinary nearby-device observations expire after the live RF window; surveillance detections are retained longer so brief events remain visible. Leaving this application stops both the Wi-Fi monitor and BLE scanner.

### CC1101

The CC1101 application is a direct-RSSI sub-GHz spectrum view. The display is split at the same point as the nRF24 view:

- the left side shows the most recent completed RSSI sweep;
- brighter markers provide a slow-decay peak hold;
- the right side shows cumulative **Likely Devices** category counts.

The current 433 MHz preset defaults to **432.800–433.200 MHz at 5 kHz steps**. Available manual step sizes include 1, 5, 10, 25, 50, 100, 200, 250, 500, and 1000 kHz. A sweep is limited to **128 samples** to keep update time bounded; an attempted LOW/HIGH/STEP change that would exceed that budget is refused and indicated in the settings UI rather than silently changing another setting.

Likely-device categories are deliberately conservative (for example remote/sensor, bursty RF, carrier, narrowband, and wide RF). These are RF-behavior classifications, not claims that RFScout has decoded a specific product. `RESET COUNTS` clears the category tally and immediately returns to the live spectrum. Leaving the application clears its runtime spectrum/peak and count state while retaining the selected tuning configuration.

### nRF24

The nRF24 application scans the 2.4 GHz nRF24 channel range and displays:

- a fast-decay activity histogram;
- a brighter slow-decay peak hold;
- a right-side **Likely Devices** category count panel.

Validated Enhanced ShockBurst observations can contribute to `Logitech?`, `Input?`, `Sensor?`, and generic `ESB device` counts. Broader spectrum-only patterns are grouped under `Other 2.4 RF`. Question marks are intentional: except where the evidence is strong, these are behavior-based inferences rather than definitive product identification.

ESB addresses are counted as identities rather than packet totals, and later classification can move an identity from a generic category into a more specific one without double-counting it. The settings menu provides channel bounds, dwell time, `RESET COUNTS`, and `BACK`; reset counts returns directly to the live display. Leaving the nRF24 application clears its runtime histogram, peak, classifier, and count state.

### Thermal

The Thermal application becomes available dynamically when an **MLX90640** is detected on the external I2C/Qwiic connection. RFScout periodically probes for optional thermal hardware while other applications are active, and the Thermal view remains last in the application order. The renderer interpolates the sensor's native 32x24 thermal frame for a more useful full-screen image while retaining the underlying sensor data rate and resolution limits.

## Shared UI and status bar

Application titles use subdued styling so the content remains visually dominant. CC1101 and nRF24 use the same left/right divider and the same `LIKELY DEVICES` visual language: fixed category rows, subdued zero values, and red nonzero counts.

The top status area sits between the application title and the battery indicator. Status icons populate from right to left. A mounted SD card is shown with a subdued SD icon; filesystem/logging errors make that icon red and sticky for the runtime. Optional hardware such as the thermal sensor can also advertise availability through the status area.

The battery indicator shows exact state of charge plus proportional fill. Battery and charger telemetry come from the board's BQ27220/BQ25896 devices through the shared board-I2C layer. Missing telemetry is nonfatal.

## SD logging

RFScout mounts an available microSD card at `/sdcard`. SD failure is nonfatal and does not prevent the radio applications from operating.

LILYGO documents **SanDisk cards of 32 GB or less** as the tested baseline for the T-Embed CC1101 family; other cards and larger capacities may work but are outside that stated compatibility baseline. See [docs/SD_CARD.md](docs/SD_CARD.md).

Logging runs asynchronously so filesystem latency does not block RF collection. Files use a bounded two-digit ring:

```text
rfscout-00.log
rfscout-01.log
...
rfscout-99.log
```

Each segment is capped at **4 MiB**, for an approximate maximum normal footprint of 400 MiB. Rotation replaces the oldest ring slot and the storage layer also attempts to preserve free filesystem space.

## Hardware/resource ownership

The LCD, microSD, CC1101, and nRF24 share board SPI resources. Pin definitions and bus ownership are centralized so application switching can stop inactive radio work cleanly rather than leaving competing peripherals active.

The board I2C controller is shared by power telemetry and optional external I2C devices such as the MLX90640. Feature modules use the shared bus layer rather than independently reinitializing the controller.

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

RFScout targets ESP32-S3 with ESP-IDF:

```sh
python3 tools/update_oui.py
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

After changes to `sdkconfig.defaults` or `partitions.csv`, regenerate configuration before building:

```sh
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

## Documentation

- [Capabilities](docs/CAPABILITIES.md)
- [Architecture](docs/ARCHITECTURE.md)
- [SD card behavior](docs/SD_CARD.md)
- [Coding style](docs/CODING_STYLE.md)

## Coding style

New and touched firmware code should follow [docs/CODING_STYLE.md](docs/CODING_STYLE.md): readable Allman formatting, `a_` argument prefixes, explicit interfaces, centralized hardware definitions, and documented public APIs.

## License

MIT
