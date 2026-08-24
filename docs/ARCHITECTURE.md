# Architecture

RFScout is organized around a small **application manager** plus capability-specific radio/sensor modules. The current top-level application order is:

```text
WiFi / BT -> CC1101 -> nRF24 -> Thermal (when MLX90640 is present)
```

The rotary encoder changes applications unless an application menu is open. The center button enters/leaves application settings. Optional applications are capability-driven: Thermal is skipped when the MLX90640 is not detected.

## Resource lifecycle

A core design rule is that inactive applications should not continue doing expensive work.

- Leaving **WiFi / BT** stops the Wi-Fi monitor and BLE scanner.
- Leaving **CC1101** stops its scanner and clears runtime RSSI/peak/classification state.
- Leaving **nRF24** stops its scanner and clears runtime histogram/peak/classification state.
- Thermal acquisition is performed only while the Thermal application is active.

Application-specific configuration such as CC1101 frequency range/step and nRF24 channel/dwell settings remains configured across view changes even when runtime observations are reset.

## Wi-Fi / BLE data path

```text
Wi-Fi monitor ---\
                  +--> normalized observation --> identity/OUI --> surveillance classifier
BLE monitor -----/              |                       |
                                v                       v
                         rolling device store      alert/history state
                                |
                                v
                            WiFi / BT UI
```

The live store is fixed-memory and rolling. OUI/vendor attribution is applied only where the address can defensibly represent an IEEE identity. Random/private addresses remain unknown rather than being assigned a misleading vendor.

Surveillance detections use separate retained state so short-lived observations can remain visible longer than the ordinary nearby-device window.

## CC1101 data path

```text
CC1101 scanner -> completed RSSI sweep -> direct display levels
                                      \-> slow peak hold
                                      \-> conservative RF-shape classifier
                                              |
                                              v
                                      category count panel
```

The visible CC1101 spectrum uses the most recently completed direct RSSI sweep; it is not an accumulated histogram. Historical information is limited to the slow-decay peak markers and the category counts.

Sweep configuration is bounded to 128 samples. LOW/HIGH/STEP adjustments that would exceed the limit are rejected instead of changing another setting automatically. This provides a predictable upper bound on sweep work and display latency.

## nRF24 data path

```text
nRF24 RPD sweep -> fast activity histogram -> slow peak hold
                                      |
                                      +-> spectrum-shape inference

validated ESB observation -> active packet classifier cache
                                      |
                                      +-> persistent address ledger
                                      |
                                      v
                               category count panel
```

The nRF24 activity display intentionally differs from CC1101 because nRF24 RPD provides thresholded channel activity rather than calibrated RSSI. Fast/slow history therefore gives a more useful visual representation than a raw single sweep.

Validated Enhanced ShockBurst metadata is classified conservatively using address, bitrate, payload size, repetition timing, and known high-confidence signatures. The small active classifier cache is separate from a larger address ledger used for count deduplication. This prevents an identity from being counted again merely because its detailed classifier record was evicted.

Category names with question marks (`Logitech?`, `Input?`, `Sensor?`) communicate that these are inferred identities/types unless the evidence is sufficiently specific.

## Likely Devices UX

CC1101 and nRF24 share the same right-side UI convention:

- fixed category rows remain visible even when zero;
- zero counts are subdued;
- nonzero counts are red;
- counts persist while the application session is active;
- `RESET COUNTS` clears the tally and immediately closes the menu so changes can be observed without an extra navigation step;
- leaving the application starts a fresh runtime observation session when it is entered again.

The two apps use the same vertical divider position so switching between them does not visually rearrange the primary content regions.

## Thermal path

The MLX90640 is an optional external I2C/Qwiic device. RFScout periodically probes for it while another application is active. When detected, the Thermal application becomes available as the final application in the rotation.

The sensor's native 32x24 frame is processed for display interpolation/scaling, but the renderer does not imply higher underlying sensor resolution.

## Shared hardware boundaries

### SPI

The LCD, microSD, CC1101, and nRF24 share board SPI resources. Fixed board pins and chip-select ownership are centralized in `board.h` and the associated platform/radio modules. Inactive radio applications are stopped before another view takes ownership of shared resources.

### I2C

`board_i2c.c/.h` owns initialization of the shared board I2C controller. Battery/power modules and optional external I2C devices use that shared controller instead of independently configuring SDA/SCL.

`battery.c/.h` owns BQ27220/BQ25896 register access and exposes cached telemetry to the UI. Missing power-management telemetry is nonfatal.

## Storage boundary

`storage.c/.h` owns SD mount/state. Logging runs asynchronously so filesystem writes do not execute in radio callbacks. Log files rotate through a bounded two-digit ring with 4 MiB segments.

Filesystem errors are surfaced through the shared top status bar rather than terminating RFScout.

## UI boundary

The display driver is kept below application renderers. Application views use common visual conventions:

- subdued app titles;
- subdued normal/status colors;
- red for actionable detections/errors;
- yellow/amber for editing or limits;
- consistent encoder semantics;
- fixed right-side category panels where applicable.

## Current versus future capabilities

Implemented application-level capabilities are Wi-Fi/BLE awareness, CC1101 spectrum awareness, nRF24 activity/fingerprinting, optional MLX90640 thermal imaging, SD logging, battery/status telemetry, and encoder-driven navigation.

NFC, IR, microphone-based features, GPS, and active/replay functions are not part of the current active application set and should not be presented as implemented functionality until they are actually enabled.
