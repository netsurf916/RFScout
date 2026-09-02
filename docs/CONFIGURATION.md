# Device configuration and wall clock

This document describes configuration implemented on current `main`. Planned configuration concepts belong in [ROADMAP.md](ROADMAP.md).

RFScout starts from compiled defaults and, when SD is mounted, overlays persistent preferences from `/sdcard/rfscout.json`. The current canonical schema version is **3**. Unknown/obsolete fields are ignored; saving builds a fresh canonical document, so obsolete keys disappear and newly introduced fields are emitted with current defaults.

RFScout is still pre-1.0. The configuration format may continue to change while the project converges on a stable schema. Compatibility with configuration files from older or newer pre-1.0 firmware is therefore **best effort, not a release contract**. The project begins treating configuration backward compatibility as a compatibility requirement at the v1.0.0 milestone.

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

Alert Morse-character maps are also stored beneath the owning application and are omitted above for brevity. The top-level `audio` object is device audio presentation policy/gain state; it is separate from the historical `apps.audio` key that represents Recorder internally.

The semantic audio model now distinguishes **UI/device**, **Event**, **Alert**, and **Content** audio. The current schema-v3 names predate that classification, so the persisted fields map as follows during this pre-1.0 transition:

- `audio.interaction_gain_percent` is the global **UI volume**;
- `audio.notification_gain_percent` is the global **Event volume**;
- `notifications.volume` is the global **Alert volume**;
- Recorder/content volume remains application-owned.

Application and event APIs use the semantic class names rather than these historical persistence names so the JSON keys can be cleaned up in a later schema revision without leaking storage terminology into app code.

## Loading and migration

Configuration loading follows a deliberately simple defaults-plus-overlay model:

1. populate current compiled defaults;
2. try to parse the existing JSON document;
3. when an older schema is recognized, apply the migrations the current firmware happens to support;
4. overlay only recognized, type-correct, valid fields;
5. ignore unknown/obsolete fields;
6. on canonical save, generate a fresh current-schema document rather than editing the source tree in place.

If the document cannot be parsed, claims an unsupported schema, or otherwise cannot be loaded safely, RFScout is allowed to discard it and write a correctly formatted configuration from current defaults. Pre-1.0 firmware does **not** preserve rejected files as a compatibility archive and does not attempt downgrade-round-trip reconciliation. Upgrading or downgrading pre-1.0 builds can therefore reset settings when formats diverge.

The normal save path still uses `rfscout.json.tmp` and `rfscout.json.bak` transiently to avoid replacing the active file with a partial write. Those are implementation details, not persistent compatibility copies. Startup also removes legacy `rfscout.json.bad`, `rfscout.json.downgrade`, and `rfscout.json.recovering` artifacts left by development builds so the SD card does not accumulate configuration sidecars.

The current schema-v2 -> schema-v3 loader retains a useful best-effort migration for the visual-theme/audio-policy split. Schema-v2 Indoor, Outdoor, and Night configurations retain their visual theme and migrate to **Normal** while carrying forward the two gain values introduced by that migration:

| Legacy theme | `interaction_gain_percent` | `notification_gain_percent` |
| --- | ---: | ---: |
| Indoor | 35% | 100% |
| Outdoor | 55% | 100% |
| Night | 20% | 60% |

Under the semantic audio classification, those retained values become the initial UI and Event volumes respectively. Legacy Discipline maps to **Night + Quiet** and retains the Night-equivalent 20% / 60% values underneath Quiet. Because RFScout is still pre-1.0, this categorization deliberately favors a clean runtime/API model over preserving every development-era effective loudness combination from the short-lived environmental-gain interpretation.

Schema-v1 configurations that predate canonical `display.theme` can still use the existing legacy NVS theme migration when the current loader recognizes them. A schema-v3 file can explicitly store `audio.policy` and both gain fields. Canonical v3 output never writes `discipline` as a theme. These pre-v3 migration paths are conveniences for development-era upgrades and are not guaranteed compatibility behavior until the v1.0.0 configuration contract is established.

The historical application key `apps.audio` remains because the internal application ABI still uses `RFSCOUT_APP_AUDIO`; the user-facing application is Recorder. `apps.startup` writes user-facing aliases such as `home` and `recorder`, while the loader also accepts historical internal aliases.

## Display theme and audio policy

