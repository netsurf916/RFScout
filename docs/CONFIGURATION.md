# Device configuration and persistent state

This is the canonical production reference for RFScout settings and durable device/app state. Planned behavior belongs in [ROADMAP.md](ROADMAP.md).

RFScout starts from compiled defaults and overlays firmware-managed records from internal LittleFS. Removable SD is not a configuration authority: RFScout does not import, export, or synchronize editable `rfscout.json` files there.

## Persistence ownership

Durable state is addressed by logical owner and key rather than caller-visible filesystem paths:

- logical `system/config` owns ordinary device-wide policy: display/power behavior, theme, timezone selection/clock presentation, SD logging, app enablement/startup choice, global audio policy/gains, and global alert policy;
- logical `app/<persistence_key>/config` owns application-specific preference overrides. The compiled app descriptor supplies the stable persistence identity; app-facing store calls derive ownership from the active app session and cannot select another app namespace;
- logical `system/time.policy` owns the small Auto/Manual wall-clock synchronization policy;
- logical `system/touch.calibration` owns bounded touch calibration tied to compatible board/display geometry;
- logical `system/breadcrumbs` owns breadcrumb enablement and retained-file limit; and
- other logical `system/*` or `app/<persistence_key>/*` records are reserved for durable state that is not ordinary configuration.

These names are an API model, not literal filenames. The persistent-store service hashes logical record keys with SHA-256 to fixed filesystem-safe names; app persistence identities are likewise mapped to private app-scope directories. Callers may therefore use opaque logical keys without depending on LittleFS naming rules. Logical keys are nonempty and bounded to 256 bytes.

The persistent-store service owns LittleFS mounting, physical path construction, record availability, and synchronization. Filesystem operations are serialized internally by the service, so callers do not manage storage mutexes. Records are bounded to 16 KiB and each write atomically replaces one record through a same-directory temporary, flush/fsync, and rename. Atomicity is per record rather than a transaction across multiple records. Interrupted-write `.tmp` files are ignored as authority and removed by one startup cleanup pass; no periodic cleanup task runs. Reads and existence checks do not create app directories.

The `device_config` API remains the in-memory surface for ordinary device/app settings. Small system-owned services such as time policy, touch calibration, and breadcrumbs own their specialized records directly rather than enlarging the ordinary configuration schema.

## Save semantics

Ordinary configuration changes are persisted automatically; the Home UI no longer exposes a **Save config** action. Setters mark the owned configuration state dirty and the main service loop flushes that dirty state in the same normal UI/service iteration.

A successful save clears the dirty state only after all current app override records and the system record have been written successfully. A failed save leaves the state dirty so the change is not falsely considered durable. Persistence failures must remain diagnosable; callers should not assume an in-memory change has reached flash merely because the setter accepted the value.

The small time-policy, touch-calibration, and breadcrumb-settings records are written directly by their owning services because they are system state outside ordinary `system/config`. Time-mode and breadcrumb-setting changes use persist-before-publish semantics: if the owned record write fails, the new setting is not exposed as accepted runtime state. Manual-time commit also validates and persists Manual policy before changing the wall clock.

## Defaults and record versions

Current system and application configuration records both use version **1**. These versions are internal validation markers, not compatibility contracts or user-facing interchange schemas. Service-owned records carry their own small version markers.

RFScout does not attempt long-lived migration between internal record layouts. A missing record means compiled defaults. A malformed record or unsupported record version is discarded for that owner. Within an otherwise valid current-version record, malformed or unsupported individual fields are ignored and their compiled defaults remain in effect. This intentionally favors a simple known-good baseline over preserving stale internal representations.

App records are **override-only**. If an app has no persisted user changes, it has no `config` record and uses its compiled defaults directly. Only values that differ from the compiled defaults are serialized. Returning all app preferences to their defaults removes the app record again.

The former monolithic `/internal/rfscout.json` and its sidecars are no longer authoritative and are removed rather than migrated into the owned-record representation. Likewise, old owned records whose version/layout no longer matches current firmware are disposable. If settings portability is added later, it should be an explicit firmware-managed Export/Import operation that validates and maps current owned records rather than exposing internal files as an editable interchange format.

## Display and theme

Brightness is a percentage. Display dim and off timeouts default to 120 and 300 seconds; zero disables a stage, and screen-off follows dim when both are enabled. The Device Settings UI presents a zero timeout as **Disabled** rather than **Off** so the value describes the automatic policy rather than the current screen state.

