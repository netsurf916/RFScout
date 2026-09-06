# Device configuration and persistence

This is the canonical production reference for persistent settings and device-level presentation/power behavior. Planned configuration concepts belong in [ROADMAP.md](ROADMAP.md).

RFScout starts from compiled defaults and treats the configuration stored in internal flash as authoritative. The current canonical schema is **version 3**. An SD card is optional for configuration persistence; when present it provides an explicit import path and a synchronized human-readable example/current copy.

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
    "system_events": { "enabled": false },
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
    "audio_spectrum": { "enabled": true },
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

Alert Morse maps are stored beneath their owning app and omitted above for brevity. The historical `apps.audio` key represents Recorder internally; `apps.audio_spectrum` independently controls the Audio Spectrum app. User-facing startup aliases such as `home` and `recorder` are written canonically where supported.

## Authority, load, and save rules

The internal LittleFS partition is the single source of truth during normal operation. The durable live file is `/internal/rfscout.json`; no internal backup, quarantine, or history copies are retained.

Startup follows this order:

1. mount internal LittleFS;
2. remove any stale internal write temporary and legacy development sidecars;
3. if `/sdcard/rfscout.json` exists, validate it as a current-schema import;
4. if valid, copy it to `/internal/rfscout.json.tmp`, flush it, and atomically rename it over the internal live file;
5. load and canonicalize the imported configuration internally, then delete the SD import only after that canonical save succeeds;
6. if the SD import is malformed or has an incompatible schema, rename it to `/sdcard/rfscout.json.bad` and leave the internal configuration unchanged;
7. load the internal configuration over compiled defaults; and
8. if the internal file is missing, corrupt, or incompatible, replace it with a freshly generated current-default configuration.

Internal writes always serialize a complete configuration to `/internal/rfscout.json.tmp`, flush and close it, then atomically rename it over `/internal/rfscout.json`. The temporary is disposable after an interrupted write; RFScout never treats it as a second authoritative configuration.

Internal schema compatibility is deliberately simple while RFScout remains pre-1.0: the stored internal document must match the firmware's current schema. An incompatible internal document is replaced by current defaults rather than migrated through backup/recovery files.

### SD import and example workflow

`/sdcard/rfscout.json` means **import this configuration**. RFScout never uses it as the normal live configuration. A successful import consumes and removes the file only after the internal copy has been parsed and canonicalized successfully. A failed parse/schema validation preserves the user's input as `/sdcard/rfscout.json.bad` so it is not retried on every boot.

`/sdcard/rfscout-example.json` is never imported under that name. It is a byte-for-byte copy of the current authoritative internal configuration and is intended to show the exact schema and current values a user can copy, rename to `rfscout.json`, edit, and reinsert for import.

RFScout compares the example and internal files before writing. If they already match, the SD card is not rewritten. The example is synchronized once at startup, after import/load/default recovery has established the authoritative internal configuration. Ordinary internal configuration saves do not touch removable storage.

## Display and theme

`display.brightness` is the normal backlight percentage. `dim_timeout_seconds` defaults to 120 seconds and `off_timeout_seconds` to 300 seconds; zero disables a stage. When both are enabled, screen-off must occur after dimming.

Dim/off are presentation states only. Applications, RF acquisition, logging, framebuffer work, and audio continue. In the off state RFScout sets the backlight to zero, disables ST7789 output, and places the panel in hardware sleep. Physical user activity wakes the display; the complete wake gesture is consumed before app navigation, lock handling, or power-button behavior. Wake performs panel sleep-out/settle, presents the latest framebuffer while dark, then restores configured brightness.

Visual themes are **Indoor**, **Outdoor**, and **Night** and affect colors only. Legacy `discipline` theme text is normalized to **Night + Quiet** if encountered in an otherwise current-schema document.

## Audio policy and volumes

RFScout distinguishes **UI/device**, **Event**, **Alert**, and **Content** audio. Current schema-v3 names predate that terminology:

- `audio.interaction_gain_percent` -> global **UI volume**;
- `audio.notification_gain_percent` -> global **Event volume**;
- `notifications.volume` -> global **Alert volume**;
- Recorder/content volume remains app-owned.

`audio.policy` is independent from theme:

- `normal` permits UI, Event, Alert, and requested Content subject to app/user policy;
- `quiet` suppresses UI, Event, and Alert presentation without rewriting saved levels, app switches, alert enablement, or Morse mappings;
- requested Content such as Recorder playback remains available.

Global Alert presentation is `OFF`, `TONE`, or `MORSE`. Individual alert mappings may also be Off without deleting the semantic/log event. Recorder playback volume is independent of the three global incidental levels. The persisted `recorder.input_gain_db` remains the Recorder capture-gain setting and currently accepts **0 through +24 dB in 3 dB steps**. Audio Spectrum has a separate runtime live-monitor gain from **0 through +48 dB in 3 dB steps**; that live setting is not currently persisted to `rfscout.json`. Whether these should converge on one calibrated persisted Audio Input setting remains roadmap work.

