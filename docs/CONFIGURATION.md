# Device configuration and wall clock

This document describes configuration implemented on the PR branch. Planned configuration concepts belong in [ROADMAP.md](ROADMAP.md).

RFScout always starts from compiled defaults. When an SD card is mounted, ordinary persistent preferences are loaded from `/sdcard/rfscout.json`. The selected display theme is intentionally persisted in ESP32 NVS instead so the viewing mode survives boots even when no SD card is installed.

## Configuration model

Schema version 1 is currently supported. A representative SD-backed configuration is:

```json
{
  "version": 1,
  "display": {
    "brightness": 70,
    "clock_24h": false
  },
  "apps": {
    "wifi_bt": true,
    "cc1101": true,
    "nrf24": true,
    "audio": true,
    "thermal": true,
    "device": true,
    "startup": "home"
  },
  "logging": {
    "enabled": true
  },
  "notifications": {
    "mode": "MORSE",
    "volume": 50,
    "characters": {
      "flock": "F",
      "body_camera": "B",
      "drone": "D",
      "camera": "C",
      "smart_glasses": "G",
      "acoustic_sensor": "A",
      "surveillance_other": "S",
      "cc1101_narrow_ism": "N",
      "cc1101_remote_sensor": "R",
      "cc1101_carrier": "K",
      "cc1101_bursty": "U",
      "cc1101_wideband": "W",
      "nrf24_esb": "E",
      "nrf24_input": "I",
      "nrf24_sensor": "T",
      "nrf24_wifi_like": "V",
      "nrf24_ble_like": "L",
      "nrf24_logitech_like": "X"
    }
  },
  "recorder": {
    "volume": 50,
    "input_gain_db": 12
  },
  "timezone": {
    "selected": "America/Chicago",
    "options": [
      {
        "name": "America/Chicago",
        "label": "US Central",
        "posix": "CST6CDT,M3.2.0,M11.1.0"
      },
      {
        "name": "UTC",
        "label": "UTC",
        "posix": "UTC0"
      }
    ]
  },
  "time": {
    "epoch": 1787756400,
    "source": "manual"
  }
}
```

The historical configuration key remains `apps.audio` because the application enum/ABI still uses `RFSCOUT_APP_AUDIO`; the user-facing application is Recorder. `apps.startup` writes user-facing aliases where useful: `home` for Device and `recorder` for Audio. The loader also accepts the historical internal names `device` and `audio`.

### Display theme persistence

The display theme is stored in NVS namespace `rfscout_ui` under key `theme`, independently of `/sdcard/rfscout.json`. Device Settings applies a theme immediately and commits it at selection time, so an SD-card failure does not silently revert an outdoor/night viewing choice on the next boot.

Current themes are:

- **Indoor** — normal high-contrast RFScout orange/white presentation;
- **Outdoor** — maximum daylight contrast, with secondary/chrome information intentionally kept bright rather than subdued;
- **Night** — red/orange-oriented UI colors that reduce bright blue/green output while retaining warning/error distinction.

The theme setting changes semantic UI roles globally, including Home branding. It does not recolor literal sensor imagery such as Thermal heat-map pixels.

## Defaults and validation

Missing or invalid ordinary fields retain compiled defaults. Home/Device is always enabled even if configuration attempts to disable it. The compiled display theme is Indoor; invalid or absent NVS theme state also falls back to Indoor.

The compiled startup target is Home. Valid startup values are Home, WiFi/BT, CC1101, nRF24, Recorder, and Thermal. At boot, a disabled or unavailable configured target falls back to Home for that boot without rewriting the saved preference. Thermal additionally requires a detected MLX90640.

The compiled notification mode is `OFF`; valid saved `OFF`, `TONE`, or `MORSE` values override it. Notification volume accepts `0` through `100` and defaults to 50 when no usable saved value exists. Speaker output applies a nonlinear perceptual curve rather than linear PCM scaling.

Recorder playback volume is a separate `0` through `100` preference and does not alter notification loudness. Recorder microphone input gain is stored as a fixed digital gain from 0 through +24 dB in 3 dB steps.

Each notification category has one compiled Morse-character default. Valid one-character saved values override defaults individually.

`apps.startup` is a backward-compatible addition within schema version 1. Existing files that omit it receive the compiled Home default without requiring a schema migration.

If the complete JSON document cannot be parsed or the schema version is unsupported, RFScout retains the bad file as `rfscout.json.bad`, regenerates defaults, and continues booting. Configuration replacement uses `.tmp` and `.bak` files so an interrupted save can recover.

