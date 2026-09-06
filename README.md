# RFScout

RFScout is a handheld **RF situational-awareness** instrument. The currently supported board is the **LILYGO T-Embed CC1101 Plus / ESP32-S3**. The firmware uses board-target and capability boundaries so additional hardware can be supported without compiling every board implementation into one universal image.

RFScout combines passive Wi-Fi/BLE observation, sub-GHz spectrum awareness, 2.4 GHz nRF24 activity analysis, SD-backed audio recording/playback, live microphone spectrum monitoring, optional thermal imaging, battery/power telemetry, SD logging, device-wide configuration, semantic event feedback, and application-scoped notifications.

<p align="center">
  <img src="assets/device.jpg" alt="RFScout running on a LILYGO T-Embed CC1101 Plus" width="800">
</p>

RFScout is intentionally passive wherever the underlying radio APIs allow it. It is designed to answer “what electronic activity is around me?” without requiring continuous raw-packet inspection.

## Applications

Top-level rotary order is:

0. **System Events** — optional and disabled by default
1. **Home / Device Settings**
2. **WiFi / BT**
3. **CC1101**
4. **nRF24**
5. **Recorder**
6. **Thermal** — only when enabled and an MLX90640 is detected
7. **Audio Spectrum** — when enabled and the microphone is available

Disabled or unavailable apps are skipped. Home is always available and is the default/fallback startup target. The application layer consumes board-neutral semantic input rather than directly depending on a rotary encoder.

### WiFi / BT

WiFi / BT passively observes Wi-Fi/BLE traffic, tracks recent devices in growable app-session PSRAM, performs longest-prefix IEEE vendor attribution, distinguishes private/random addresses from unresolved public addresses, and combines multiple signals for conservative surveillance-oriented classification.

Wi-Fi, BLE, and retained surveillance history currently have independent 10–300 second retention controls. Ordinary Wi-Fi/BLE discovery feedback can be enabled independently; surveillance categories use app-owned notifications.

### CC1101

CC1101 performs receive-only RSSI spectrum sweeps with configurable profiles/range/step, a live activity histogram, conservative RF-behavior classifications, notifications, and count reset.

### nRF24

nRF24 performs receive-only 2.4 GHz RPD scanning with configurable channel bounds/dwell, activity history, conservative classification, and surfaces for validated Enhanced ShockBurst observations. The current RPD scanner does not itself decode packets; no ACK generation, replay, injection, or active discovery is performed.

### Recorder

Recorder uses the internal PDM microphone and SD storage for 16-bit mono WAV capture and playback. It measures effective PDM/PCM timing at runtime, applies asynchronous trim/DC-removal/normalization, supports recording browse/delete, and has independent playback volume. Active recording or finalization blocks app transitions until the file is safely complete; RFScout does not stop a recording and later attempt to reconstruct or restart it after an aborted transition.

### Audio Spectrum

Audio Spectrum is the separate live microphone FFT/spectrum app and does not require an SD card. On its main view, rotary input remains normal global app navigation; center opens app-local settings. The current setting is fixed digital microphone input gain from **0 through +48 dB in 3 dB steps**.

### Thermal

Thermal uses an optional MLX90640 on the external I2C/Qwiic interface. Native 32x24 data is interpolated for the LCD; literal thermal imagery bypasses semantic theme color translation.

## Home / Device Settings

Current global settings include brightness/display idle, visual theme, independent **Normal / Quiet** Audio policy, UI/Event/Alert presentation levels, short/long power-button actions, low-battery/high-temperature protective policy, clock/timezone/manual time, startup/app enablement, SD logging, and explicit configuration save.

Small finite choices use direct center-to-cycle behavior. App-owned notification settings and WiFi / BT retention remain in their respective app menus.

Visual themes are **Indoor**, **Outdoor**, and **Night** and affect presentation only. Quiet suppresses incidental UI/Event/Alert presentation without mutating saved notification preferences. Explicit application content audio such as Recorder playback remains available.

## Power, display, and persistence

Automatic screen-off affects LCD presentation only. Applications, logging, acquisition, framebuffer updates, and audio continue while the ST7789 is asleep. The whole first physical wake gesture is consumed before ordinary app, lock, or power-button handling.

Short power-button presses can be configured as None, Screen off, Sleep, or Power off; long press supports None or Power off. Protective policy can request sleep/power-off after confirmed low battery or sustained high SoC temperature, using fresh telemetry and conservative external-power checks.

Persistent preferences live in internal flash at `/internal/rfscout.json` using schema version **3**, so normal settings persistence does not depend on an SD card. `/sdcard/rfscout.json` is an explicit one-shot import file, while `/sdcard/rfscout-example.json` is synchronized from the current internal configuration at startup when removable storage is available. Deep sleep separately retains a small RTC-backed session record containing active app and input-lock state.

See [Device configuration and persistence](docs/CONFIGURATION.md) for the detailed configuration, display-idle, power, internal-flash, SD, logging, and schema contracts.

## Hardware and memory

The current target's LCD, microSD, CC1101, and nRF24 share SPI; battery/power telemetry and external sensors share I2C; shared microphone/audio capture uses I2S0 and the speaker service uses I2S1.

RFScout requires usable external **PSRAM**. Large app-owned collections can grow dynamically during an app resource session and are reclaimed only after the platform proves workers/callbacks no longer reference them. Internal SRAM remains reserved for system, stack, DMA/hardware-constrained, and latency-sensitive allocations.

See [Board targets and porting](docs/BOARD_PORTING.md).

## Build and flashing

RFScout targets ESP32-S3 with ESP-IDF 6.x; CI currently builds with ESP-IDF 6.1 and enforces clang-format 18.

```sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Ready-for-review pull requests can produce short-lived candidate bundles. Published releases are prepared deliberately through the release workflow and mirrored to the browser flasher.

## Documentation

The documentation is intentionally consolidated. Production pages describe current behavior; future design and release planning belong only in the roadmap.

- [Capabilities](docs/CAPABILITIES.md) — implemented user-visible behavior
- [Architecture](docs/ARCHITECTURE.md) — app/service boundaries, lifecycle, resource sessions, UI/audio/power/storage architecture
- [Device configuration and persistence](docs/CONFIGURATION.md) — schema, migration, display/power behavior, SD/logging, time
- [Board targets and porting](docs/BOARD_PORTING.md) — board/capability/memory contract
- [Prefix identity](docs/PREFIX_IDENTITY.md) — generated OUI/prefix and surveillance identity data
- [Coding style](docs/CODING_STYLE.md) — source/style requirements
- [Getting started](docs/GETTING_STARTED.md) — development setup
- [Firmware releases](docs/RELEASING.md) — candidate/release/browser-flasher workflow
- [Roadmap](docs/ROADMAP.md) — canonical completed-work and future-release planning

## License

MIT