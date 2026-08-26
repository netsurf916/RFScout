# Architecture

RFScout is organized around a small **application manager** plus capability-specific radio/sensor modules. The current top-level application order is:

```text
WiFi / BT -> CC1101 -> nRF24 -> Audio -> Thermal (when enabled and present) -> Device
```

The rotary encoder changes applications unless an application menu is open. The center button enters/leaves application settings. Optional applications are capability-driven: Thermal is skipped when the MLX90640 is unavailable or disabled. Device is always reachable and provides the global configuration UI.

## Resource lifecycle

A core design rule is that inactive applications should not continue doing expensive work.

- Leaving **WiFi / BT** stops the Wi-Fi monitor and BLE scanner.
- Leaving **CC1101** stops its scanner and clears runtime RSSI/peak/classification state.
- Leaving **nRF24** stops its scanner and clears runtime histogram/peak/classification state.
- Leaving **Audio** stops the I2S/PDM acquisition path and audio FFT worker.
- Thermal acquisition is performed only while the Thermal application is active.
- Device owns the encoder while its global settings view is active; an explicit Back action resumes normal app navigation.

Application-specific configuration such as CC1101 frequency range/step, nRF24 channel/dwell settings, Audio range/channel selection, and WiFi/BT alert mode remains runtime state independently of the acquisition workers unless explicitly stored in Device configuration.

## Wi-Fi / BLE data path

```text
Wi-Fi monitor ---\
                  +--> normalized observation --> identity/OUI --> surveillance classifier
BLE monitor -----/              |                       |
                                v                       v
                         rolling device store      alert/history state
                                |                       |
                                v                       +--> bounded audio command queue
                            WiFi / BT UI                         |
                                                               v
                                                        I2S speaker worker
```

The live store is fixed-memory and rolling. OUI/vendor attribution is applied only where the address can defensibly represent an IEEE identity. Random/private addresses remain unknown rather than being assigned a misleading vendor.

Surveillance detections use separate retained state so short-lived observations can remain visible longer than the ordinary nearby-device window.

Audible alerts are optional. WiFi/BT settings select **Off**, **Tone**, or **Morse**; the boot default is Off. Morse alerts use exactly one selected character. The RF observation callback never generates audio synchronously: it submits a bounded command to `audio_output.c/.h`, and a dedicated worker renders the tone or Morse pattern.

## CC1101 data path

```text
CC1101 scanner -> completed RSSI sweep -> direct display levels
                                      \-> optional slow peak hold
                                      \-> conservative RF-shape classifier
                                              |
                                              v
                                      category count panel
```

The visible CC1101 spectrum uses the most recently completed direct RSSI sweep; it is not an accumulated histogram. Historical information is limited to optional slow-decay peak markers and the category counts.

Sweep configuration is bounded to 128 samples. LOW/HIGH/STEP adjustments that would exceed the limit are rejected instead of changing another setting automatically. This provides a predictable upper bound on sweep work and display latency.

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
                               category count panel
```

The nRF24 activity display intentionally differs from CC1101 because nRF24 RPD provides thresholded channel activity rather than calibrated RSSI. Fast activity history therefore gives a more useful visual representation than a raw single sweep.

Validated Enhanced ShockBurst metadata is classified conservatively using address, bitrate, payload size, repetition timing, and known high-confidence signatures. The small active classifier cache is separate from a larger address ledger used for count deduplication. This prevents an identity from being counted again merely because its detailed classifier record was evicted.

Category names with question marks (`Logitech?`, `Input?`, `Sensor?`) communicate that these are inferred identities/types unless the evidence is sufficiently specific.

## Audio data path

```text
PDM microphone -> ESP32-S3 I2S0/PDM RX -> DMA buffers
                                         |
                                         v
                                 channel selection
                                         |
                              filtered decimation
                              (Voice / Mid only)
                                         |
                                         v
                                  512-point FFT
                                         |
                            dBFS normalization / auto scale
                                         |
                                         v
                               latest spectrum frame -> Audio UI