`display.brightness` is the normal backlight percentage. `dim_timeout_seconds` defaults to 120 seconds and `off_timeout_seconds` to 300 seconds; zero disables the corresponding stage. When both are enabled, screen-off must occur after dimming.

Screen-off is a presentation state only: application acquisition, logging, framebuffer updates, and audio behavior continue. Any physical user input wakes a dim/off display, and the wake gesture is consumed before ordinary app, lock, or power-button handling. See [DISPLAY_IDLE.md](DISPLAY_IDLE.md).

Visual themes are **Indoor**, **Outdoor**, and **Night**. Theme selection affects colors/presentation only and is stored under `display.theme`.

Device-wide Audio policy is stored independently under `audio.policy`:

- `normal` permits UI/device, Event, Alert, and explicitly requested Content audio subject to app/global policy;
- `quiet` suppresses UI/device, Event, and Alert audio without changing their saved volumes, event switches, alert enablement, or Morse mappings;
- explicitly requested Content audio such as Recorder playback remains available in Quiet.

The three global incidental levels are exposed in Device Settings as **UI volume**, **Event volume**, and **Alert volume**. Changing Audio policy never overwrites these values. App-native policy remains independent as well; Recorder can remain incidentally quiet even when the device Audio policy is Normal.

Changing the visual theme in schema v3 does not modify Audio policy or any global audio level. Changing Audio policy does not modify the theme.

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

## Events, alerts, and Recorder audio

Ordinary semantic activity such as `RFSCOUT_EVENT_DETECTED` is **Event** audio. WiFi / BT stores independent ordinary Wi-Fi and BLE event-feedback switches, which default enabled when absent. These quick activity beeps use the global Event volume.

Attention-oriented semantic notifications are **Alert** audio. Global alert presentation is `OFF`, `TONE`, or `MORSE`; Alert volume is 0 through 100. Each detecting application owns its configurable alert gate and Morse mappings. Individual alert mappings may also be set to **Off**, suppressing audio for that category without removing the semantic event.

Warnings and errors are also Alert-class sounds even when they do not have a configurable detection-alert descriptor. Quiet suppresses Alert presentation without destroying or rewriting alert configuration.

Recorder playback volume is independent of the three incidental global volumes. Recorder microphone input gain is currently 0 through +24 dB in 3 dB steps.

## Application enablement and availability

An app's `enabled` value is a user preference, not proof that hardware exists. CC1101, nRF24, and Recorder are additionally gated by startup hardware probes; Thermal is hot-plug probed because it is an external attachable peripheral. Home/Device is always enabled.

A configured startup target that is disabled or unavailable falls back to Home for that boot without erasing the saved preference.

## Home / Device Settings

Current Device Settings include:

- brightness, dim timeout, screen-off timeout, and visual theme;
- device-wide **Audio policy** (`Normal` / `Quiet`);
- global **UI volume**, **Event volume**, **Alert** presentation mode, and **Alert volume**;
- **Power button** configuration;
- **Power policy** configuration for low battery and high internal temperature;
- 12/24-hour clock format, timezone, and manual date/time;
- preferred startup application and per-app enablement;
- SD logging;
- explicit configuration save.

Per-app alert mappings and WiFi / BT retention/event switches remain in the owning application menus. Small finite choices use direct center-to-cycle behavior rather than a separate edit mode; numeric volume/range values retain turn-to-adjust editing.

Setters mark canonical configuration dirty. RFScout flushes dirty configuration before deep sleep. Without SD, settings can affect the running session but canonical persistence is unavailable.

## Deep-sleep session state

Deep sleep loses normal RAM, so RFScout keeps a small RTC-retained session envelope separate from canonical configuration. It currently retains the active application and global input-lock state. Open menus, edit state, queued sounds, and active Recorder operations are intentionally not restored.

## Wall clock and timezones

The ESP32 system wall clock is authoritative for shared clock display, log timestamps, and normal filesystem calendar metadata. Monotonic time remains authoritative for polling, debounce, dwell, retries, housekeeping, and other intervals.

Timezone options are stored as stable `name`, user-facing `label`, and POSIX `TZ` rule. Manual time entry is validated after local-time conversion. A plausible RTC-backed wall clock is preserved across software resets, flashing, and deep sleep while that RTC domain remains powered; saved shutdown time is only a cold-boot continuity aid, not proof of synchronization.
