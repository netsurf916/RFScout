# RFScout capabilities

## Core goal

RFScout is a **situational-awareness** instrument: provide a useful, glanceable picture of nearby electronic devices, RF activity, and selected sensor data without requiring the user to inspect raw packets continuously.

## Implemented applications

### WiFi / BT

Implemented capabilities include:

- passive Wi-Fi and BLE observation;
- rolling recent-device state;
- IEEE vendor attribution using longest-prefix matching across MA-L (24-bit), MA-M (28-bit), MA-S/OUI-36 (36-bit), and legacy IAB assignments;
- explicit `RANDOM` handling for Wi-Fi locally administered and BLE random/private address types;
- explicit `UNKNOWN` handling for globally administered/public addresses with no matching IEEE assignment;
- ranked vendor counts across the full active observation set;
- surveillance-oriented classification using narrow, explainable evidence;
- curated surveillance-prefix metadata resolved through the same identity lookup layer as IEEE vendor data;
- visual detection highlighting;
- global Off/Tone/Morse notifications for new surveillance detections;
- a `RESET DETECTIONS` action that clears the live device/OUI census and retained surveillance history;
- automatic stop of Wi-Fi/BLE collectors when another application is selected.

The WiFi / BT application is the only current view that treats IEEE MAC identity as a normal device identity source. IEEE attribution is separate from curated surveillance significance: the common prefix lookup can return a vendor, optional surveillance metadata, or both. Higher-layer BLE/protocol evidence remains part of the surveillance classifier. Notification policy is device-wide and is configured in Device rather than in the WiFi/BT action menu.

Its notification items are Flock/ALPR, body camera, drone, camera, smart glasses, acoustic sensor, and other surveillance. Each item has exactly one stable notification descriptor and one configurable Morse character.

### CC1101

The CC1101 application currently provides:

- direct RSSI spectrum sweeps across supported sub-GHz tuning ranges;
- optional slow-decay peak-hold markers;
- manual LOW/HIGH/STEP controls;
- 1, 5, 10, 25, 50, 100, 200, 250, 500, and 1000 kHz step options;
- a hard 128-sample maximum per sweep;
- a 433 MHz default of 432.800-433.200 MHz at 5 kHz spacing;
- fixed **DETECTIONS** category counts using conservative RF-shape inference;
- a global audible notification when a detection category count advances;
- `RESET COUNTS` with immediate return to the live analyzer;
- runtime-state reset when the application is exited.

The detection labels are behavior categories, not decoded product identities. CC1101 activity does not carry an IEEE identity equivalent to Wi-Fi/BLE OUI information. Its notification items are narrowband, remote/sensor, carrier, bursty RF, and wide RF.

### nRF24

The nRF24 application currently provides:

- configurable low/high channel bounds;
- configurable dwell time;
- fast-decay channel activity histogram;
- optional slow-decay peak-hold markers;
- passive Enhanced ShockBurst metadata classification when validated observations are available;
- fixed **DETECTIONS** count categories;
- per-address count deduplication independent of the small active classifier cache;
- conservative labels such as `Logitech?`, `Input?`, and `Sensor?`;
- global audible notifications when underlying detection-category counts advance;
- `RESET COUNTS` with immediate return to the live analyzer;
- runtime histogram/classifier/count reset when the application is exited.

Spectrum-only patterns that cannot support a device-level identity are grouped under `Other 2.4 RF` in the display. Underlying Wi-Fi-like and BLE/hopping-RF-like classifier categories remain distinct for notification-character configuration. Its notification items are ESB device, input, sensor, WiFi-like, hopping/BLE-like, and Logitech-like.

### Recorder

The Recorder application uses the T-Embed CC1101 Plus internal PDM microphone through ESP-IDF's I2S0 PDM receive path and requires mounted SD storage for capture/playback.

Implemented behavior includes:

- 16 kHz, 16-bit mono PCM WAV recording under `/sdcard/audio/`;
- asynchronous microphone capture outside the UI loop;
- WAV-header finalization on stop and application exit;
- automatic `/sdcard/audio/` creation when storage is available;
- explicit `NO SD CARD` behavior when storage is unavailable;
- enumeration of only supported, correctly formatted PCM WAV recordings;
- duration and size metadata for validated recordings;
- asynchronous playback over the onboard I2S1 speaker;
- use of the configured global speaker volume for WAV playback;
- clean stop of capture/playback when leaving the application.

