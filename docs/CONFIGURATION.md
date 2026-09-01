# Device configuration and wall clock

This document describes configuration implemented on current `main`. Planned configuration concepts belong in [ROADMAP.md](ROADMAP.md).

RFScout starts from compiled defaults and, when SD is mounted, overlays persistent preferences from `/sdcard/rfscout.json`. The current canonical schema version is **3**. Unknown/obsolete fields are ignored; saving builds a fresh canonical document, so obsolete keys disappear and newly introduced fields are emitted with current defaults.

## Representative schema

```json
{
  "version": 3,
  "display": {
    "brightness": 70,
    "dim_timeout_seconds": 120,
    "off_timeout_seconds": 300,
    "clock_24h": false,
    "theme": "indoor"
  },
  "audio": {
    "policy": "normal",
    "interaction_gain_percent": 35,
    "notification_gain_percent": 100
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

Notification Morse-character maps are also stored beneath the owning application and are omitted above for brevity. The top-level `audio` object is device audio presentation policy/gain state; it is separate from the historical `apps.audio` key that represents Recorder internally.

## Loading and migration

Configuration loading follows a defaults-plus-overlay model:

1. populate current compiled defaults;
2. parse the existing JSON document;
3. accept schema versions 1 through the current version;
4. overlay only recognized, type-correct, valid fields;
5. translate recognized older-schema fields;
6. map legacy `display.theme: "discipline"` to `display.theme: "night"` plus `audio.policy: "quiet"`;
7. move legacy theme-owned sound gain into independent audio gain fields;
8. ignore unknown/obsolete fields;
9. on canonical save, generate a new schema-v3 document rather than editing the source tree in place.

Schema-v2 Indoor, Outdoor, and Night configurations retain their visual theme and migrate to **Normal** while preserving their previous effective sound levels:

| Legacy theme | Interaction gain | Notification environmental gain |
| --- | ---: | ---: |
| Indoor | 35% | 100% |
| Outdoor | 55% | 100% |
| Night | 20% | 60% |

Legacy Discipline maps to **Night + Quiet** and retains the Night-equivalent 20% / 60% gain state underneath Quiet. This means changing the migrated policy back to Normal later produces the same Night-level sound that existed before the separation rather than an unexpected loudness jump.

Schema-v1 configurations that predate canonical `display.theme` still use the existing legacy NVS theme migration before they are rewritten. A schema-v3 file can explicitly store `audio.policy` and both gain fields. Canonical v3 output never writes `discipline` as a theme.

A file claiming a future schema version is rejected rather than rewritten by older firmware. Invalid complete documents are retained as `rfscout.json.bad`; replacement uses `.tmp` and `.bak` files for recovery.

Schema v3 also includes explicit downgrade-round-trip recovery for the schema-v2 behavior above. Before normal configuration loading, RFScout considers `rfscout.json.bad` only when it is a valid schema-v3 document with valid visual-theme and independent audio-policy/gain state; arbitrary corrupt or unrelated quarantine files are left untouched. If a valid older active configuration exists, settings that schema-v2 firmware could legitimately change are merged into the preserved v3 document while the schema-v3 version and independent `audio.*` policy/gain state remain authoritative. A downgraded legacy `discipline` selection is the exception: it is reconciled with the same canonical migration semantics as an ordinary load, becoming **Night + Quiet** with 20% interaction and 60% notification environmental gain. If the active downgraded file is corrupt, the already-validated v3 quarantine is preferred and the corrupt file is retained separately for diagnosis rather than replacing the good quarantine.

The reconciliation write is itself transactional. RFScout writes the merged v3 document to `rfscout.json.recovering`, moves the downgraded active file to `rfscout.json.downgrade`, and then promotes the recovery document to the active path. On the next boot, an interrupted transaction is recognized before ordinary downgrade recovery: a valid `.recovering` document is completed, while an invalid recovery document is rolled back from `.downgrade` when possible. This preserves both schema-v3-only state and legitimate settings edited while running older firmware across a v0.4.0 -> schema-v2 downgrade -> v0.4.0 upgrade, including power loss during the reconciliation itself.

The historical application key `apps.audio` remains because the internal application ABI still uses `RFSCOUT_APP_AUDIO`; the user-facing application is Recorder. `apps.startup` writes user-facing aliases such as `home` and `recorder`, while the loader also accepts historical internal aliases.

## Display theme and audio policy

`display.brightness` is the normal backlight percentage. `dim_timeout_seconds` defaults to 120 seconds and `off_timeout_seconds` to 300 seconds; zero disables the corresponding stage. When both are enabled, screen-off must occur after dimming.

Screen-off is a presentation state only: application acquisition, logging, framebuffer updates, and audio behavior continue. Any physical user input wakes a dim/off display, and the wake gesture is consumed before ordinary app, lock, or power-button handling. See [DISPLAY_IDLE.md](DISPLAY_IDLE.md).

Visual themes are **Indoor**, **Outdoor**, and **Night**. Theme selection affects colors/presentation only and is stored under `display.theme`.

Device-wide Audio policy is stored independently under `audio.policy`:

- `normal` permits incidental/event feedback and enabled detection-notification presentation subject to existing app/global policy;
- `quiet` suppresses those two audible classes without altering notification enablement, Morse mappings, global notification mode, global volume, or the stored environmental gains.

`interaction_gain_percent` is the theme-independent output level used for incidental/event feedback. `notification_gain_percent` is an environmental multiplier applied to the user's existing global notification volume; it does not replace that global volume. These fields primarily preserve pre-v3 effective behavior and are not currently exposed as separate Device Settings controls.

Changing the visual theme in schema v3 does not modify Audio policy or either gain. Changing Audio policy does not modify the theme or gains. New/default configurations use Indoor-equivalent 35% interaction / 100% notification gain.

Quiet does not suppress intentionally requested application content audio such as Recorder playback. App-native policy remains independent as well; Recorder can remain incidentally quiet even when the device Audio policy is Normal.

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

Audio policy is applied to presentation after those settings are evaluated. Quiet therefore temporarily suppresses audible notification presentation without destroying or rewriting the user's notification configuration. In Normal policy, the independent notification environmental gain is multiplied by the user's global notification volume.

Recorder playback volume is independent of notification volume. Recorder microphone input gain is currently 0 through +24 dB in 3 dB steps.

## Application enablement and availability

An app's `enabled` value is a user preference, not proof that hardware exists. CC1101, nRF24, and Recorder are additionally gated by startup hardware probes; Thermal is hot-plug probed because it is an external attachable peripheral. Home/Device is always enabled.

A configured startup target that is disabled or unavailable falls back to Home for that boot without erasing the saved preference.

## Home / Device Settings

Current Device Settings include:

- brightness, dim timeout, screen-off timeout, and visual theme;
- device-wide **Audio policy** (`Normal` / `Quiet`);
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
