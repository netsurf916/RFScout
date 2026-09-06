# Device configuration and persistent state

This is the canonical production reference for RFScout settings and durable device/app state. Planned behavior belongs in [ROADMAP.md](ROADMAP.md).

RFScout starts from compiled defaults and overlays firmware-managed records from internal LittleFS. Removable SD is not a configuration authority: RFScout no longer imports, exports, or synchronizes editable `rfscout.json` files there.

## Persistence ownership

Durable state is addressed semantically rather than by filesystem paths:

- `system/config` owns device-wide policy: display/power behavior, theme, time/timezone, logging, app enablement/startup choice, global audio policy/gains, and global alert policy.
- `app/<persistence_key>/config` owns application-specific preferences. The compiled app descriptor supplies the stable persistence key; app-facing store calls derive ownership from the active app session and cannot select another app namespace.
- other `system/*` or `app/<persistence_key>/*` records are reserved for durable state that is not configuration, such as future T-Deck Plus touch calibration or browsable app history.

The persistent-store service owns LittleFS mounting, path construction, record availability, and atomic replacement. Each record is written to a temporary in the same ownership directory, flushed/synchronized, then renamed over its one live copy. Normal persistence does not retain backup/history/quarantine sidecars.

The existing `device_config` API remains the in-memory compatibility surface so UI and policy callers do not need to know which record owns a setting. App enablement remains system policy even when that app owns its own preferences.

## Defaults, schemas, and migration

System configuration currently uses schema version 3. App configuration records have their own record version and may evolve independently. Missing, malformed, incompatible, or invalid values retain compiled defaults; canonical saves rewrite the effective representation.

The first firmware using this model may read the former `/internal/rfscout.json` once. A successful canonical save splits that legacy document into `system/config` plus app-owned `config` records and removes the old file and stale sidecars. SD-based configuration import is not restored.

If settings portability is added later, it should be an explicit firmware-managed Export/Import operation that validates owned records rather than exposing the internal persistence representation as an editable interchange format.

## Display and theme

Brightness is a percentage. Display dim and off timeouts default to 120 and 300 seconds; zero disables a stage, and screen-off follows dim when both are enabled.

Dim/off are presentation states only. Applications, RF acquisition, logging, framebuffer work, and audio continue. Physical activity wakes the display, and the complete wake gesture is consumed before app navigation, input-lock handling, or power-button semantics.

Visual themes are **Indoor**, **Outdoor**, and **Night** and affect presentation colors only. Unsupported values retain the compiled default.

## Audio policy and volumes

RFScout distinguishes **UI/device**, **Event**, **Alert**, and **Content** audio. Device-wide configuration owns UI/Event/Alert policy and levels; app-owned configuration owns content-specific preferences such as Recorder playback volume.

`normal` permits UI, Event, Alert, and requested Content subject to their normal controls. `quiet` suppresses UI, Event, and Alert presentation without rewriting saved levels or semantic events; requested Content such as Recorder playback remains available.

Global Alert presentation is `OFF`, `TONE`, or `MORSE`. Recorder capture gain is app-owned and accepts **0 through +48 dB in 3 dB steps**. Audio Spectrum has no persisted input-gain setting: it analyzes the raw microphone stream through its high-pass/DC blocker and displays an automatically ranged FFT; `AUTO N dB` is the current display-window span, not signal gain.

## Power button and protective policy

Valid semantic power actions are `none`, `screen_off`, `sleep`, and `power_off`, subject to gesture/policy and board capability. Defaults are short press `sleep`, long press `power_off`, low-battery threshold 10% with `power_off`, and high internal temperature 90 C with `power_off`.

Low-battery protection requires five consecutive fresh successful fuel-gauge readings and fresh charger evidence that external power is absent. External power cancels a pending low-battery action; the condition rearms after charge rises three percentage points above threshold.

High-temperature protection uses the ESP32-S3 internal temperature sensor, requires five consecutive one-second samples, and rearms 5 C below threshold. High temperature has priority over low battery. Manual and protective transitions use the shared semantic power/overlay path.

## Applications and app-owned preferences

App `enabled` state and the preferred startup app are system-owned policy, not hardware availability proof. Home/Device remains enabled. If the preferred startup app is disabled or unavailable, RFScout falls back for that boot without erasing the preference.

Current app-owned configuration includes:

- **WiFi/BLE:** source retention, surveillance sensitivity, app notification enablement/event choices, and alert-character mappings.
- **Recorder/Audio:** playback volume and capture input gain, plus any Audio-owned notification preferences.
- other apps own their notification/config fragments only when they have persisted app-specific settings.

WiFi/BLE surveillance sensitivity is `high`, `medium`, or `low`, defaulting to `high`. High accepts curated name evidence, Medium requires curated prefix/OUI or stronger evidence, and Low requires distinctive company/service evidence or Remote ID. Sensitivity is an evidence threshold, not accumulated confidence.

Wi-Fi and BLE retention each default to 60 seconds and currently accept 10–300 seconds in 10-second steps. Their source membership ages independently. Surveillance classification is an attribute of the canonical active-device record and shares that record's lifetime. Planned `Instant` and `Never` semantics remain in [ROADMAP.md](ROADMAP.md).

## Internal flash, PSRAM, and SD

Internal LittleFS is for bounded records that must survive app exit and reboot. PSRAM remains volatile app-session working memory. The current mainline partition map still provides roughly 8 MiB of LittleFS; the OTA work in PR #75 plans a smaller approximately 4 MiB durable budget alongside two 6 MiB application slots, so persistent consumers must not depend on today's partition size.

SD remains for removable/bulk content such as logs, WAV recordings, captures, and future explicit transfer packages. SD absence is nonfatal to internal configuration, radio/sensor apps, and Audio Spectrum. It prevents SD-backed operations such as Recorder file creation/playback and SD logging.

Recorder uses `/sdcard/audio/` for 16-bit mono PCM WAV files. Logs remain SD-only and use UTC ISO-8601 timestamps when wall time is valid, with explicit monotonic `UP+...` timestamps before authority is established.

## Deep sleep, time, and timezones

Deep sleep loses ordinary RAM, so RFScout keeps a small RTC-backed session envelope separate from persistent configuration. It currently preserves active-app/input-lock session state; menus, edit state, queued sounds, and active Recorder operations are not reconstructed.

The ESP32 system wall clock is authoritative for displayed time, log timestamps, and filesystem calendar metadata. Monotonic time remains authoritative for debounce, dwell, retries, housekeeping, and intervals. Timezone options use a stable name, user-facing label, and POSIX `TZ` rule. Saved shutdown time is a cold-boot continuity aid rather than proof of synchronization.

## Erase and update semantics

A full-device erase destroys `system/*` and `app/*` internal records, including device/app configuration and future calibration/history records. It does not erase removable SD contents.

Ordinary firmware updates with an unchanged partition layout are expected to preserve internal records. Partition-layout migrations may require a destructive installation and must make that explicit to the user; the web-flasher/OTA planning in PR #75 owns that user-facing distinction.