The former FFT/windowing/decimation/spectrum-analyzer implementation is removed. The historical internal Audio application identifier/interface remains only as a compatibility boundary until a later enum/naming cleanup.

### Thermal

An external **MLX90640** connected over the board's I2C/Qwiic interface is supported as an optional application.

- RFScout periodically probes for the sensor;
- Thermal appears near the end of the application order only when the sensor is detected and enabled;
- the native 32x24 thermal frame is scaled/interpolated for the LCD;
- thermal acquisition is not kept running while another app is active.

Thermal currently displays sensor data rather than discrete detection events. Future Thermal detectors can use the same global notification interface.

### Device

The Device application provides global runtime and SD-backed configuration:

- a pass-through landing screen that does not trap encoder navigation;
- display brightness;
- 12/24-hour clock presentation;
- editable timezone selection;
- manual wall-clock setting;
- per-application enablement;
- SD logging enablement;
- global notification mode: `OFF`, `TONE`, or `MORSE`;
- global notification volume from `0` through `100%`, adjusted in 10% steps;
- one independently editable Morse character for every defined detection category;
- explicit configuration save;
- configuration/time-source status.

Device itself is always available so configuration cannot disable its own recovery path. Notification mode, volume, and per-category characters are stored in `/sdcard/rfscout.json` and restored on a later boot. The compiled fallbacks are `OFF`, **50% volume**, and the compiled category characters when no usable saved values exist.

## Application settings inventory

The settings and alert inventories are explicit so additions do not silently create mismatched UI/configuration lists.

| Application | Settings/actions | Notification items |
| --- | --- | --- |
| WiFi / BT | Back; Reset detections | Flock/ALPR; Body camera; Drone; Camera; Smart glasses; Acoustic sensor; Other surveillance |
| CC1101 | Back; Profile; Low; High; Step; Peak hold; Reset counts | Narrowband; Remote/sensor; Carrier; Bursty RF; Wide RF |
| nRF24 | Back; Low channel; High channel; Dwell; Peak hold; Reset counts | ESB device; Input; Sensor; WiFi-like; Hopping/BLE-like; Logitech-like |
| Recorder | Back; Record; File; Play | None currently |
| Thermal | No settings menu currently | None currently |
| Device | Back; Brightness; Clock format; Timezone; Manual date/time; app enablement; SD logging; notification mode; notification volume; every notification character; Save config | Configures every registered notification item but emits none itself |

The alert enum and the canonical descriptor table in `alert.c` are intentionally 1:1. Each descriptor records its owning application, stable JSON key, user-facing label, and default character. Device and SD persistence iterate that registry rather than maintaining duplicate alert-item lists.

When classifier categories change, the remaining producer mappings must change with them: WiFi/BT surveillance mapping is in `rfscout.c`; CC1101 and nRF24 mappings are in `app_manager.c`.

## Application navigation and UX

Current app order:

1. WiFi / BT
2. CC1101
3. nRF24
4. Recorder
5. Thermal, when enabled and available
6. Device

Encoder conventions are intentionally shared:

- rotate on a main/pass-through view to change applications;
- center press opens application settings where provided;
- every settings/action menu opens with **Back** as the first and initially selected entry;
- opening a menu and pressing center again immediately returns without changing a setting;
- rotate in a menu to select;
- center press enters/exits editable values;
- action entries such as `RESET COUNTS` or `RESET DETECTIONS` execute immediately;
- Device requires a center press to enter global settings; otherwise encoder rotation continues to the next app;
- yellow/amber denotes active editing or a configuration limit;
- red denotes detections/counts or error states;
- normal application labels/status information use subdued colors.

CC1101 and nRF24 use the same spectrum/right-panel divider and the same fixed-row **DETECTIONS** presentation. Applications that expose peak hold use a common ON/OFF menu concept while retaining app-specific defaults.

## Surveillance awareness

The WiFi / BT path supports targeted surveillance classifications. The design rule remains conservative:

- do not label a generic device as surveillance merely because its manufacturer also sells surveillance equipment;
- treat curated prefix metadata as one evidence source rather than as the whole classifier;
- combine available prefix, advertised-name, manufacturer, service, and protocol evidence where possible;
- preserve confidence/evidence internally where classifications are generated;
- retain noteworthy detections longer than the ordinary nearby-device window.

