# Device configuration and wall clock

This document describes configuration implemented on current `main`. Planned configuration concepts belong in [ROADMAP.md](ROADMAP.md).

RFScout always starts from compiled defaults. When an SD card is mounted, persistent user preferences are loaded from `/sdcard/rfscout.json`. The current canonical schema version is **2**.

## Configuration model

A representative schema-v2 configuration is:

```json
{
  "version": 2,
  "display": {
    "brightness": 70,
    "clock_24h": false,
    "theme": "indoor"
  },
  "apps": {
    "wifi_bt": {
      "enabled": true,
      "retention": {
        "wifi_seconds": 60,
        "bluetooth_seconds": 60,
        "surveillance_seconds": 60
      },
      "notifications": {
        "enabled": true,
        "events": {
          "wifi_device": true,
          "ble_device": true
        },
        "characters": {
          "flock": "F",
          "body_camera": "B",
          "drone": "D",
          "camera": "C",
          "smart_glasses": "G",
          "acoustic_sensor": "A",
          "surveillance_other": "S"
        }
      }
    },
    "cc1101": {
      "enabled": true,
      "notifications": {
        "enabled": true,
        "characters": {
          "cc1101_narrow_ism": "N",
          "cc1101_remote_sensor": "R",
          "cc1101_carrier": "K",
          "cc1101_bursty": "U",
          "cc1101_wideband": "W"
        }
      }
    },
    "nrf24": {
      "enabled": true,
      "notifications": {
        "enabled": true,
        "characters": {
          "nrf24_esb": "E",
          "nrf24_input": "I",
          "nrf24_sensor": "T",
          "nrf24_wifi_like": "V",
          "nrf24_ble_like": "L",
          "nrf24_logitech_like": "X"
        }
      }
    },
    "audio": { "enabled": true },
    "thermal": { "enabled": true },
    "device": { "enabled": true },
    "startup": "home"
  },
  "logging": {
    "enabled": true
  },
  "notifications": {
    "mode": "MORSE",
    "volume": 50
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
  }
}
```

The historical configuration key remains `apps.audio` because the application enum still uses `RFSCOUT_APP_AUDIO`; the user-facing application is Recorder. `apps.startup` writes user-facing aliases where useful: `home` for Device and `recorder` for Audio. The loader also accepts the historical internal names `device` and `audio`.

The exact `time` object is emitted only when RFScout has valid saved wall-clock state to preserve.

## Canonical loading and migration

Configuration loading deliberately follows a defaults-plus-overlay model:

1. populate all current compiled defaults;
2. parse the existing JSON file;
3. accept supported schema versions from 1 through the current version;
4. overlay only recognized, type-correct, valid fields;
5. translate recognized legacy schema-v1 fields into current runtime state;
6. ignore unknown or obsolete fields;
7. when saving, build a new canonical schema-v2 document from runtime state rather than copying the source JSON tree.

The result is intentionally migration-friendly. Fields introduced by newer firmware automatically retain current defaults when absent from an older file and appear on the next save. Unknown or retired fields are ignored during load and disappear when the canonical file is rewritten.

A configuration claiming a schema version newer than the running firmware is rejected rather than being destructively downgraded. Invalid complete documents are retained as `rfscout.json.bad`, defaults are restored, and boot continues. Atomic replacement uses `.tmp` and `.bak` files for recovery.

Schema v1 is still accepted. Its boolean application entries and global `notifications.characters` mappings are translated into the current app-owned representation when loaded. The next save writes only schema v2.

## Display themes

Theme selection is part of canonical `/sdcard/rfscout.json` state under `display.theme`.

Current themes are:

- **Indoor** — normal high-contrast RFScout orange/white presentation;
- **Outdoor** — maximum daylight contrast;
- **Night** — red/orange-oriented UI colors that reduce bright blue/green output;
- **Discipline** — the same visual palette as Night plus suppression of incidental interaction and notification audio.

Discipline does not disable explicitly requested application content audio such as Recorder playback. Theme sound policy is environmental policy layered on top of app-native behavior and user notification preferences; it does not overwrite those preferences.

## WiFi / BT retention configuration

WiFi / BT owns three independent retention settings under `apps.wifi_bt.retention`:

- `wifi_seconds`: lifetime of Wi-Fi source membership in the live device list;
- `bluetooth_seconds`: lifetime of Bluetooth source membership in the live device list;
- `surveillance_seconds`: lifetime of retained classified detections in the surveillance list.

All three default to 60 seconds, accept values from 10 through 300 seconds, and use 10-second increments in the application settings menu. Missing, out-of-range, or non-step-aligned values retain the compiled 60-second default.

Wi-Fi and Bluetooth age independently. If a normalized observation has been seen from both sources, one source can expire while the other remains live. The retained surveillance history has its own timeout so classified detections can be kept for a different period without changing ordinary device-list aging.

## Notification configuration

RFScout separates global presentation from app-owned notification choices.

Global `notifications` contains:

- `mode`: `OFF`, `TONE`, or `MORSE`;
- `volume`: `0` through `100`.

Application notification settings live under the owning app. Detecting apps have an `enabled` master gate plus their own Morse-character mappings. WiFi / BT additionally contains `notifications.events.wifi_device` and `notifications.events.ble_device`, which independently control the lightweight beep for ordinary newly observed Wi-Fi and BLE devices.

