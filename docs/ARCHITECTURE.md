# Architecture

RFScout is organized around a small **application manager** plus capability-specific radio/sensor modules. The current top-level application order is:

```text
WiFi / BT -> CC1101 -> nRF24 -> Recorder -> Thermal (when enabled and present) -> Device
```

The rotary encoder changes applications unless an application menu is open. The center button enters/leaves application settings. All settings/action menus open with **Back** selected first. Device is a pass-through landing screen: merely arriving at Device does not take encoder ownership, and a center press is required to enter global settings. Optional applications are capability-driven: Thermal is skipped when the MLX90640 is unavailable or disabled.

## Resource lifecycle

A core design rule is that inactive applications should not continue doing expensive work.

- Leaving **WiFi / BT** stops the Wi-Fi monitor and BLE scanner.
- Leaving **CC1101** stops its scanner and clears runtime RSSI/peak/classification state.
- Leaving **nRF24** stops its scanner and clears runtime histogram/peak/classification state.
- Leaving **Recorder** stops active microphone capture or WAV playback and releases Recorder-owned resources.
- Thermal acquisition is performed only while the Thermal application is active.
- Device owns the encoder only after its settings view is deliberately entered; Back returns to the pass-through Device landing screen.

Application-specific analyzer configuration such as CC1101 frequency range/step and nRF24 channel/dwell settings remains runtime state independently of the acquisition workers unless explicitly stored in Device configuration. Audible-notification mode, volume, and per-detection Morse characters are device-wide configuration. Recorder playback volume is a separate application preference. Both are persisted in `/sdcard/rfscout.json`.

## Wi-Fi / BLE data path

```text
Wi-Fi monitor ---\
                  +--> normalized observation --> identity/OUI --> surveillance classifier
BLE monitor -----/              |                       |
                                v                       v
                         rolling device store      alert/history state
                                |                       |
                                v                       +--> global notification policy
                            WiFi / BT UI                         |
                                                               v
                                                        audio-output queue
```

The live store is fixed-memory and rolling. OUI/vendor attribution is applied only where the address can defensibly represent an IEEE identity. Random/private addresses remain unknown rather than being assigned a misleading vendor.

Surveillance detections use separate retained state so short-lived observations can remain visible longer than the ordinary nearby-device window. The WiFi/BT action menu exposes `RESET DETECTIONS`, which clears both the live observation/OUI census and retained surveillance history without restarting the collectors.

Audible notification policy is global rather than WiFi/BT-owned. A new surveillance observation is mapped to a stable notification category and submitted to the shared alert service. In Morse mode the category's configured one-character mapping is used; Tone mode uses the common tone. The observation callback never generates audio synchronously.

## CC1101 data path

```text
CC1101 scanner -> completed RSSI sweep -> direct display levels
                                      \-> optional slow peak hold
                                      \-> conservative RF-shape classifier
                                              |
                                              v
                                      detection count panel
                                              |
                                      count increment
                                              |
                                              v
                                      global notification
```

The visible CC1101 spectrum uses the most recently completed direct RSSI sweep; it is not an accumulated histogram. Historical information is limited to optional slow-decay peak markers and the detection category counts.

Sweep configuration is bounded to 128 samples. LOW/HIGH/STEP adjustments that would exceed the limit are rejected instead of changing another setting automatically. This provides a predictable upper bound on sweep work and display latency.

A newly confirmed classifier result increments its detection category. When a category count advances, the application manager requests that category's configured global notification. Resetting counts also resets the notification baseline so later detections can notify again.

## nRF24 data path

```text
nRF24 RPD sweep -> fast activity histogram -> optional slow peak hold
                                      |
                                      +-> spectrum-shape inference

validated ESB observation -> active packet classifier cache
                                      |
                                      +-> persistent address ledger
                                      |
                                      v
                               detection count panel
                                      |
                               count increment
                                      |
                                      v
                               global notification
```

The nRF24 activity display intentionally differs from CC1101 because nRF24 RPD provides thresholded channel activity rather than calibrated RSSI. Fast activity history therefore gives a more useful visual representation than a raw single sweep.

