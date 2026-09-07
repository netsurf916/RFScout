# Device configuration and persistent state

This is the canonical production reference for RFScout settings and durable device/app state. Planned behavior belongs in [ROADMAP.md](ROADMAP.md).

RFScout starts from compiled defaults and overlays firmware-managed records from internal LittleFS. Removable SD is not a configuration authority: RFScout no longer imports, exports, or synchronizes editable `rfscout.json` files there.

## Persistence ownership

Durable state is addressed by logical owner and key rather than caller-visible filesystem paths:

- logical `system/config` owns device-wide policy: display/power behavior, theme, timezone selection/clock presentation, logging, app enablement/startup choice, global audio policy/gains, and global alert policy;
- logical `app/<persistence_key>/config` owns application-specific preference overrides. The compiled app descriptor supplies the stable persistence identity; app-facing store calls derive ownership from the active app session and cannot select another app namespace;
- other logical `system/*` or `app/<persistence_key>/*` records are reserved for durable state that is not configuration, such as future T-Deck Plus touch calibration or browsable app history.

These names are an API model, not literal filenames. The persistent-store service hashes logical record keys with SHA-256 to fixed filesystem-safe names; app persistence identities are likewise mapped to private app-scope directories. Callers may therefore use opaque logical keys without depending on LittleFS naming rules. Logical keys are nonempty and bounded to 256 bytes.

The persistent-store service owns LittleFS mounting, physical path construction, record availability, and synchronization. Filesystem operations are serialized internally by the service, so callers do not manage storage mutexes. Records are bounded to 16 KiB and each write atomically replaces one record through a same-directory temporary, flush/fsync, and rename. Atomicity is per record rather than a transaction across multiple records. Interrupted-write `.tmp` files are ignored as authority and removed by one startup cleanup pass; no periodic cleanup task runs. Reads and existence checks do not create app directories.

The existing `device_config` API remains the in-memory settings surface so UI and policy callers do not need to know which record owns a setting. App enablement remains system policy even when that app owns its own preferences.

## Defaults and record versions

Current system and application configuration records both use version **1**. These versions are internal validation markers, not compatibility contracts or user-facing interchange schemas.

RFScout does not attempt long-lived migration between internal record layouts. A missing record means compiled defaults. A malformed record or unsupported record version is discarded for that owner. Within an otherwise valid current-version record, malformed or unsupported individual fields are ignored and their compiled defaults remain in effect. This intentionally favors a simple known-good baseline over preserving stale internal representations.

App records are **override-only**. If an app has no persisted user changes, it has no `config` record and uses its compiled defaults directly. Only values that differ from the compiled defaults are serialized. Returning all app preferences to their defaults removes the app record again.

The former monolithic `/internal/rfscout.json` and its sidecars are no longer authoritative and are removed rather than migrated into the owned-record representation. Likewise, old owned records whose version/layout no longer matches current firmware are disposable. If settings portability is added later, it should be an explicit firmware-managed Export/Import operation that validates and maps current owned records rather than exposing internal files as an editable interchange format.

## Display and theme

Brightness is a percentage. Display dim and off timeouts default to 120 and 300 seconds; zero disables a stage, and screen-off follows dim when both are enabled.

Dim/off are presentation states only. Applications, RF acquisition, logging, framebuffer work, and audio continue. Physical activity wakes the display, and the complete wake gesture is consumed before app navigation, input-lock handling, or power-button semantics.

Visual themes are **Indoor**, **Outdoor**, and **Night** and affect presentation colors only. Unsupported values retain the compiled default.

## Audio policy and volumes

RFScout distinguishes **UI/device**, **Event**, **Alert**, and **Content** audio. Device-wide configuration owns UI/Event/Alert policy and levels; app-owned configuration owns content-specific preferences such as Recorder playback volume.

`normal` permits UI, Event, Alert, and requested Content subject to their normal controls. `quiet` suppresses UI, Event, and Alert presentation without rewriting saved levels or semantic events; requested Content such as Recorder playback remains available.

Global Alert presentation is `OFF`, `TONE`, or `MORSE`. Recorder has no persistent capture-gain setting: it records the microphone stream without software pre-amplification, then post-processes the completed WAV through trim, DC/low-frequency high-pass filtering, and bounded normalization. Audio Spectrum uses the same shared one-pole high-pass primitive before its FFT and then applies automatic display ranging; `AUTO N dB` is the current display-window span, not signal gain.