## Power button and protective policy

Valid semantic power actions are `none`, `screen_off`, `sleep`, and `power_off`, subject to gesture/policy and board capability.

Defaults:

- short press: `sleep`;
- long press: `power_off`;
- low battery: 10%, action `power_off`;
- high internal temperature: 90 C, action `power_off`.

Short press may expose None, Screen off, Sleep, or Power off. Long press exposes None or Power off; Sleep is intentionally not a long-hold action because the same button is the deep-sleep wake source. If the display is dim/off, wake consumes the whole gesture before power semantics run.

Low-battery protection requires the threshold for five consecutive **fresh successful** fuel-gauge reads plus fresh charger telemetry confirming external power is absent. Failed reads break confirmation. External power cancels a pending low-battery action; the condition rearms after charge rises three percentage points above threshold.

High-temperature policy uses the ESP32-S3 internal temperature sensor, not MLX90640. It requires five consecutive one-second samples and rearms 5 C below threshold. High temperature takes priority over low battery. When high-temperature action is Power off, RFScout refreshes charger evidence first; if external-power absence cannot be freshly established, it falls back to Sleep when supported.

Manual and protective transitions share the same global overlay and semantic execution path. Protective warnings use a five-second countdown and are canceled only when the owning policy observes that the underlying condition cleared.

## Application enablement and availability

An app's `enabled` value is user preference, not hardware proof. Home/Device is always available. CC1101, nRF24, and Audio Spectrum use startup availability probes; Recorder and Thermal are hot-plug aware. A disabled/unavailable preferred startup app falls back to Home for that boot without erasing the preference.

Current Device Settings expose brightness/dim/off/theme, Audio policy and global levels, power-button/power-policy choices, clock/timezone/manual time, startup/app enablement, SD logging, and explicit save. Audio Spectrum is enabled independently from Recorder. Small finite choices use direct center-to-cycle behavior; numeric ranges retain edit/rotate interaction. Per-app alert mappings and WiFi / BT retention stay in the owning app menus.

## WiFi / BT retention

Current production retention is three independent values under `apps.wifi_bt.retention`: `wifi_seconds`, `bluetooth_seconds`, and `surveillance_seconds`. Each defaults to 60 seconds and accepts 10–300 seconds in 10-second steps.

Wi-Fi and BLE source membership age independently in merged observations. Surveillance history has its own window. Planned `Instant` and `Never` semantics remain in [ROADMAP.md](ROADMAP.md).

## Internal flash, SD, Recorder files, and logging

The 16 MiB flash layout reserves an 8 MiB factory application partition and the remaining approximately 8 MiB LittleFS partition for internal persistence. Configuration currently uses only the single live JSON file (plus a short-lived write temporary); the remaining internal capacity is intentionally left available for future system-owned persistent records/database use.

The current board has no separate SD card-detect GPIO; SD storage becomes available after successful mount/probe. SD absence is nonfatal to configuration persistence, radio/sensor apps, and Audio Spectrum. It prevents Recorder file operations, SD logs, configuration import/export-example synchronization, and other bulk/removable-storage operations.

The conservative board-tested baseline is a FAT-compatible SanDisk card of 32 GB or less. Cards shipped as exFAT may require reformatting to a supported FAT filesystem.

Recorder uses `/sdcard/audio/` and writes 16-bit mono PCM WAV. It records the measured effective PCM sample rate in the WAV header. Supported playback validation accepts 8–48 kHz mono 16-bit PCM. Current post-processing includes 300 ms head trim, 600 ms tail trim, DC removal, and bounded normalization. Live FFT/spectrum monitoring belongs to Audio Spectrum and does not require storage.

Logs remain SD-only. They rotate through `rfscout-00.log` ... `rfscout-99.log`, up to 4 MiB each (about 400 MiB normal maximum), while attempting to retain at least 64 MiB filesystem free. Valid wall time uses UTC ISO-8601 timestamps; before wall time is established logs use explicit monotonic `UP+...` timestamps.

## Deep-sleep session state

Deep sleep loses normal RAM, so RFScout keeps a small RTC-backed envelope separate from canonical configuration. It currently stores active app and global input-lock state. Menus, edit state, queued sounds, and active Recorder operations are intentionally not restored.

## Wall clock and timezones

The ESP32 system wall clock is authoritative for shared display time, log timestamps, and filesystem calendar metadata. Monotonic time remains authoritative for debounce, dwell, retries, housekeeping, and intervals.

Timezone options use stable `name`, user-facing `label`, and POSIX `TZ` rule. Manual time is validated after local-time conversion. RTC-backed wall time may survive resets/flashing/deep sleep while that domain remains powered; saved shutdown time is only a cold-boot continuity aid, not synchronization proof.
