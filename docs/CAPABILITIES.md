# RFScout capabilities

## Core goal

RFScout is a **situational-awareness** instrument: provide a useful, glanceable picture of nearby electronic devices and RF activity without requiring the user to inspect raw packets continuously.

## Implemented applications

### WiFi / BT

Implemented capabilities include:

- passive Wi-Fi and BLE observation;
- rolling recent-device state;
- IEEE OUI/vendor attribution for globally administered addresses;
- explicit `UNKNOWN` handling for private/randomized or unresolved identities;
- ranked vendor counts;
- surveillance-oriented classification using narrow, explainable evidence;
- visual detection highlighting;
- automatic stop of Wi-Fi/BLE collectors when another application is selected.

The WiFi / BT application is the only current view that treats IEEE MAC/OUI identity as a normal device identity source.

### CC1101

The CC1101 application currently provides:

- direct RSSI spectrum sweeps across supported sub-GHz tuning ranges;
- slow-decay peak-hold markers;
- manual LOW/HIGH/STEP controls;
- 1, 5, 10, 25, 50, 100, 200, 250, 500, and 1000 kHz step options;
- a hard 128-sample maximum per sweep;
- a 433 MHz default of 432.800-433.200 MHz at 5 kHz spacing;
- fixed likely-device category counts using conservative RF-shape inference;
- `RESET COUNTS` with immediate return to the live analyzer;
- runtime-state reset when the application is exited.

The likely-device labels are behavior categories, not decoded product identities. CC1101 activity does not carry an IEEE identity equivalent to Wi-Fi/BLE OUI information.

### nRF24

The nRF24 application currently provides:

- configurable low/high channel bounds;
- configurable dwell time;
- fast-decay channel activity histogram;
- slow-decay peak-hold markers;
- passive Enhanced ShockBurst metadata classification when validated observations are available;
- fixed likely-device count categories;
- per-address count deduplication independent of the small active classifier cache;
- conservative labels such as `Logitech?`, `Input?`, and `Sensor?`;
- `RESET COUNTS` with immediate return to the live analyzer;
- runtime histogram/classifier/count reset when the application is exited.

Spectrum-only patterns that cannot support a device-level identity are grouped under `Other 2.4 RF` rather than presented as a specific product.

### Thermal

An external **MLX90640** connected over the board's I2C/Qwiic interface is supported as an optional application.

- RFScout periodically probes for the sensor;
- Thermal appears last in the application order only when the sensor is detected;
- the native 32x24 thermal frame is scaled/interpolated for the LCD;
- thermal acquisition is not kept running while another app is active.

## Application navigation and UX

Current app order:

1. WiFi / BT
2. CC1101
3. nRF24
4. Thermal, when available

Encoder conventions are intentionally shared:

- rotate on the main view to change applications;
- center press opens application settings where provided;
- rotate in a menu to select;
- center press enters/exits editable values;
- action entries such as `RESET COUNTS` execute immediately;
- `RESET COUNTS` and `BACK` both close the menu after the action;
- yellow/amber denotes active editing or a configuration limit;
- red denotes detections/counts or error states;
- normal application labels/status information use subdued colors.

CC1101 and nRF24 use the same spectrum/right-panel divider and the same fixed-row Likely Devices presentation.

## Surveillance awareness

The WiFi / BT path supports targeted surveillance classifications. The design rule remains conservative:

- do not label a generic device as surveillance merely because its manufacturer also sells surveillance equipment;
- combine available OUI, advertised-name, manufacturer, service, and protocol evidence where possible;
- preserve confidence/evidence internally where classifications are generated;
- retain noteworthy detections longer than the ordinary nearby-device window.

## Alerts and status

The UI includes a shared top status region between the application label and battery indicator. Status icons populate from right to left and use subdued normal-state colors.

Implemented status behavior includes SD-card presence and sticky logging/filesystem errors. Battery state is shown continuously when telemetry is available.

The project also contains alert-oriented support for noteworthy surveillance detections. Alert behavior should remain asynchronous so radio processing is never blocked by UI/audio work.

## microSD logging

Implemented storage behavior includes:

- optional microSD mount at runtime;
- nonfatal operation when media is missing or cannot be mounted;
- asynchronous logging rather than filesystem I/O in radio callbacks;
- two-digit circular filenames (`00` through `99`);
- 4 MiB maximum per segment;
- approximate 400 MiB bounded normal log footprint;
- sticky SD error indication in the status bar.

LILYGO's documented tested baseline is a SanDisk card of 32 GB or less. See [SD_CARD.md](SD_CARD.md).

## Battery / power telemetry

Implemented baseline behavior includes:

- BQ27220 state-of-charge, voltage, and signed current;
- BQ25896 external-power/charge-state detection;
- cached periodic telemetry;
- exact percentage plus proportional battery fill;
- charging indication;
- subdued normal colors with stronger low-charge warnings;
- nonfatal unavailable state;
- shared `board_i2c` ownership rather than duplicated bus initialization.

## OUI database

RFScout maintains a compiled IEEE MA-L/OUI table for Wi-Fi/BLE attribution. Generated data can be refreshed with `tools/update_oui.py` or the repository workflow.

## Not currently active applications

The T-Embed hardware exposes additional capabilities, but the following should be treated as future/inactive until explicitly implemented and enabled in the app manager:

- NFC/PN532 tools;
- IR analyzer/transmit tools;
- microphone/audio-analysis applications;
- GPS support;
- active RF transmission/replay functions.

## Future ideas

Useful next steps include:

- stronger protocol-level nRF24 fingerprints;
- time-windowed rather than session-only radio category counts;
- CC1101 burst timing/modulation analysis;
- per-category drill-down views;
- configurable classifier thresholds;
- structured SD exports/captures;
- additional optional hardware applications that follow the same resource-lifecycle and UI conventions.
