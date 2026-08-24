# Architecture

RFScout uses a unified **observation/classification layer** so nearby-device identity, surveillance classification, UI state, and logging all operate from the same normalized observations rather than duplicate device tables.

## Data flow

```text
Wi-Fi ----\
BLE -------+--> normalized observation --> identity/vendor --> surveillance classifier
CC1101 ----|                                  |                    |
nRF24 -----|                                  v                    v
NFC -------|                            rolling device store     alert events
IR --------/                                  |
                                               v
                                      UI / logging / summaries
```

Board telemetry follows a separate path because it describes RFScout itself rather than an observed device:

```text
BQ27220 ----\
             +--> battery module --> cached battery_status_t --> UI
BQ25896 ----/
                 ^
                 |
          shared board_i2c bus
```

## Unified device store

The core maintains a fixed-memory rolling set of approximately 1,000 unique addressable devices. A normalized device record includes:

- address/identity;
- source radio;
- vendor/brand when meaningful;
- RSSI/channel where meaningful;
- first/last seen timestamps;
- optional surveillance classification.

Wi-Fi and BLE observations of the same meaningful public MAC should not create two census entries. Random/private addresses need different treatment because they cannot safely be attributed to an IEEE OUI.

The live device store expires records after the configured current-RF window and uses swap removal within a fixed PSRAM allocation. Surveillance detections are copied into a separate, smaller fixed-capacity history so they can remain visible longer without inflating the live brand, Wi-Fi, BLE, or device counts.

## Classification

Classification is layered:

1. **Identity validity** — decide whether an address is usable for OUI/device identity.
2. **Vendor/OUI** — general manufacturer attribution from the current IEEE registry.
3. **Surveillance signature** — narrow multi-signal detection using curated OUI, name, manufacturer, service, and protocol evidence.

A surveillance result should be explainable. The classifier preserves confidence and evidence information alongside the broad category and specific type/brand signature.

## Alert manager

Alerts are events generated when a device transitions into a noteworthy classification. The alert manager will own:

- unique-device suppression;
- cooldown/re-alert policy;
- mute state;
- Morse selection/timing;
- visual/LED/audio fan-out.

Audio should run asynchronously so Morse playback never blocks radio observation callbacks.

## Battery and shared I2C boundary

`board_i2c.c/.h` exclusively owns initialization of the T-Embed shared I2C controller. Feature modules request device handles through that interface instead of independently configuring SDA/SCL or competing for the peripheral.

`battery.c/.h` owns all BQ27220 and BQ25896 addresses, registers, polling, decoding, and cached state. The UI knows only `battery_status_t`; it does not perform I2C operations or include charger/fuel-gauge register definitions. Missing power-management devices are treated as a feature-level unavailable state rather than an application-fatal error.

The one-second battery poll is intentionally decoupled from the faster LCD refresh. Future NFC support should reuse `board_i2c` while remaining isolated from battery behavior.

## Platform boundary

T-Embed-specific code is divided by capability:

- display/UI driver;
- encoder/input;
- speaker/audio;
- RGB LEDs;
- Wi-Fi monitor;
- BLE monitor;
- CC1101;
- nRF24;
- PN532;
- IR;
- SD/logging;
- shared board I2C;
- battery/power.

The core observation/classification modules do not include board pin definitions. Hardware pins and fixed board addresses are centralized in `board.h`.

## Hardware targets

Primary target: **LILYGO T-Embed CC1101 Plus / ESP32-S3**.

A future secondary target can preserve **ESP32-C5** support for users who value dual-band 2.4/5 GHz Wi-Fi observation over the T-Embed form factor. Feature availability should therefore be capability-driven rather than scattered `#ifdef` assumptions throughout the core.

## Implementation phases

1. Baseline data model, platform boundary, Morse/classifier interfaces and documentation.
2. Verify exact T-Embed CC1101 Plus board revision and pin map.
3. Bring up display, encoder, speaker, LEDs and power telemetry.
4. Implement passive Wi-Fi/BLE observation and IEEE OUI generation/database.
5. Build and refine curated multi-signal surveillance signatures.
6. Implement Nearby/Surveillance UI and asynchronous alert manager.
7. Add SD event logging.
8. Add CC1101/nRF24 RF-activity screens.
9. Add NFC/IR tools.