Validated Enhanced ShockBurst metadata is classified conservatively using address, bitrate, payload size, repetition timing, and known high-confidence signatures. The small active classifier cache is separate from a larger address ledger used for count deduplication. This prevents an identity from being counted again merely because its detailed classifier record was evicted.

Category names with question marks (`Logitech?`, `Input?`, `Sensor?`) communicate that these are inferred identities/types unless the evidence is sufficiently specific. The UI may combine broad Wi-Fi-like and BLE/hopping-RF-like spectrum detections into `Other 2.4 RF`, while notification state retains the underlying category so each can have an independent Morse mapping.

## Recorder data path

```text
PDM microphone -> ESP32-S3 I2S0/PDM RX -> DMA buffers -> Recorder worker
                                                        |
                                                        v
                                               PCM WAV on /sdcard/audio/
                                                        |
                                                        v
                                             validated recording list
                                                        |
                                                        v
                                             I2S1 speaker playback
```

`recorder.c/.h` owns microphone capture, WAV creation/finalization, supported-file validation, and the in-memory recording list. Recorder uses 32 kHz, 16-bit mono PCM so captured files can be streamed directly through the existing I2S1 speaker service without a second resampling path.

Storage is a required runtime dependency for Recorder rather than for RFScout as a whole. If the SD card is unavailable, Recorder remains navigable but exposes a `NO SD CARD` state and disables record/play actions. Leaving Recorder stops active capture or playback and finalizes an open WAV header before releasing resources.

The historical `audio_spectrum.c/.h` interface is temporarily retained as a compatibility adapter for the existing application-manager enum and navigation contract; it no longer performs FFT, windowing, decimation, or spectrum analysis.

## Speaker and Morse output

The onboard speaker is isolated from microphone acquisition:

```text
detection category -> configured character + mode + volume -> alert_request_character()
                                                           |
                                                           v
                                         bounded FreeRTOS queue -> audio_output worker
```

`audio_output.c/.h` owns I2S1 transmit and waveform generation. Callers enqueue short sound requests with zero wait; a full queue drops the new sound instead of stalling an acquisition callback. I2S is enabled only while a queued sound is being rendered and disabled again when idle.

Notification volume is captured in the queued command. The output worker scales the previous fixed PCM amplitude linearly by the queued `0-100%` value. That means a later Device volume change affects future notifications without changing commands already waiting in the queue.

`alert.c/.h` owns device-wide Off/Tone/Morse policy, volume state, and the stable notification-category enumeration. A single descriptor table has a 1:1 correspondence with the notification enum and owns each item's application, stable JSON key, label, and default character. `device_config.c/.h` iterates those descriptors when loading and saving mappings rather than maintaining a second key list.

When alert categories change, the enum and descriptor table must change together. The WiFi/BT classifier mapping in `rfscout.c` and the CC1101/nRF24 mappings in `app_manager.c` are the producer-side locations that must also be reviewed. Device rows and JSON character iteration are descriptor-driven.

Compiled notification defaults include mode `OFF`, volume **50%**, and distinct characters such as Flock `F`, acoustic sensor `A`, camera `C`, drone `D`, and smart glasses `G`; a valid SD configuration overrides those defaults on boot.

`morse.c/.h` owns the shared Morse character table. Printable characters represented by ITU-R M.1677-1 are centralized there so notification mappings and the planned Audio Morse browser/player use the same patterns and timing semantics.

## Detections UX

CC1101 and nRF24 share the same right-side **DETECTIONS** convention:

- fixed category rows remain visible even when zero;
- zero counts are subdued;
- nonzero counts are red;
- counts persist while the application session is active;
- `RESET COUNTS` clears the tally and immediately closes the menu so changes can be observed without an extra navigation step;
- leaving the application starts a fresh runtime observation session when it is entered again.

The two apps use the same vertical divider position so switching between them does not visually rearrange the primary content regions.

## Thermal path

The MLX90640 is an optional external I2C/Qwiic device. RFScout periodically probes for it while another application is active. When detected and enabled, the Thermal application becomes available near the end of the rotation before Device.