## Power button and protective policy

Valid semantic power actions are `none`, `screen_off`, `sleep`, and `power_off`, subject to gesture/policy and board capability. Defaults are short press `sleep`, long press `power_off`, low-battery threshold 10% with `power_off`, and high internal temperature 90 C with `power_off`.

Low-battery protection requires five consecutive fresh successful fuel-gauge readings and fresh charger evidence that external power is absent. External power cancels a pending low-battery action; the condition rearms after charge rises three percentage points above threshold.

High-temperature protection uses the ESP32-S3 internal temperature sensor, requires five consecutive one-second samples, and rearms 5 C below threshold. High temperature has priority over low battery. Manual and protective transitions use the shared semantic power/overlay path.

## Applications and app-owned preferences

App `enabled` state and the preferred startup app are system-owned policy, not hardware availability proof. Home/Device remains enabled. If the preferred startup app is disabled or unavailable, RFScout falls back for that boot without erasing the preference.

Current app-owned configuration includes:

- **WiFi/BLE:** source retention, surveillance sensitivity, app notification enablement/event choices, and alert-character mappings;
- **Recorder/Audio:** playback volume plus Audio-owned notification preferences;
- other apps own their notification/config fragments only when they have persisted app-specific overrides.

WiFi/BLE surveillance sensitivity is `high`, `medium`, or `low`, defaulting to `high`. High accepts curated name evidence, Medium requires curated prefix/OUI or stronger evidence, and Low requires distinctive company/service evidence or Remote ID. Sensitivity is an evidence threshold, not accumulated confidence.

Wi-Fi and BLE retention each default to 60 seconds and currently accept 10–300 seconds in 10-second steps. Their source membership ages independently. Surveillance classification is an attribute of the canonical active-device record and shares that record's lifetime. Planned `Instant` and `Never` semantics remain in [ROADMAP.md](ROADMAP.md).

## Internal flash, PSRAM, and SD

Internal LittleFS is for bounded records that must survive app exit and reboot. PSRAM remains volatile app-session working memory. The current mainline partition map still provides roughly 8 MiB of LittleFS; the OTA work in PR #75 plans a smaller approximately 4 MiB durable budget alongside two 6 MiB application slots, so persistent consumers must not depend on today's partition size.

SD remains for removable/bulk content such as logs, WAV recordings, captures, and future explicit transfer packages. SD absence is nonfatal to internal configuration, radio/sensor apps, and Audio Spectrum. It prevents SD-backed operations such as Recorder file creation/playback and SD logging. Saving device/app configuration never depends on SD availability.

Recorder uses `/sdcard/audio/` for 16-bit mono PCM WAV files. Logs remain SD-only and use the RTC-backed system wall clock for their UTC timestamps.

## Deep sleep, time, and timezones

Deep sleep loses ordinary RAM, so RFScout keeps a small RTC-backed session envelope separate from persistent configuration. It currently preserves active-app/input-lock session state; menus, edit state, queued sounds, and active Recorder operations are not reconstructed.

The ESP32 RTC-backed system wall clock is authoritative for displayed time, log timestamps, and filesystem calendar metadata. RFScout does not persist an epoch, saved-time validity flag, or clock-source token in configuration. Manual date/time controls update the running system clock only; future GPS/NTP updates use the same runtime clock API. On startup RFScout simply uses the clock value supplied by the RTC domain. Source identity is not durable clock state.

Only the selected timezone and 12/24-hour presentation are persistent user preferences. The available timezone names, labels, and POSIX rules are compiled firmware metadata, so firmware updates can correct those definitions without stale stored copies overriding them. Monotonic time remains authoritative for debounce, dwell, retries, housekeeping, and intervals.

## Erase and update semantics

A full-device erase destroys system and app internal records, including device/app configuration and future calibration/history records. It does not erase removable SD contents.

Ordinary firmware updates with an unchanged partition layout preserve internal files physically, but RFScout intentionally makes no promise that an older internal record layout will remain semantically compatible. An unsupported record simply falls back to defaults. Partition-layout migrations may require a destructive installation and must make that explicit to the user; the web-flasher/OTA planning in PR #75 owns that user-facing distinction.
