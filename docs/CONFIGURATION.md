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
    "dim_timeout_seconds": 120,
    "off_timeout_seconds": 300,
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

The display inactivity values are total idle times measured from the most recent physical user activity. `dim_timeout_seconds` defaults to 120 seconds and `off_timeout_seconds` defaults to 300 seconds; zero disables the corresponding stage. When both are nonzero, `off_timeout_seconds` must be later than `dim_timeout_seconds`. Automatic screen-off only disables LCD presentation: applications, logging, acquisition, and audible notifications continue running. See [Display idle behavior](DISPLAY_IDLE.md) for wake/input semantics.

The historical configuration key remains `apps.audio` because the application enum still uses `RFSCOUT_APP_AUDIO`; the user-facing application is Recorder. `apps.startup` writes user-facing aliases where useful: `home` for Device and `recorder` for Audio. The loader also accepts the historical internal names `device` and `audio`.

An application `enabled` value is a user preference, not a promise that the hardware exists. CC1101, nRF24, and Recorder are additionally gated by one-time startup hardware probes; if a fixed radio or the board PDM microphone is absent, that application is omitted from navigation for the current boot even when configured enabled. The saved preference is preserved so the same configuration can be used across differently populated T-Embed variants. Thermal remains dynamically probed because it is an external attachable peripheral.

The exact `time` object is emitted only when RFScout has valid saved wall-clock state to preserve.

## Canonical loading and migration

Configuration loading deliberately follows a defaults-plus-overlay model:

1. populate all current compiled defaults;
2. parse the existing JSON file;
3. accept supported schema versions from 1 through the current version;
4. overlay only recognized, type-correct, valid fields;
5. translate recognized legacy schema-v1 fields into current runtime state;
6. ignore unknown or obsolete fields;
7. preserve compiled defaults for missing fields; and
8. write the current canonical shape the next time settings are saved.

This lets older configuration files continue working when new optional fields are added. A future incompatible schema change would require an explicit version bump and migration policy rather than silently changing the meaning of existing fields.

## Application enablement and availability

`apps.<name>.enabled` controls whether a supported application participates in normal navigation. Home/Device remains enabled regardless of the saved value. Runtime hardware availability is a separate condition owned by the application availability framework; an enabled application may still be omitted for a boot when its required hardware is unavailable.

The current fixed-hardware probes are startup-cached because those devices cannot be hot-plugged on the supported board topology. Hot-pluggable applications use their own runtime availability callbacks instead. Configuration is not rewritten when runtime hardware is missing.

## Observation retention

WiFi/BT maintains separate live-view retention windows for Wi-Fi observations, Bluetooth observations, and the surveillance list. The three values live under `apps.wifi_bt.retention` and are expressed in seconds. Current accepted values are 10 through 300 seconds in 10-second increments; the compiled default for each is 60 seconds.

Changing a retention value affects whether an observation remains visible in the corresponding current UI list. It does not redefine the observation store's session deduplication identity and does not erase SD log history.

## Application notification preferences

Applications that support detection notifications have a nested `notifications` object. Its `enabled` flag controls whether that application's detection events may generate the configured global Tone/Morse output. Application-specific event switches and Morse-character mappings remain under the same application object.

The top-level `notifications.mode` and `notifications.volume` are global output policy. Recorder playback volume remains separate under `recorder.volume`.

## Persistence and recovery

Runtime setters mark canonical configuration dirty. Explicit save actions and power-state transitions use the same atomic replacement path:

1. write a temporary JSON document;
2. flush and close it;
3. move the previous configuration to a backup path;
4. rename the temporary document into place; and
5. remove the backup after success.

If the active configuration is missing after an interrupted replacement, startup attempts to recover the backup. Invalid configuration is quarantined and regenerated from compiled defaults rather than partially trusting malformed state.

## Timezone and wall-clock state

Timezone definitions are stored as a selected stable name plus configured options containing display labels and POSIX timezone strings. Manual time is maintained separately from timezone presentation.

When RFScout has valid wall-clock state to preserve for hard power-off, it may write a `time` object containing an epoch and source token. That bootstrap state is optional and is not required for ordinary configuration loading.