The sensor's native 32x24 frame is processed for display interpolation/scaling, but the renderer does not imply higher underlying sensor resolution. Thermal currently has no discrete detection classifier; future Thermal detection events can use the same global notification interface.

## Device configuration and time

`device_config.c/.h` owns compiled defaults plus optional SD-backed `/sdcard/rfscout.json` preferences. Configuration loading is nonfatal: RFScout can continue with runtime defaults when storage or persistence fails. Global notification mode, volume, and every notification-category Morse character are part of this configuration.

Device has two navigation states: a pass-through landing screen and the entered settings editor. Rotation on the landing screen remains top-level app navigation; center enters settings; settings opens on Back; Back returns to the landing screen.

`device_time.c/.h` owns authoritative system wall-clock state and source/provenance metadata. User-visible time and timestamps use the wall clock, while debounce, dwell, retries, timeouts, and housekeeping remain on monotonic timers. Deep-sleep RTC metadata and cold-boot saved-time provenance are treated separately.

## Shared hardware boundaries

### SPI

The LCD, microSD, CC1101, and nRF24 share board SPI resources. `board_spi.c/.h` owns initialization of SPI2 and exposes an idempotent board-level initializer used by both storage and display setup. Fixed pins and chip selects remain centralized in `board.h`. This avoids relying on accidental peripheral initialization order.

### I2C

`board_i2c.c/.h` owns initialization of the shared board I2C controller. Battery/power modules and optional external I2C devices use that shared controller instead of independently configuring SDA/SCL.

`battery.c/.h` owns BQ27220/BQ25896 register access and exposes cached telemetry to the UI. Missing power-management telemetry is nonfatal.

### I2S / PDM

`recorder.c/.h` owns ESP32-S3 I2S0 PDM receive for the internal microphone while recording. Capture uses DMA and a dedicated Recorder worker, and the receive channel is released whenever Recorder is not actively recording.

`audio_output.c/.h` independently owns I2S1 standard transmit for the onboard speaker. Short alert sounds therefore do not reconfigure or borrow the microphone's I2S0 channel.

## Storage and logging boundary

`storage.c/.h` owns SD mount/state. `log_writer.c/.h` owns the bounded asynchronous record queue, filesystem writes, segment rotation, and shutdown semantics. Radio, audio, and UI paths never perform direct SD writes.

`app_logging.c/.h` is the application-level coordinator. It observes application transitions and submits ENTER/EXIT plus rate-limited telemetry records for active applications. Wi-Fi/BLE detailed device lifecycle records continue to use the same writer queue.

Filesystem errors are surfaced through the shared top status bar rather than terminating RFScout.

## UI boundary

The display driver is kept below application renderers. `ui_present.c` applies shared status/battery chrome and the global power overlay at presentation time so individual views do not need to duplicate those concerns.

Application views use common visual conventions:

- shared battery, status, and wall-clock chrome;
- subdued app titles;
- subdued normal/status colors;
- red for actionable detections/errors;
- yellow/amber for editing or limits;
- consistent encoder semantics;
- Back as the first and initially selected menu action;
- fixed right-side category panels where applicable;
- optional peak hold controlled through application settings where supported.

## Source/module boundary

Hand-maintained modules follow the conventions in [CODING_STYLE.md](CODING_STYLE.md): public declarations live in headers, public APIs are documented with Doxygen, private helpers/state remain `static`, hardware pin definitions remain centralized, and source is intentionally formatted for readability rather than compactness. Generated `.inc` data tables are maintained through their generators instead of hand editing.

## Current versus future capabilities

Implemented application-level capabilities are Wi-Fi/BLE awareness, device-wide configurable asynchronous detection notifications with persistent volume and per-category Morse mappings, CC1101 spectrum/detection awareness, nRF24 activity/fingerprinting, SD-backed PCM WAV recording/playback, optional MLX90640 thermal imaging, SD logging, battery/status telemetry, Device configuration/time management, and encoder-driven navigation.

The separate interactive CW/Morse sender is planned next. NFC, IR, GPS UI, and active/replay functions are not part of the current active application set and should not be presented as implemented functionality until they are actually enabled.
