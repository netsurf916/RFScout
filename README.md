# RFScout

RFScout is a handheld **RF situational-awareness** instrument for ESP32-S3. A single universal firmware image currently supports runtime profiles for the **LILYGO T-Embed CC1101 Plus** and **LILYGO T-Deck Plus**. The T-Embed remains the hardware-validated baseline; T-Deck initial support is being validated incrementally on hardware.

RFScout combines passive Wi-Fi/BLE observation, board-appropriate RF capabilities, SD logging/storage, semantic input and notifications, persistent device configuration, battery telemetry, and optional hardware-specific applications exposed only when the active runtime profile has a safe provider.

<p align="center">
  <img src="assets/device.jpg" alt="RFScout running on a LILYGO T-Embed CC1101 Plus" width="800">
</p>

RFScout is intentionally passive wherever the underlying radio APIs allow it. It is designed to answer “what electronic activity is around me?” without requiring continuous raw-packet inspection.

## Applications

Top-level application order is:

0. **System Events** — optional and disabled by default
1. **Home / Device Settings**
2. **WiFi / BT**
3. **CC1101** — only when the active board provides the current CC1101 service
4. **nRF24** — only when the active board provides the current nRF24 service
5. **Recorder** — only when the current audio-capture/storage providers are available
6. **Thermal** — only when enabled and an MLX90640 is detected
7. **Audio Spectrum** — when enabled and the microphone provider is available

Disabled or unavailable apps are skipped. Home is always available and is the default/fallback startup target. Applications consume board-neutral semantic input, service availability, and a managed application view rather than board names or physical controls.

### WiFi / BT

WiFi / BT passively observes Wi-Fi/BLE traffic, tracks recent devices in growable app-session PSRAM, performs longest-prefix IEEE vendor attribution, distinguishes private/random addresses from unresolved public addresses, and combines multiple signals for conservative surveillance-oriented classification.

Wi-Fi, BLE, and retained surveillance history currently have independent 10–300 second retention controls. Ordinary Wi-Fi/BLE discovery feedback can be enabled independently; surveillance categories use app-owned notifications.

### CC1101

CC1101 performs receive-only RSSI spectrum sweeps with configurable profiles/range/step, a live activity histogram, conservative RF-behavior classifications, notifications, and count reset.

### nRF24

nRF24 performs receive-only 2.4 GHz RPD scanning with configurable channel bounds/dwell, activity history, conservative classification, and surfaces for validated Enhanced ShockBurst observations. The current RPD scanner does not itself decode packets; no ACK generation, replay, injection, or active discovery is performed.

### Recorder

Recorder uses the available audio-capture provider and SD storage for 16-bit mono WAV capture and playback. T-Embed uses its existing PDM microphone provider; T-Deck uses the onboard ES7210 through the shared board I2C service and an I2S0 four-slot TDM receive path while exposing the selected MIC1 stream to Recorder as ordinary mono PCM. Active recording or finalization blocks app transitions until the file is safely complete.

### Audio Spectrum

Audio Spectrum is the separate live microphone FFT/spectrum app and does not require an SD card. It is exposed only when the active profile has a compatible audio-capture provider. Live frequency labels use measured PCM throughput once available rather than assuming the configured request rate.

### Thermal

Thermal uses an optional MLX90640 on the external I2C/Qwiic interface. Native 32x24 data is interpolated for the LCD; literal thermal imagery bypasses semantic theme color translation.

## Home / Device Settings

Current global settings include brightness/display idle, visual theme, independent **Normal / Quiet** audio policy, UI/Event/Alert presentation levels, supported power-button/protective actions, time source/timezone/manual time, startup/app enablement, SD logging, and touch recalibration when a touch provider is available.

Setting changes are persisted automatically through RFScout's internal persistent-record service; there is no user-facing Save step. App-owned notification settings and WiFi / BT retention remain in their respective app menus.

The Time submenu separates source policy from timezone presentation. **Auto** accepts valid GPS UTC time when the GPS service is available and locked; **Manual** exposes explicit date/time fields. Consumers can distinguish invalid, retained, manual, and GPS-synchronized wall-clock state.

## Power, display, and persistence

Display and power actions are capability-gated by the selected runtime profile. T-Embed retains its validated dedicated power-button, deep-sleep, and physical power-off semantics. The device-wide input lock suppresses navigation, touch, and future keyboard input, but intentionally does not disable T-Embed's dedicated power button.

T-Deck Plus currently exposes a voltage-only battery estimate but no authoritative charger/external-power state, so automatic low-battery power actions are intentionally disabled there. T-Deck also does not currently advertise sleep or physical power-off capabilities.

Persistent preferences are firmware-managed internal records split between device-wide system records and descriptor-owned app records. Touch calibration and time policy are also system-owned bounded records. SD is reserved for removable/bulk data and logging, not configuration authority. See `docs/CONFIGURATION.md`.

## T-Deck Plus baseline

Initial T-Deck Plus support includes runtime board detection, the 320x240 ST7789 display, a managed 320x170 app view plus system-owned navigation region, trackball/center input, calibrated GT911 touch navigation plus semantic app-view Select, GPS location/time service, SD support, voltage-derived battery percentage, standard-I2S speaker output, and ES7210 microphone capture for Recorder/Audio Spectrum. GPIO0/trackball-center is suspended while the ES7210 capture interface owns the conflicting hardware path; app-view touch continues to provide semantic Select during that interval. Hardware capabilities without a validated RFScout provider, including SX1262 and keyboard input, are not advertised to applications yet.

GPS fixes are exposed with explicit validity/freshness and are opportunistically attached to SD log records. A system status indicator shows GPS receiver detection and fix state.

## Hardware and memory

The runtime board profile owns physical pin mappings, buses, display transforms, capabilities, and power semantics. Provider code is compiled into the universal image but initializes only when the selected profile permits it. Unknown hardware remains awake and inert without initializing board GPIO/peripherals so it can be recovered by reflashing.

RFScout requires usable external **PSRAM**. Large app-owned collections can grow dynamically during an app resource session and are reclaimed only after the platform proves workers/callbacks no longer reference them. Internal SRAM remains reserved for system, stack, DMA/hardware-constrained, and latency-sensitive allocations.

See [Board targets and porting](docs/BOARD_PORTING.md).

## Build and flashing

RFScout targets ESP32-S3 with **ESP-IDF 6.1** and enforces clang-format 18.

```sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

The same firmware image selects T-Embed CC1101 Plus or T-Deck Plus at runtime. Ready-for-review pull requests can produce short-lived candidate bundles. Published releases are prepared deliberately through the release workflow and mirrored to the browser flasher.

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
