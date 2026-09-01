# Device configuration and wall clock

This document describes configuration implemented on current `main`. Planned configuration concepts belong in [ROADMAP.md](ROADMAP.md).

RFScout starts from compiled defaults and, when SD is mounted, overlays persistent preferences from `/sdcard/rfscout.json`. The current canonical schema version is **2**. Unknown/obsolete fields are ignored; saving builds a fresh canonical document, so obsolete keys disappear and newly introduced fields are emitted with current defaults.

## Representative schema

```json
{
  "version": 2,
  "display": {
    "brightness": 70,
    "dim_timeout_seconds": 120,
    "off_timeout_seconds": 300,
    "clock_24h": false,
    "theme": "indoor"
  },
  "power": {
    "short_press": "sleep",
    "long_press": "power_off",
    "low_battery_percent": 10,
    "low_battery_action": "power_off",
    "high_temperature_c": 90,
    "high_temperature_action": "power_off"
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
        }
      }
    },
    "cc1101": { "enabled": true },
    "nrf24": { "enabled": true },
    "audio": { "enabled": true },
    "thermal": { "enabled": true },
    "device": { "enabled": true },
    "startup": "home"
  },
  "logging": { "enabled": true },
  "notifications": {
    "mode": "OFF",
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

Notification Morse-character maps are also stored beneath the owning application and are omitted above for brevity.

## Loading and migration

Configuration loading follows a defaults-plus-overlay model:

1. populate current compiled defaults;
2. parse the existing JSON document;
3. accept schema versions 1 through the current version;
4. overlay only recognized, type-correct, valid fields;
5. translate recognized schema-v1 fields;
6. ignore unknown/obsolete fields;
7. on save, generate a new canonical schema-v2 document rather than editing the source tree in place.

A file claiming a future schema version is rejected rather than rewritten by older firmware. Invalid complete documents are retained as `rfscout.json.bad`; replacement uses `.tmp` and `.bak` files for recovery.

The historical application key `apps.audio` remains because the internal application ABI still uses `RFSCOUT_APP_AUDIO`; the user-facing application is Recorder. `apps.startup` writes user-facing aliases such as `home` and `recorder`, while the loader also accepts historical internal aliases.

## Display configuration

`display.brightness` is the normal backlight percentage. `dim_timeout_seconds` defaults to 120 seconds and `off_timeout_seconds` to 300 seconds; zero disables the corresponding stage. When both are enabled, screen-off must occur after dimming.

Screen-off is a presentation state only: application acquisition, logging, framebuffer updates, and audible behavior continue. Any physical user input wakes a dim/off display, and the wake gesture is consumed before ordinary app, lock, or power-button handling. See [DISPLAY_IDLE.md](DISPLAY_IDLE.md).

Current visual themes are **Indoor**, **Outdoor**, **Night**, and **Discipline**. Discipline uses the Night palette and currently also suppresses incidental interaction and notification audio while preserving explicit application content audio. The planned v0.4.0 separation of visual theme and audio policy is documented only in the roadmap until implemented.

## Power configuration

The `power` object stores both physical-button behavior and protective thresholds/actions. Valid action strings are `none`, `screen_off`, `sleep`, and `power_off`, subject to the specific gesture/policy and board capability.

Defaults are:

- `short_press`: `sleep`;
- `long_press`: `power_off`;
- `low_battery_percent`: `10` (`0`/Off is also valid; menu choices advance in 5% steps through 20%);
- `low_battery_action`: `power_off`;
- `high_temperature_c`: `90` (`0`/Off or 70, 80, 90, 100 C);
- `high_temperature_action`: `power_off`.

The detailed confirmation, hysteresis, charger-freshness, thermal fallback, and transition-overlay contracts live in [POWER.md](POWER.md), which is the single production reference for power behavior.

## WiFi / BT retention

WiFi / BT owns three independent retention settings under `apps.wifi_bt.retention`: `wifi_seconds`, `bluetooth_seconds`, and `surveillance_seconds`. All currently default to 60 seconds and accept 10 through 300 seconds in 10-second increments.

Wi-Fi and BLE source membership age independently for a merged observation. Surveillance history has its own timeout. Planned `Instant` and `Never` semantics are roadmap work and are not current behavior.

## Notifications and Recorder audio

Global `notifications.mode` is `OFF`, `TONE`, or `MORSE`; global volume is 0 through 100. Each detecting application owns its notification gate and Morse mappings. WiFi / BT additionally stores independent ordinary Wi-Fi and BLE discovery-feedback switches, which default enabled when absent.

Recorder playback volume is independent of notification volume. Recorder microphone input gain is currently 0 through +24 dB in 3 dB steps.

## Application enablement and availability

An app's `enabled` value is a user preference, not proof that hardware exists. CC1101, nRF24, and Recorder are additionally gated by startup hardware probes; Thermal is hot-plug probed because it is an external attachable peripheral. Home/Device is always enabled.

A configured startup target that is disabled or unavailable falls back to Home for that boot without erasing the saved preference.

## Home / Device Settings

Current Device Settings include:

- brightness, dim timeout, screen-off timeout, and display theme;
- **Power button** configuration;
- **Power policy** configuration for low battery and high internal temperature;
- 12/24-hour clock format, timezone, and manual date/time;
- preferred startup application and per-app enablement;
- SD logging;
- global notification mode and volume;
- explicit configuration save.

Per-app notification mappings and WiFi / BT retention controls remain in the owning application menus. Small finite choices use direct center-to-cycle behavior rather than a separate edit mode.

Setters mark canonical configuration dirty. RFScout flushes dirty configuration before deep sleep. Without SD, settings can affect the running session but canonical persistence is unavailable.

## Deep-sleep session state

Deep sleep loses normal RAM, so RFScout keeps a small RTC-retained session envelope separate from canonical configuration. It currently retains the active application and global input-lock state. Open menus, edit state, queued sounds, and active Recorder operations are intentionally not restored.

## Wall clock and timezones

The ESP32 system wall clock is authoritative for shared clock display, log timestamps, and normal filesystem calendar metadata. Monotonic time remains authoritative for polling, debounce, dwell, retries, housekeeping, and other intervals.

Timezone options are stored as stable `name`, user-facing `label`, and POSIX `TZ` rule. Manual time entry is validated after local-time conversion. A plausible RTC-backed wall clock is preserved across software resets, flashing, and deep sleep while that RTC domain remains powered; saved shutdown time is only a cold-boot continuity aid, not proof of synchronization.