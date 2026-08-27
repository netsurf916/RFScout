# Device configuration and wall clock

RFScout has a dedicated **Device** application for settings that affect the whole instrument. Runtime behavior always has compiled defaults; an SD card is optional. When an SD card is mounted, persistent preferences live in `/sdcard/rfscout.json`.

## Configuration model

The configuration file is versioned JSON. A typical file is:

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
    "device": true
  },
  "logging": {
    "enabled": true
  },
  "notifications": {
    "mode": "MORSE",
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

The `device` application is always enabled even if an edited configuration attempts to disable it. This prevents configuration from making its own recovery UI inaccessible.

## Defaults and validation

RFScout starts from compiled defaults and then applies valid fields from the SD configuration. Missing or invalid ordinary settings retain their compiled defaults. Invalid timezone entries are ignored. If the selected timezone does not name a usable configured option, RFScout falls back to UTC when available and otherwise to the first valid configured timezone.

The compiled audible-notification mode is `OFF`. That fallback is used when no usable saved notification mode is available. A valid saved `OFF`, `TONE`, or `MORSE` value is restored on boot and overrides the compiled fallback.

Each notification category also has a compiled one-character Morse default. Valid one-character values in `notifications.characters` replace those defaults individually. Unsupported or multi-character values are ignored without invalidating unrelated configuration. The category keys are stable configuration identifiers; the user-facing labels in Device may be more descriptive.

Schema version 1 is currently supported. A document with a missing or unsupported schema version is treated as incompatible rather than being partially interpreted as the current schema.

A saved wall-clock epoch is accepted only when it is within RFScout's supported sanity range. Invalid time state is ignored and is omitted the next time the configuration is written.

If the complete JSON document cannot be parsed, or its schema version is unsupported, RFScout renames it to `rfscout.json.bad`, regenerates a default configuration, and continues booting.

Configuration replacement is crash-recoverable. RFScout writes and synchronizes `rfscout.json.tmp`, moves the previous configuration to `rfscout.json.bak`, promotes the synchronized temporary file to `rfscout.json`, and removes the backup after success. If startup finds the main configuration missing but the backup present, it restores the backup before loading settings.

## Device application

Device has a pass-through landing screen. Merely rotating onto Device does not give settings ownership of the encoder: another encoder tick continues normal application navigation. Press the center button to enter Device settings.

The settings menu always opens with **Back** selected first. Pressing center again immediately returns to the Device landing screen without changing configuration. Current settings include:

- LCD brightness;
- 12-hour or 24-hour clock presentation;
- timezone selection;
- manual date and time;
- enable/disable state for WiFi/BT, CC1101, nRF24, Audio, and Thermal;
- SD logging enablement;
- global audible-notification mode (`OFF`, `TONE`, or `MORSE`);
- an individually editable Morse character for every currently defined detection category;
- explicit configuration save.

Notification categories cover WiFi/BLE surveillance classifications plus CC1101 and nRF24 detection classifications. The character setting affects Morse mode only; Tone mode uses the same short tone for every category.

Application enablement takes effect when navigation resumes. Disabled applications are skipped. Thermal additionally requires an MLX90640 to be detected.

Without an SD card, Device remains usable and settings can affect the running session, but persistent saves are unavailable. With storage available, **Save config** writes the active notification mode, every category mapping, and the other Device preferences to `/sdcard/rfscout.json`. An orderly hard power-off also writes the current in-memory preferences, so the explicit Save action is useful when persistence is wanted before shutdown or deep sleep.

Manual calendar input is validated after local-time conversion. Invalid dates and local times that cannot be represented in the selected timezone, such as a time inside a daylight-saving transition gap, are rejected rather than silently normalized.

## Notification behavior

The notification policy is global rather than owned by the WiFi/BT application. Detection-producing applications request a notification category and the shared alert service applies the configured mode and Morse mapping.

Current categories include:

- surveillance: Flock/ALPR, body camera, drone, camera, smart glasses, acoustic sensor, and other surveillance;
- CC1101: narrowband, remote/sensor, carrier, bursty RF, and wide RF;
- nRF24: ESB device, input, sensor, Wi-Fi-like activity, BLE/hopping-RF-like activity, and Logitech-like activity.

CC1101 and nRF24 notifications are generated when their accumulated detection-category count advances. Resetting those counts resets the notification baseline as well. WiFi/BLE surveillance notifications are generated for new observations classified as surveillance.

Audio and Thermal currently expose analyzer/sensor views rather than discrete detection classifiers, so they have no category event to notify yet. The global notification interface is application-independent and can be used by future Audio or Thermal detectors without moving policy back into an individual app.

## Wall clock and monotonic time

RFScout deliberately separates two kinds of time.

The ESP32 system wall clock is authoritative for:

- the clock displayed in the shared status area;
- SD log timestamps;
- filesystem/calendar timestamps provided through the normal ESP-IDF/FAT path.

Monotonic timing remains authoritative for:

- polling intervals;
- RF scan dwell times;
- debounce and button-hold timing;
- retry intervals;
- log flush intervals;
- timeouts and housekeeping.

Changing the wall clock must never affect those monotonic operations.

## Setting and restoring time

Manual time setting is always available from Device. RFScout records the mechanism that established the wall clock. Current source tokens are designed to include:

- `manual`;
- `gps`;
- `ntp`;
- `rtc` or a specific future RTC such as `rtc:ds3231`.

The `time` object is not continuously rewritten while the clock advances. During a normal hard power-off, after the log writer has drained and before the SD card is unmounted, RFScout updates the saved epoch and source in `rfscout.json`. On the next cold power-on, that saved epoch bootstraps the ESP32 system clock.

Because the ESP32-S3 clock does not run through complete board power removal, the saved epoch is the time at the previous orderly shutdown. It cannot account for elapsed time while the board was physically unpowered. RFScout therefore tracks that the current runtime clock was restored from saved state while preserving the original authority that last set it, for example `Manual (saved)`. A restored saved time is a startup continuity aid, not proof that the displayed wall clock is still synchronized to real time after an arbitrary powered-off interval.

Deep sleep is different. The ESP32-S3 RTC domain continues running through deep sleep. RFScout therefore does not replace the running system time with the saved hard-shutdown checkpoint after a deep-sleep wake. Clock-authority metadata is retained in RTC memory so the resumed clock keeps the same source identity without rewriting the system time.

## Timezones

Timezone choices are data rather than a fixed firmware enumeration. Each option contains:

- `name`: a stable identifier used by the configuration;
- `label`: user-facing text in Device;
- `posix`: the POSIX `TZ` rule actually supplied to newlib/ESP-IDF.

This allows timezone choices and DST rules to be changed by editing the SD configuration without rebuilding firmware.

The system wall clock remains UTC epoch time. Timezone selection only changes local presentation and conversion of manually entered local calendar time.

## Logging

When wall time is valid, every SD record begins with a UTC ISO-8601 timestamp, for example:

```text
2026-08-26T19:52:41Z APP CONFIG display.brightness old=70 new=80
```

If no valid wall time has been established, the logger explicitly falls back to a monotonic marker such as `UP+123456` rather than inventing a calendar date.

Settings changes are logged through the same asynchronous writer. Manual wall-clock changes include the old UTC time, new UTC time, and source so discontinuities in subsequent records remain explainable.

## Future external RTC behavior

External RTC support is intentionally outside the first Device/settings implementation. The time-service interface already preserves a specific source identifier so different authorities are not collapsed into a generic `external` state.

When a supported external RTC is later detected on QWIIC/I2C, RFScout should prompt the user rather than silently choosing an authority. The prompt should offer at least:

1. **Read RTC** — set RFScout's authoritative system wall clock from the RTC.
2. **Set RTC** — write RFScout's current authoritative wall clock to the external RTC.

A GPS-derived time source can use the same authority model without pretending it has the same synchronization semantics as an RTC.