```

`audio_spectrum.c/.h` owns microphone acquisition and DSP state. DMA reads and FFT work run independently of LCD rendering. The UI consumes the most recent completed frame; when a newer frame replaces an unpublished one, the overrun counter records that freshness was preferred over queueing stale visual data.

The DSP currently uses focused Voice, Mid, High, Audible, and Ultrasonic ranges. The audio analyzer is still under hardware validation, so its output should not be treated as a calibrated measurement source.

The planned recorder will reuse this microphone ownership rather than creating a second I2S0 reader. Recording and spectrum analysis should share captured PCM through an explicit fan-out boundary.

## Speaker and Morse output

The onboard speaker is isolated from microphone acquisition:

```text
alert_request()
      |
      v
bounded FreeRTOS queue -> audio_output worker -> I2S1 standard TX -> onboard speaker
```

`audio_output.c/.h` owns I2S1 transmit and waveform generation. Callers enqueue short sound requests with zero wait; a full queue drops the new sound instead of stalling an acquisition callback. I2S is enabled only while a queued sound is being rendered and disabled again when idle.

`morse.c/.h` owns the shared Morse character table. Printable characters represented by ITU-R M.1677-1 are centralized there so the current one-character alert system and the planned Audio Morse browser/player use the same patterns and timing semantics.

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

The MLX90640 is an optional external I2C/Qwiic device. RFScout periodically probes for it while another application is active. When detected and enabled, the Thermal application becomes available near the end of the rotation before Device.

The sensor's native 32x24 frame is processed for display interpolation/scaling, but the renderer does not imply higher underlying sensor resolution.

## Device configuration and time

`device_config.c/.h` owns compiled defaults plus optional SD-backed `/sdcard/rfscout.json` preferences. Configuration loading is nonfatal: RFScout can continue with runtime defaults when storage or persistence fails.

`device_time.c/.h` owns authoritative system wall-clock state and source/provenance metadata. User-visible time and timestamps use the wall clock, while debounce, dwell, retries, timeouts, and housekeeping remain on monotonic timers. Deep-sleep RTC metadata and cold-boot saved-time provenance are treated separately.

## Shared hardware boundaries

### SPI

The LCD, microSD, CC1101, and nRF24 share board SPI resources. `board_spi.c/.h` owns initialization of SPI2 and exposes an idempotent board-level initializer used by both storage and display setup. Fixed pins and chip selects remain centralized in `board.h`. This avoids relying on accidental peripheral initialization order.

### I2C

`board_i2c.c/.h` owns initialization of the shared board I2C controller. Battery/power modules and optional external I2C devices use that shared controller instead of independently configuring SDA/SCL.

`battery.c/.h` owns BQ27220/BQ25896 register access and exposes cached telemetry to the UI. Missing power-management telemetry is nonfatal.

### I2S / PDM

`audio_spectrum.c/.h` owns ESP32-S3 I2S0 PDM receive for the internal microphone. Acquisition uses DMA and a dedicated FFT worker. The receive channel is released when Audio is inactive so the analyzer does not consume CPU and peripheral resources off-screen.

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
- fixed right-side category panels where applicable;
- optional peak hold controlled through application settings where supported.

## Source/module boundary

Hand-maintained modules follow the conventions in [CODING_STYLE.md](CODING_STYLE.md): public declarations live in headers, public APIs are documented with Doxygen, private helpers/state remain `static`, hardware pin definitions remain centralized, and source is intentionally formatted for readability rather than compactness. Generated `.inc` data tables are maintained through their generators instead of hand editing.

## Current versus future capabilities

Implemented application-level capabilities are Wi-Fi/BLE awareness, configurable asynchronous WiFi/BT surveillance sounds, CC1101 spectrum awareness, nRF24 activity/fingerprinting, PDM microphone spectrum analysis, optional MLX90640 thermal imaging, SD logging, battery/status telemetry, Device configuration/time management, and encoder-driven navigation.

Audio recording and the interactive International Morse player are planned next. NFC, IR, GPS UI, and active/replay functions are not part of the current active application set and should not be presented as implemented functionality until they are actually enabled.
