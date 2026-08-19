# RFScout

RFScout is a handheld **RF situational-awareness** project for the LILYGO T-Embed CC1101 Plus.

The project combines the useful ideas from Brandalize and OUI-SPY into one passive nearby-device view:

- identify nearby devices by OUI/vendor when a globally administered MAC permits it;
- maintain a rolling view of recently observed unique devices;
- rank nearby brands;
- identify narrow, high-confidence surveillance-device signatures;
- provide visual, LED, and Morse-code audio alerts for new surveillance detections;
- expand the same UI to other radios and sensors available on the T-Embed CC1101 Plus.

RFScout is intended as an awareness instrument, not an active network client. Wi-Fi/BLE collection should remain passive wherever the hardware/API permits it.

## Planned primary screens

1. **Nearby** — unique devices, brands, RSSI/channel/source and recent activity.
2. **Surveillance** — high-confidence OUI-SPY-style detections and alert history.
3. **RF Activity** — Wi-Fi/BLE plus future CC1101 and nRF24 activity summaries.
4. **Tools** — NFC, IR, logging, settings, mute/alert controls and device information.

The rotary encoder is the primary navigation control. The onboard speaker is intended for Morse-code surveillance alerts, and the RGB LEDs can provide glanceable status/alert state.

## Repository status

This repository currently contains the baseline architecture and data model. Hardware-specific T-Embed drivers and the mature Brandalize/OUI-SPY classifiers will be brought in behind clean interfaces so RFScout's core does not depend directly on one display or radio implementation.

See [docs/CAPABILITIES.md](docs/CAPABILITIES.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build

RFScout targets the ESP32-S3 used by the T-Embed CC1101 Plus:

```sh
idf.py set-target esp32s3
idf.py build
```

The baseline hardware layer is intentionally incomplete until the exact board revision/pin map is verified against the delivered unit and LILYGO documentation.

## License

MIT