Dim/off are presentation states only. Applications, RF acquisition, logging, framebuffer work, and audio continue. Physical activity wakes the display, and the complete wake gesture is consumed before app navigation, input-lock handling, or power-button semantics. Boards without an RFScout user/power key do not enter power-key handling as part of this wake path.

Visual themes are **Indoor**, **Outdoor**, and **Night** and affect presentation colors only. Unsupported values retain the compiled default.

## Touch calibration

Touch calibration is system-owned state rather than app configuration or SD data. It is stored as one small versioned `system/touch.calibration` record.

Compatibility is checked against the active board identity, physical/native display geometry, and transform metadata. Missing or incompatible calibration causes startup calibration before touch is authoritative. Explicit recalibration captures a replacement and atomically overwrites the same record only after the new geometry passes sanity checks. If persistence fails, the newly captured calibration may remain active for that boot while the previously stored record remains authoritative after reboot.

## Audio policy and volumes

RFScout distinguishes **UI/device**, **Event**, **Alert**, and **Content** audio. Device-wide configuration owns UI/Event/Alert policy and levels; app-owned configuration owns content-specific preferences such as Recorder playback volume.

`normal` permits UI, Event, Alert, and requested Content subject to their normal controls. `quiet` suppresses UI, Event, and Alert presentation without rewriting saved levels or semantic events; requested Content such as Recorder playback remains available where the active board has a speaker provider.

Global Alert presentation is `OFF`, `TONE`, or `MORSE`. Recorder has no persistent capture-gain setting: it records the microphone stream without software pre-amplification, then post-processes the completed WAV through trim, DC/low-frequency high-pass filtering, and bounded normalization. Audio Spectrum uses the same shared one-pole high-pass primitive before its FFT and then applies automatic display ranging; `AUTO N dB` is the current display-window span, not signal gain.

## Battery and protective power policy

Valid semantic power actions are `none`, `screen_off`, `sleep`, and `power_off`, subject to gesture/policy and runtime board capability. Compiled defaults remain short press `sleep`, long press `power_off`, low-battery threshold 10% with `power_off`, and high internal temperature 90 C with `power_off`; unsupported actions are not executable on a profile that does not advertise them.

T-Embed uses its fuel-gauge/charger telemetry. Low-battery protection requires five consecutive fresh successful percentage readings plus fresh authoritative evidence that external power is absent. External power cancels a pending low-battery action; the condition rearms after charge rises three percentage points above threshold.

T-Deck currently exposes only voltage-derived battery percentage. Because no authoritative external-power/charging state is available, RFScout deliberately does **not** use that estimated percentage to trigger an automatic sleep or power-off action. This prevents a low battery that is already plugged in from entering a shutdown loop.

High-temperature protection uses the ESP32-S3 internal temperature sensor, requires five consecutive one-second samples, and rearms 5 C below threshold. High temperature has priority over low battery, but any requested action is still constrained by the semantic power capabilities of the active profile.

On T-Embed, terminal sleep/power-off preparation first persists dirty configuration and quiesces removable-storage users. Active Recorder recording/finalization vetoes the transition rather than being interrupted and reconstructed. The SD volume itself remains mounted for the running firmware lifetime; terminal transitions do not add a separate unmount/remount recovery state machine.

## Applications and app-owned preferences

App `enabled` state and the preferred startup app are system-owned policy, not hardware availability proof. Home/Device remains enabled. If the preferred startup app is disabled or unavailable on the active board, RFScout falls back for that boot without erasing the preference.

Current app-owned configuration includes:

- **WiFi/BLE:** source retention, surveillance sensitivity, app notification enablement/event choices, and alert-character mappings;
- **Recorder/Audio:** playback volume plus Audio-owned notification preferences; and
- other apps own their notification/config fragments only when they have persisted app-specific overrides.

WiFi/BLE surveillance sensitivity is `high`, `medium`, or `low`, defaulting to `high`. High accepts curated name evidence, Medium requires curated prefix/OUI or stronger evidence, and Low requires distinctive company/service evidence or Remote ID. Sensitivity is an evidence threshold, not accumulated confidence.

Wi-Fi and BLE retention each default to 60 seconds and currently accept 10–300 seconds in 10-second steps. Their source membership ages independently. Surveillance classification is an attribute of the canonical active-device record and shares that record's lifetime. Planned `Instant` and `Never` semantics remain in [ROADMAP.md](ROADMAP.md).

## Internal flash, PSRAM, and SD

Internal LittleFS is for bounded records that must survive app exit and reboot. PSRAM remains volatile app-session working memory. Persistent consumers must remain independent of the physical LittleFS partition size so later OTA layout changes do not leak into service contracts.

