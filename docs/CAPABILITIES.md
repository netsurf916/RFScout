# RFScout capabilities

## Core goal

RFScout is a **situational-awareness** instrument: provide a useful, glanceable picture of nearby electronic devices, RF activity, and selected sensor data without requiring the user to inspect raw packets continuously.

## Implemented applications

### WiFi / BT

Implemented capabilities include:

- passive Wi-Fi and BLE observation;
- rolling recent-device state;
- IEEE OUI/vendor attribution for globally administered addresses;
- explicit `UNKNOWN` handling for private/randomized or unresolved identities;
- ranked vendor counts across the full active observation set;
- surveillance-oriented classification using narrow, explainable evidence;
- visual detection highlighting;
- configurable surveillance audible alerts: Off, Tone, or single-character Morse;
- automatic stop of Wi-Fi/BLE collectors when another application is selected.

Audible alerts default to Off on every boot. Tone and Morse playback run asynchronously on the onboard speaker so radio callbacks never wait for audio output. Morse alerts are exactly one selectable character.

The WiFi / BT application is the only current view that treats IEEE MAC/OUI identity as a normal device identity source.

### CC1101

The CC1101 application currently provides:

- direct RSSI spectrum sweeps across supported sub-GHz tuning ranges;
- optional slow-decay peak-hold markers;
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
- optional slow-decay peak-hold markers;
- passive Enhanced ShockBurst metadata classification when validated observations are available;
- fixed likely-device count categories;
- per-address count deduplication independent of the small active classifier cache;
- conservative labels such as `Logitech?`, `Input?`, and `Sensor?`;
- `RESET COUNTS` with immediate return to the live analyzer;
- runtime histogram/classifier/count reset when the application is exited.

Spectrum-only patterns that cannot support a device-level identity are grouped under `Other 2.4 RF` rather than presented as a specific product.

### Audio

The Audio application uses the T-Embed CC1101 Plus internal PDM microphone through ESP-IDF's I2S PDM receive path.

Implemented behavior includes:

- DMA-fed microphone acquisition;
- 48 kHz native PCM conversion;
- Hann-windowed FFT processing;
- automatically scaled spectrum display;
- focused Voice, Mid, High, Audible, and Ultrasonic ranges;
- filtered 2x decimation for Voice and Mid;
- selectable LEFT/RIGHT PDM receive slot for hardware validation;
- optional peak hold, disabled by default for a more responsive live trace;
- overrun accounting when a newer FFT frame replaces an unpublished frame.

The Audio application is currently an exploratory spectrum analyzer, not a calibrated sound-level instrument. The next planned Audio expansion is microphone recording plus an interactive International Morse player built on the shared speaker/Morse layer.

### Thermal

An external **MLX90640** connected over the board's I2C/Qwiic interface is supported as an optional application.

- RFScout periodically probes for the sensor;
- Thermal appears near the end of the application order only when the sensor is detected and enabled;
- the native 32x24 thermal frame is scaled/interpolated for the LCD;
- thermal acquisition is not kept running while another app is active.

### Device

The Device application provides global runtime and SD-backed configuration:

- display brightness;
- 12/24-hour clock presentation;
- editable timezone selection;
- manual wall-clock setting;
- per-application enablement;
- SD logging enablement;
- explicit configuration save;
- configuration/time-source status.

Device itself is always available so configuration cannot disable its own recovery path.

## Application navigation and UX

Current app order:

1. WiFi / BT
2. CC1101
3. nRF24
4. Audio
5. Thermal, when enabled and available
6. Device

Encoder conventions are intentionally shared:

- rotate on the main view to change applications;
- center press opens application settings where provided;
- rotate in a menu to select;
- center press enters/exits editable values;
- action entries such as `RESET COUNTS` execute immediately;
- `RESET COUNTS` and `BACK` close their menus after the action;
- Device uses an explicit Back action to resume top-level navigation;
- yellow/amber denotes active editing or a configuration limit;
- red denotes detections/counts or error states;
- normal application labels/status information use subdued colors.

CC1101 and nRF24 use the same spectrum/right-panel divider and the same fixed-row Likely Devices presentation. Applications that expose peak hold use a common ON/OFF menu concept while retaining app-specific defaults.

## Surveillance awareness

The WiFi / BT path supports targeted surveillance classifications. The design rule remains conservative:

- do not label a generic device as surveillance merely because its manufacturer also sells surveillance equipment;
- combine available OUI, advertised-name, manufacturer, service, and protocol evidence where possible;
- preserve confidence/evidence internally where classifications are generated;
- retain noteworthy detections longer than the ordinary nearby-device window.

## Alerts and status

The UI includes a shared top status region between the application label and battery indicator. Status icons populate from right to left and use subdued normal-state colors. The shared status and battery chrome is applied by the common presentation layer so newly added views inherit it automatically.

Implemented status behavior includes SD-card presence and sticky logging/filesystem errors. Battery state is shown continuously when telemetry is available.

WiFi / BT surveillance detections can optionally produce an audible alert. Alert mode is **Off**, **Tone**, or **Morse**; Off is the boot default. Morse is limited to one selected character. The printable-character table follows ITU-R M.1677-1 for A-Z, figures, and the standard ASCII-representable punctuation signs.

The onboard-speaker service uses a bounded asynchronous queue. Audio generation therefore remains outside radio callbacks and cannot deliberately stall acquisition while a sound finishes.

## microSD logging

Implemented storage behavior includes:

- optional microSD mount at runtime;
- nonfatal operation when media is missing or cannot be mounted;
- asynchronous logging rather than filesystem I/O in radio or audio acquisition callbacks;
- detailed Wi-Fi/BLE lifecycle records;
- application-owned ENTER/EXIT and rate-limited telemetry records for WiFi/BT, CC1101, nRF24, Audio, Thermal, and Device;
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

## Hardware audio resources

The internal PDM microphone uses I2S0 receive. The onboard speaker uses the board's dedicated BCLK/LRCLK/data pins through I2S1 standard transmit. The speaker path is managed by `audio_output.c/.h` and is independent of the Audio analyzer's microphone worker.

The shared Morse table is used by audible alerts and is intended to be reused by the planned Audio Morse browser/player rather than duplicating code tables.

## OUI database

RFScout maintains a compiled IEEE MA-L/OUI table for Wi-Fi/BLE attribution. Generated data can be refreshed with `tools/update_oui.py` or the repository workflow.

## Not currently active applications

The T-Embed hardware exposes additional capabilities, but the following should be treated as future/inactive until explicitly implemented and enabled in the app manager:

- NFC/PN532 tools;
- IR analyzer/transmit tools;
- GPS user interface;
- active RF transmission/replay functions.

## Future ideas

Useful next steps include:

- Audio microphone recording and an interactive International Morse pattern player;
- stronger protocol-level nRF24 fingerprints;
- time-windowed rather than session-only radio category counts;
- CC1101 burst timing/modulation analysis;
- audio FFT/PDM validation followed by tuner and DTMF features;
- per-category drill-down views;
- configurable classifier thresholds;
- structured SD exports/captures;
- additional optional hardware applications that follow the same resource-lifecycle and UI conventions.
