# Architecture

This document describes the architecture implemented on this PR branch. Forward-looking architecture and feature planning belong in [ROADMAP.md](ROADMAP.md).

## Application manager and lifecycle

Top-level application order is:

```text
WiFi / BT -> CC1101 -> nRF24 -> Recorder -> Thermal (when enabled and present) -> Home / Device Settings
```

RFScout defaults to Home at startup. `device_config` persists a preferred startup application through `apps.startup`; `app_manager` validates that preference against application enablement and runtime hardware availability. A disabled or unavailable target falls back to Home for the current boot without rewriting the saved preference.

Encoder rotation changes applications when the active view is not consuming it for a menu or edit operation. Home is the pass-through state of the non-disableable Device application; center deliberately enters Device Settings.

Lifecycle rules are intentionally explicit:

- leaving WiFi / BT stops Wi-Fi and BLE collectors;
- leaving CC1101 stops scanning and clears runtime spectrum/classification state;
- leaving nRF24 stops scanning and clears runtime activity/classification state;
- leaving Recorder stops capture, monitor, or playback activity;
- Thermal acquisition runs only while Thermal is active;
- Home owns no radio/sensor worker merely by being active;
- Device Settings owns the encoder only after it is deliberately entered.

Startup selection preserves the established initialization ordering. WiFi/BT keeps its top-level boot initialization path; other selected startup applications enter through their normal application lifecycle after shared UI and optional-hardware initialization.

## Wi-Fi / BLE path

```text
Wi-Fi monitor ---\
                  +--> normalized observation --> prefix identity --> surveillance classifier
BLE monitor -----/              |                        |
                                v                        v
                         rolling device store       retained detections
                                |                        |
                                +-----------> WiFi / BT UI
                                                         |
                                                         v
                                                global notification
```

Prefix identity keeps authoritative IEEE vendor attribution separate from curated surveillance significance. Private/random addresses are not assigned misleading IEEE vendors. The surveillance classifier can combine prefix metadata with BLE/protocol evidence.

## CC1101 path

```text
CC1101 worker -> completed RSSI sweep -> live spectrum
                                      -> optional peak hold
                                      -> conservative classifier
                                      -> detection counts
                                      -> global notification on count advance
```

Sweep configuration is bounded to 128 samples. Detection categories describe RF behavior rather than decoded product identity.

## nRF24 path

```text
nRF24 RPD sweeps -> activity history -> optional peak hold
validated ESB observations -> classifier/address ledger -> detection counts
                                                   |
                                                   v
                                           global notification
```

The address ledger is separate from the small active classifier cache so classifier eviction does not cause repeated identity counts.

## Recorder path

```text
internal PDM mic -> ESP32-S3 I2S0/PDM -> Recorder worker
                                              |
                                              +-> runtime clock/rate measurement
                                              +-> PCM capture
                                              +-> async trim/DC removal/normalization
                                              +-> WAV under /sdcard/audio/
                                              +-> validated recording list
                                              +-> I2S1 speaker playback
```

`recorder.c/.h` currently owns microphone configuration and capture directly. The nominal I2S/PDM request is 48 kHz; Recorder measures the physical PDM clock and delivered PCM rate at runtime and uses the effective rate for WAV metadata. The saved format is 16-bit mono PCM, with supported validation/playback rates from 8 through 48 kHz.

Recorder also owns live monitor mode, a 256-point microphone FFT presentation feed, fixed digital input gain, recording enumeration/deletion, and save-time post-processing. Recorder playback volume is independent from the device-wide notification volume.

The historical `audio_spectrum.c/.h` files remain as a compatibility adapter for the existing `RFSCOUT_APP_AUDIO` application/navigation ABI. They no longer implement the removed standalone analyzer.

## Speaker and notification path

```text
detection -> alert category -> global mode/character/volume -> bounded queue -> I2S1 worker
recording selection ---------------------------------------------> WAV playback
```

`audio_output.c/.h` owns I2S1 transmit. Alert commands are nonblocking from detection paths; queue saturation drops a sound rather than blocking acquisition. Notification volume uses a nonlinear perceptual mapping. WAV playback has an independently adjustable Recorder volume.

`alert.c/.h` owns global Off/Tone/Morse policy and the notification descriptor registry. `morse.c/.h` owns the shared Morse patterns.

## Thermal path

MLX90640 is an optional external I2C/Qwiic device. RFScout probes for availability and exposes Thermal only when enabled and detected. The native 32x24 frame is interpolated for display.

## Home, Device configuration, and time

`device_config.c/.h` owns compiled defaults plus optional `/sdcard/rfscout.json` persistence. Configuration is nonfatal: RFScout remains usable without storage.

Persisted state includes app enablement, preferred startup application, brightness, clock format, timezone configuration, global alert mode/volume/characters, Recorder playback volume, Recorder input gain, and selected time metadata.

Home uses the canonical `assets/logo.png` artwork. At configure time `tools/generate_home_logo.py` converts that source image into a compact 2-bit black/orange/white LCD bitmap using only the Python standard library; the generated header is compiled into the firmware and rendered by `home_landing.c`. This keeps the LCD branding visually aligned with the browser flasher without carrying the full PNG decoder or source image in firmware. Shared top chrome remains active; center enters the existing Device Settings editor and Back returns to Home.

`device_time.c/.h` owns authoritative wall-clock state and provenance. Wall-clock time is used for display and calendar timestamps; monotonic timers remain responsible for intervals, dwell, debounce, retries, and timeouts.

## Shared hardware boundaries

### SPI

LCD, microSD, CC1101, and nRF24 share SPI2. `board_spi.c/.h` owns board-level bus initialization; fixed pins and chip selects are centralized in `board.h`.

### I2C

`board_i2c.c/.h` owns the shared board I2C controller. Battery/power devices and optional external sensors use the common bus layer.

### I2S

I2S0 is used by Recorder for PDM microphone capture/monitoring. I2S1 is independently owned by the speaker service.

A future shared Audio Input service is intentionally **not** described here as current architecture; its design and migration goals are tracked in [ROADMAP.md](ROADMAP.md).

## Storage and logging

`storage.c/.h` owns SD mount/state. `log_writer.c/.h` owns asynchronous queued writes, segment rotation, free-space policy, and shutdown. `app_logging.c/.h` coordinates application ENTER/EXIT and rate-limited telemetry.

Filesystem errors are nonfatal and surfaced through shared status UI.

## UI boundary

Application views render their own content into the framebuffer. Shared top chrome is drawn through `ui_chrome_draw()`, which owns title placement plus shared status/battery presentation. Long titles are constrained so they do not collide with the right-side status region.

The bottom of the display is not globally reserved. Views may use a common footer baseline when useful, but data-heavy applications can use the full remaining content height.

`ui_present.c` remains the common presentation layer and applies the global power-off overlay before flushing the framebuffer. It does not own application chrome.

## Release/build boundary

Pull-request and `main` CI perform formatting/tests and, for eligible builds, ESP-IDF firmware packaging. Candidate artifacts are short-lived. The browser-flasher Pages workflow mirrors the candidate from the most recent triggering eligible build and published release assets into a same-origin ESP Web Tools site. Durable releases are prepared only through the manual release workflow.
