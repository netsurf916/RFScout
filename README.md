# RFScout

RFScout is a handheld **RF situational-awareness** instrument. The currently supported board is the **LILYGO T-Embed CC1101 Plus / ESP32-S3**. The firmware uses board-target and capability boundaries so additional hardware can be supported without compiling every board implementation into one universal image.

RFScout combines passive Wi-Fi/BLE observation, sub-GHz spectrum awareness, 2.4 GHz nRF24 activity analysis, SD-backed audio recording/monitoring, optional thermal imaging, battery/power telemetry, SD logging, device-wide configuration, semantic event feedback, and application-scoped notifications.

<p align="center">
  <img src="assets/device.jpg" alt="RFScout running on a LILYGO T-Embed CC1101 Plus" width="800">
</p>

RFScout is intentionally passive wherever the underlying radio APIs allow it. It is designed to answer “what electronic activity is around me?” without requiring continuous raw-packet inspection.

## Applications

Top-level navigation order is:

1. **WiFi / BT**
2. **CC1101**
3. **nRF24**
4. **Recorder**
5. **Thermal** — only when enabled and an MLX90640 is detected
6. **Home / Device Settings**

Home is the default startup target. A preferred startup app can be configured; disabled or unavailable targets fall back to Home for that boot without erasing the preference.

The application layer consumes board-neutral semantic input rather than directly depending on a rotary encoder. Holding the center control for about two seconds toggles the global input lock; the separate physical power button remains available.

### WiFi / BT

WiFi / BT passively observes Wi-Fi/BLE traffic, tracks recent devices, performs longest-prefix IEEE vendor attribution, distinguishes private/random addresses from unresolved public addresses, and combines multiple signals for conservative surveillance-oriented classification.

Wi-Fi, BLE, and retained surveillance history currently have independent 10–300 second retention controls. Ordinary Wi-Fi/BLE discovery feedback can be enabled independently; surveillance categories use app-owned notifications.

### CC1101

CC1101 performs receive-only RSSI spectrum sweeps with configurable profiles/range/step, a live activity histogram, bounded 128-sample sweeps, conservative RF-behavior classifications, notifications, and count reset.

### nRF24

nRF24 performs receive-only 2.4 GHz RPD scanning with configurable channel bounds/dwell, activity history, conservative classification, and surfaces for validated Enhanced ShockBurst observations. The current RPD scanner does not itself decode packets; no ACK generation, replay, injection, or active discovery is performed.

### Recorder

Recorder uses the internal PDM microphone and SD storage for 16-bit mono WAV capture/playback. It measures effective PDM/PCM timing at runtime, applies asynchronous trim/DC-removal/normalization, supports recording browse/delete, independent playback volume, live monitor FFT, and fixed microphone input gain.

### Thermal

Thermal uses an optional MLX90640 on the external I2C/Qwiic interface. Native 32x24 data is interpolated for the LCD; literal thermal imagery bypasses semantic theme color translation.

## Home / Device Settings

Current global settings include:

- brightness, display dim timeout, screen-off timeout, and theme;
- configurable short/long **Power button** actions;
- **Power policy** for low battery and high ESP32 internal temperature;
- 12/24-hour clock, timezone, and manual time;
- startup application and application enablement;
- SD logging;
- global notification mode and volume;
- explicit configuration save.

Small finite choices use direct center-to-cycle behavior. App-owned notification settings and WiFi / BT retention remain in their respective app menus.

Current themes are **Indoor**, **Outdoor**, **Night**, and **Discipline**. Discipline currently combines Night's palette with quieter incidental/notification behavior while preserving explicit content audio. The planned v0.4.0 separation into visual theme plus Normal/Quiet audio policy is roadmap work, not current behavior.

## Power and display behavior

Short power-button presses can be configured as `None`, `Screen off`, `Sleep`, or `Power off`; long press supports `None` or `Power off`. If the display is dimmed/off, the entire first wake gesture is consumed before power-button behavior is evaluated.

Protective policy can request sleep/power-off after confirmed low battery or sustained high SoC temperature. Low-battery protection requires fresh fuel-gauge and charger telemetry. High-temperature `Power off` falls back to `Sleep` when removable power cannot be freshly confirmed. See [Power controls and protective policy](docs/POWER.md).

Automatic screen-off affects LCD presentation only. Applications, logging, acquisition, framebuffer updates, and audible behavior continue while the ST7789 is asleep. See [Display idle behavior](docs/DISPLAY_IDLE.md).

## Feedback and notifications

RFScout distinguishes incidental semantic feedback, application-owned detection notifications, and intentional application content audio. Global notification presentation is **OFF**, **TONE**, or **MORSE** plus volume; each detecting app owns its notification gate/mappings.

## Configuration and persistence

Persistent preferences live in `/sdcard/rfscout.json` using schema version **2**. Loading starts from defaults and overlays recognized valid fields. Schema v1 is translated where supported; unknown keys are pruned on the next canonical save; future schema versions are rejected rather than destructively rewritten.

Deep sleep separately retains a small RTC-backed session record containing active app and input-lock state. See [Device configuration and wall clock](docs/CONFIGURATION.md).

## Hardware and memory

The current target's LCD, microSD, CC1101, and nRF24 share SPI; battery/power telemetry and external sensors share I2C; Recorder uses I2S0 and the speaker service uses I2S1.

RFScout currently requires usable external **PSRAM**. Large platform allocations such as the framebuffer already depend on it; internal SRAM is reserved for system, stacks, DMA/hardware-constrained, and latency-sensitive allocations. See [Board targets and porting](docs/BOARD_PORTING.md).

## Build and flashing

RFScout targets ESP32-S3 with ESP-IDF 6.x; CI currently builds with ESP-IDF 6.1 and enforces clang-format 18.

```sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Ready-for-review pull requests can produce short-lived candidate bundles. Published releases are prepared deliberately through the release workflow and mirrored to the browser flasher.

## Documentation

Production documentation describes current `main`; forward-looking design belongs in the roadmap.

- [Capabilities](docs/CAPABILITIES.md) — implemented user-visible capabilities
- [Architecture](docs/ARCHITECTURE.md) — module ownership and resource boundaries
- [Application lifecycle](docs/APP_LIFECYCLE.md) — enter/settle/activate/quiesce contract
- [Board targets and porting](docs/BOARD_PORTING.md) — board/capability/memory contract
- [Device configuration and wall clock](docs/CONFIGURATION.md) — schema, migration, and time
- [Power controls and protective policy](docs/POWER.md) — button and automatic power transitions
- [Display idle behavior](docs/DISPLAY_IDLE.md) — LCD dim/off/wake semantics
- [SD card behavior](docs/SD_CARD.md) — storage, Recorder, and logging
- [Firmware releases](docs/RELEASING.md) — candidate/release/browser-flasher workflow
- [Roadmap](docs/ROADMAP.md) — completed work and future plans

## License

MIT