## Home and Device Settings

Home is the pass-through landing state of the non-disableable Device application. RFScout starts there by default. Merely rotating onto Home does not take ownership of the encoder. Center enters Device Settings; Back returns to Home.

Current Device Settings include:

- LCD brightness;
- display theme (Indoor, Outdoor, Night);
- 12/24-hour clock format;
- timezone selection;
- preferred startup application;
- manual date and time;
- enable/disable state for WiFi/BT, CC1101, nRF24, Recorder (`apps.audio`), and Thermal;
- SD logging enablement;
- global notification mode;
- global notification volume;
- one editable Morse character for every notification category;
- explicit SD-backed configuration save.

Recorder playback volume and microphone input gain are persisted in the SD-backed configuration but are adjusted from Recorder itself. The display theme is persisted immediately in NVS and does not depend on the explicit Save config action.

Without an SD card, SD-backed settings can affect the running session but cannot be persisted. The display theme remains persistent because it uses NVS.

## Application settings inventory

| Application | Current settings/actions | Notification items |
| --- | --- | --- |
| WiFi / BT | Back; Reset detections | Flock/ALPR; Body camera; Drone; Camera; Smart glasses; Acoustic sensor; Other surveillance |
| CC1101 | Back; Profile; Low; High; Step; Peak hold; Reset counts | Narrowband; Remote/sensor; Carrier; Bursty RF; Wide RF |
| nRF24 | Back; Low channel; High channel; Dwell; Peak hold; Reset counts | ESB device; Input; Sensor; WiFi-like; Hopping/BLE-like; Logitech-like |
| Recorder | Back; New recording; Monitor; recording browser; playback volume; delete recording; monitor input gain | None currently |
| Thermal | No application settings menu | None currently |
| Home / Device Settings | Home pass-through; Back; brightness; theme; clock format; timezone; startup app; manual date/time; app enablement; SD logging; notification mode/volume; notification characters; Save config | Configures all registered notification items |

## Notification registry

The notification enum and descriptor table in `alert.c` are intentionally 1:1. Each descriptor owns the application, stable JSON key, user-facing label, and compiled default character. Device and configuration persistence iterate that registry.

Current stable keys are:

| Application | Alert item | JSON key | Default Morse |
| --- | --- | --- | --- |
| WiFi / BT | Flock / ALPR | `flock` | `F` |
| WiFi / BT | Body camera | `body_camera` | `B` |
| WiFi / BT | Drone | `drone` | `D` |
| WiFi / BT | Camera | `camera` | `C` |
| WiFi / BT | Smart glasses | `smart_glasses` | `G` |
| WiFi / BT | Acoustic sensor | `acoustic_sensor` | `A` |
| WiFi / BT | Other surveillance | `surveillance_other` | `S` |
| CC1101 | Narrowband | `cc1101_narrow_ism` | `N` |
| CC1101 | Remote/sensor | `cc1101_remote_sensor` | `R` |
| CC1101 | Carrier | `cc1101_carrier` | `K` |
| CC1101 | Bursty RF | `cc1101_bursty` | `U` |
| CC1101 | Wide RF | `cc1101_wideband` | `W` |
| nRF24 | ESB device | `nrf24_esb` | `E` |
| nRF24 | Input | `nrf24_input` | `I` |
| nRF24 | Sensor | `nrf24_sensor` | `T` |
| nRF24 | WiFi-like | `nrf24_wifi_like` | `V` |
| nRF24 | Hopping/BLE-like | `nrf24_ble_like` | `L` |
| nRF24 | Logitech-like | `nrf24_logitech_like` | `X` |

## Wall clock and timezones

The ESP32 system wall clock is authoritative for the shared clock display, SD log timestamps, and normal filesystem calendar metadata. Monotonic time remains authoritative for polling, RF dwell, debounce, retries, timeouts, and housekeeping.

Timezone choices are stored as data: stable `name`, user-facing `label`, and POSIX `TZ` rule. Manual calendar input is validated after local-time conversion so invalid/nonexistent local times are rejected.

On orderly hard power-off, RFScout can save the current epoch and authority source as a cold-boot continuity aid. Because complete board power removal stops the ESP32 clock, restored saved time is not treated as proof of ongoing synchronization. Deep sleep is handled separately because the RTC domain continues running.

Current implemented authority is manual/saved runtime state. Additional time-authority integrations are roadmap items rather than current configuration behavior.