Curated surveillance prefixes remain in `data/surveillance_signatures.csv`, but their generated table is consumed by the common prefix identity layer. This allows vendor attribution and surveillance significance to share one lookup operation while keeping the curated interpretation separate from authoritative IEEE ownership data. A field-verified locally administered prefix can therefore contribute surveillance evidence without being misrepresented as an IEEE OUI assignment.

## Notifications and status

The UI includes a shared top status region between the application label and battery indicator. Status icons populate from right to left and use subdued normal-state colors. The shared status and battery chrome is applied by the common presentation layer so newly added views inherit it automatically.

Implemented status behavior includes SD-card presence and sticky logging/filesystem errors. Battery state is shown continuously when telemetry is available.

Audible detection notifications use a device-wide **Off**, **Tone**, or **Morse** mode. `OFF` is the compiled fallback. A valid mode loaded from the SD configuration is restored on boot instead of being forced back to Off. The compiled notification-volume fallback is **50%**; a valid saved `0-100` value overrides it. Volume is applied to both Tone and Morse output.

Each queued speaker command captures the current notification volume when it is requested. Changing volume therefore affects later commands without changing the level of commands already waiting in the asynchronous queue.

Morse notifications contain exactly one character selected by detection category; the default mapping includes Flock `F`, acoustic `A`, camera `C`, drone `D`, and smart glasses `G`, and every mapping can be edited in Device and persisted.

The printable-character table follows ITU-R M.1677-1 for A-Z, figures, and the standard ASCII-representable punctuation signs. The onboard-speaker service uses a bounded asynchronous queue, so audio generation remains outside radio callbacks and cannot deliberately stall acquisition while a sound finishes.

## microSD logging

Implemented storage behavior includes:

- optional microSD mount at runtime;
- nonfatal operation when media is missing or cannot be mounted;
- asynchronous logging rather than filesystem I/O in radio or audio acquisition callbacks;
- detailed Wi-Fi/BLE lifecycle records;
- application-owned ENTER/EXIT and rate-limited telemetry records for WiFi/BT, CC1101, nRF24, Recorder, Thermal, and Device;
- two-digit circular filenames (`00` through `99`);
- 4 MiB maximum per segment;
- approximate 400 MiB bounded normal log footprint;
- sticky SD error indication in the status bar.

LILYGO's documented tested baseline is a SanDisk card of 32 GB or less. See [SD_CARD.md](SD_CARD.md).

## Battery / power telemetry

Implemented baseline behavior includes BQ27220 state-of-charge/voltage/current, BQ25896 external-power/charge-state detection, cached periodic telemetry, proportional battery presentation, charging indication, and nonfatal unavailable state through the shared board-I2C layer.

## Hardware audio resources

The internal PDM microphone uses I2S0 receive while Recorder is actively capturing. The onboard speaker uses the board's dedicated BCLK/LRCLK/data pins through I2S1 standard transmit. Recorder playback is streamed through the same speaker service used by alerts, while capture and playback retain separate I2S peripheral ownership.

The shared Morse table is used by detection notifications and is intended to be reused by the planned separate CW application rather than duplicating code tables.

## OUI / prefix identity database

RFScout maintains compiled IEEE MA-L, MA-M, MA-S/OUI-36, and legacy IAB assignment tables for Wi-Fi/BLE vendor attribution. Longest-prefix matching prefers 36-bit assignments, then 28-bit, then 24-bit. CID is not used for vendor attribution because it is not a globally unique MAC-address allocation.

`tools/update_oui.py` generates the IEEE tables, while `tools/update_surveillance.py` generates curated surveillance-prefix metadata consumed by the same runtime lookup layer. See [PREFIX_IDENTITY.md](PREFIX_IDENTITY.md) for registry sources, matching precedence, maintenance rules, and classifier boundaries.

## Not currently active applications

The T-Embed hardware exposes additional capabilities, but the following should be treated as future/inactive until explicitly implemented and enabled in the app manager:

- NFC/PN532 tools;
- IR analyzer/transmit tools;
- GPS user interface;
- active RF transmission/replay functions.

## Future ideas

Useful next steps include Audio microphone recording and the Morse player, stronger protocol-level nRF24 fingerprints, time-windowed radio detection counts, CC1101 burst timing/modulation analysis, audio FFT/PDM validation followed by tuner/DTMF features, per-category drill-down views, configurable classifier thresholds, structured SD exports/captures, and additional optional hardware applications that follow the same lifecycle/UI/notification conventions.