Those two discovery-event switches default to `true` when absent, preserving historical behavior for older configuration files. Turning the WiFi / BT notification master gate off suppresses both general discovery feedback and app-specific surveillance notifications without destroying the individual settings.

Recorder content audio is separate from notification policy. Recorder playback volume is a distinct `0` through `100` preference and microphone input gain is a fixed digital gain from 0 through +24 dB in 3 dB steps.

## Defaults and validation

Missing or invalid recognized fields retain compiled defaults. Home/Device is always enabled even if configuration attempts to disable it. The compiled theme is Indoor and the compiled startup target is Home.

A disabled or unavailable configured startup target falls back to Home for the current boot without rewriting the saved preference. Thermal additionally requires a detected MLX90640.

The compiled global notification mode is `OFF`. Notification volume defaults to 50. Each application notification category has a compiled Morse-character default; valid one-character saved values override defaults individually. Wi-Fi, Bluetooth, and surveillance retention each default to 60 seconds.

## Home and Device Settings

Home is the pass-through landing state of the non-disableable Device application. Center enters Device Settings; Back returns to Home.

Current Device Settings include:

- LCD brightness;
- display theme (Indoor, Outdoor, Night, Discipline);
- 12/24-hour clock format;
- timezone selection;
- preferred startup application;
- manual date and time;
- enable/disable state for WiFi/BT, CC1101, nRF24, Recorder (`apps.audio`), and Thermal;
- SD logging enablement;
- global notification mode;
- global notification volume;
- explicit SD-backed configuration save.

Per-app notification enablement and Morse mappings are edited from the corresponding application settings rather than from the global Device menu. WiFi / BT also exposes ordinary Wi-Fi-device and BLE-device discovery feedback switches plus its three retention controls there.

Setters mark canonical configuration dirty. RFScout flushes dirty configuration before deep sleep so a setting changed immediately before sleep is not silently lost.

Without an SD card, settings can affect the running session but canonical persistence is unavailable.

## Application settings inventory

| Application | Current settings/actions | Notification controls |
| --- | --- | --- |
| WiFi / BT | Back; Notifications; WiFi timeout; BT timeout; Surveillance timeout; Reset detections | master gate; WiFi device beep; BLE device beep; Flock/ALPR; Body camera; Drone; Camera; Smart glasses; Acoustic sensor; Other surveillance |
| CC1101 | Back; Profile; Low; High; Step; Peak hold; Notifications; Reset counts | master gate plus Narrowband; Remote/sensor; Carrier; Bursty RF; Wide RF Morse mappings |
| nRF24 | Back; Low channel; High channel; Dwell; Peak hold; Notifications; Reset counts | master gate plus ESB device; Input; Sensor; WiFi-like; Hopping/BLE-like; Logitech-like Morse mappings |
| Recorder | Back; New recording; Monitor; recording browser; playback volume; delete recording; monitor input gain | none currently; native app policy suppresses incidental feedback while preserving content audio |
| Thermal | No application settings menu | none currently |
| Home / Device Settings | global device settings and Save config | global mode and volume only |

## Semantic event and sound-policy relationship

Application/input code emits semantic events only after RFScout has accepted an action or state change. `feedback` resolves those events through the source application's native audio profile and active theme sound policy before asking `audio_output` to play disposable UI feedback.

App-specific detection notifications use the notification subsystem rather than hard-coding tones in detection code. Global mode/volume is applied after the app's notification gate and category mapping. This keeps event meaning, user policy, theme policy, and audio execution separate.

## Deep-sleep session persistence

Deep sleep loses normal RAM, so RFScout keeps a deliberately small RTC-retained session record separate from canonical user configuration. It currently retains:

- active application;
- user-input lock state.

The retained record is guarded by magic/version/checksum validation and is used only on a valid sleep wake. If the retained application is disabled or unavailable, normal fallback behavior applies.

Transient UI transactions are intentionally not restored: open menus, selected rows, edit mode, queued sounds, and active Recorder capture/playback are discarded. Broader application-session persistence is a future framework concern rather than part of schema-v2 user configuration.

## Wall clock and timezones

The ESP32 system wall clock is authoritative for the shared clock display, SD log timestamps, and normal filesystem calendar metadata. Monotonic time remains authoritative for polling, RF dwell, debounce, retries, timeouts, and housekeeping.

Timezone choices are stored as data: stable `name`, user-facing `label`, and POSIX `TZ` rule. Manual calendar input is validated after local-time conversion so invalid/nonexistent local times are rejected.

At startup, RFScout first inspects the already-running RTC-backed system clock. A wall clock from 2020 through the end of 2100 is treated as plausibly valid and is preserved across software resets, firmware flashing, and sleep while the RTC domain remains powered. If retained authority metadata is unavailable, the clock is labeled `RTC retained` rather than inventing GPS, NTP, or manual provenance.

Only when the running wall clock is implausible does RFScout fall back to the saved configuration timestamp. On orderly hard power-off, RFScout can save the current epoch and authority source as a cold-boot continuity aid. Because complete board power removal stops the ESP32 clock, restored saved time is not treated as proof of ongoing synchronization. Saved checkpoints and manual time entry use the same supported 2020-through-2100 range.