SD remains for removable/bulk content such as logs, breadcrumb tracks, WAV recordings, captures, and future explicit transfer packages. SD absence is nonfatal to internal configuration, radio/sensor apps that do not require it, and Audio Spectrum. It prevents SD-backed operations such as Recorder file creation/playback, SD logging, and breadcrumb recording. Saving device/app configuration never depends on SD availability.

The SD service owns mounting for the lifetime of the running firmware and latches physical media loss unavailable rather than trying to recover a removed card in place. Raw card-status and filesystem arbitration are implementation details of the storage service; ordinary consumers do not expose or manage that lock.

Recorder uses `/sdcard/audio/` for 16-bit mono PCM WAV files. Event logs remain under `/sdcard/logs/`. When the GPS service has a fresh valid position at event creation, the log writer snapshots latitude/longitude into that queued event record; otherwise location fields are omitted.

Breadcrumb recording is a separate, opt-in location-history feature and does not depend on the **SD logging** toggle. It requires both a fresh valid GPS position and fresh GPS UTC; system/RTC/manual time is never substituted. Accepted fixes are sampled at most once every five seconds and written as line-oriented CSV under `/sdcard/logs/breadcrumbs/` with columns `timestamp_utc,latitude_deg,longitude_deg`.

Breadcrumb filenames are derived only from GPS UTC as `YYYYMMDDHHM.csv`, where the final minute digit is the ten-minute bucket (`0` through `5`). Each record therefore selects its destination directly from its GPS timestamp; there is no open-file rotation state. The writer appends one record, flushes, fsyncs, and closes the file on every write. If an interrupted write leaves a non-newline-terminated tail, the next append isolates that partial row before writing a complete new row.

Breadcrumb retention is configured by file count rather than days. The compiled default is **144 files**, approximately 24 hours at six ten-minute buckets per hour. Device Settings offers 24, 72, 144, 288, and 1008-file limits. The breadcrumb directory is pruned at service startup, when a new bucket file is created, and after lowering the limit. Because filenames are GPS-UTC chronological, retention can delete the lexicographically oldest recognized breadcrumb filename without depending on filesystem timestamps.

## Time, GPS synchronization, and timezones

The ESP32 RTC-backed system wall clock is authoritative for displayed time, event-log timestamps, and filesystem calendar metadata. RFScout does not serialize the current epoch into ordinary configuration. Breadcrumbs are the deliberate exception: both their CSV timestamp and bucket filename come directly from fresh GPS UTC so manual or retained system time cannot create false track chronology.

`device_time` exposes explicit clock validity and source (`invalid`, retained RTC value, manual, or GPS) plus an **Auto / Manual** synchronization policy. The policy is stored separately in the bounded `system/time.policy` record.

In **Manual** mode, the Home Time submenu exposes local Year/Month/Day/Hour/Minute controls. Committing a manual time validates the requested epoch, persists Manual policy, and only then updates the running system clock. A persistence failure therefore leaves the existing runtime mode/clock unchanged.

In **Auto** mode, manual date/time fields are hidden. A checksum-valid, fix-valid GPS UTC timestamp may update the system clock. The GPS service supplies UTC only; `device_time` continues to own timezone conversion. The current clock remains valid after a successful synchronization even if the live GPS fix later becomes stale; consumers needing current GPS validity must query the GPS service separately.

Timezone selection and 12/24-hour presentation remain ordinary persistent configuration. The available timezone names, labels, and POSIX rules are compiled firmware metadata, so firmware updates can correct those definitions without stale stored copies overriding them. Monotonic time remains authoritative for debounce, dwell, retries, housekeeping, and intervals.

## Deep sleep and session state

Deep sleep loses ordinary RAM, so profiles that support it keep a small RTC-backed session envelope separate from persistent configuration. It currently preserves active-app/input-lock session state; menus, edit state, queued sounds, and active Recorder operations are not reconstructed. Because active recording/finalization vetoes terminal sleep, RFScout never relies on reconstructing an interrupted recording after wake.

T-Deck currently advertises no RFScout deep-sleep or physical-power-off capability. Its center/BOOT GPIO is an input provider control, not a semantic power button.

## Erase and update semantics

A full-device erase destroys system and app internal records, including device/app configuration, time policy, touch calibration, breadcrumb settings, and other internal history records. It does not erase removable SD contents, including breadcrumb CSV files.

Ordinary firmware updates with an unchanged partition layout preserve internal files physically, but RFScout intentionally makes no promise that an older internal record layout will remain semantically compatible. An unsupported record simply falls back to defaults. Partition-layout migrations may require a destructive installation and must make that explicit to the user.
