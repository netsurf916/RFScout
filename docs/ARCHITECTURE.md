# Architecture

RFScout should combine Brandalize and OUI-SPY at the **observation/classification layer**, not by running two independent applications or duplicate device tables.

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

## Unified device store

The core maintains a fixed-memory rolling set of approximately 1,000 unique addressable devices. A normalized device record includes:

- address/identity;
- source radio;
- vendor/brand when meaningful;
- RSSI/channel where meaningful;
- first/last seen timestamps;
- optional surveillance classification.

Wi-Fi and BLE observations of the same meaningful public MAC should not create two census entries. Random/private addresses need different treatment because they cannot safely be attributed to an IEEE OUI.

The initial store implementation is intentionally simple. Before production use it should be refined into a true most-recently-seen eviction policy and should preserve/merge source metadata rather than overwriting it when one identity appears on multiple transports.

## Classification

Classification is layered:

1. **Identity validity** — decide whether an address is usable for OUI/device identity.
2. **Vendor/OUI** — Brandalize-style general manufacturer attribution.
3. **Surveillance signature** — OUI-SPY-style narrow detection using the strongest available evidence.

A surveillance result should be explainable. The mature classifier should eventually return a confidence/evidence structure rather than only an enum.

## Alert manager

Alerts are events generated when a device transitions into a noteworthy classification. The alert manager will own:

- unique-device suppression;
- cooldown/re-alert policy;
- mute state;
- Morse selection/timing;
- visual/LED/audio fan-out.

Audio should run asynchronously so Morse playback never blocks radio observation callbacks.

## Platform boundary

`platform.*` is currently a safe stub. T-Embed-specific code should be divided further as implementation grows:

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
- battery/power.

The core observation/classification modules should not include board pin definitions.

## Hardware targets

Primary target: **LILYGO T-Embed CC1101 Plus / ESP32-S3**.

A future secondary target can preserve **ESP32-C5** support for users who value dual-band 2.4/5 GHz Wi-Fi observation over the T-Embed form factor. Feature availability should therefore be capability-driven rather than scattered `#ifdef` assumptions throughout the core.

## Implementation phases

1. Baseline data model, platform boundary, Morse/classifier interfaces and documentation.
2. Verify exact T-Embed CC1101 Plus board revision and pin map.
3. Bring up display, encoder, speaker, LEDs and power telemetry.
4. Port passive Wi-Fi/BLE observation and Brandalize OUI generation/database.
5. Port and refine OUI-SPY surveillance signatures.
6. Implement Nearby/Surveillance UI and asynchronous alert manager.
7. Add SD event logging.
8. Add CC1101/nRF24 RF-activity screens.
9. Add NFC/IR tools.
