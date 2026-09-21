# Architecture

This is the canonical production architecture reference for RFScout. It describes implemented boundaries and invariants on the current development line; future design and release planning belong in [ROADMAP.md](ROADMAP.md).

## Board, app, and service boundaries

RFScout currently supports one runtime hardware profile, **LILYGO T-Embed CC1101 Plus**, while preserving the profile/capability abstraction for future boards. Early passive detection selects the immutable profile before normal GPIO or peripheral initialization. Unknown hardware is a deliberate recovery state: RFScout leaves ordinary board hardware untouched and remains awake for recovery flashing.

A runtime profile owns physical facts and advertises a capability only when RFScout has a provider it is prepared to initialize safely. Board-profile detection establishes what hardware is legal; service probing establishes whether an expected device is present; operational open establishes whether the provider can actually run.

Applications depend on semantic services, application descriptors, and managed views rather than board identity.

### Runtime dispatch contract

Runtime dispatch stays descriptor-driven. Application availability is derived from configured enablement plus the semantic services legal and available on the selected profile. Board-specific branching belongs in board/provider integration code, not applications.

## Application lifecycle, views, and resource sessions

Every active app has a temporary **resource session**, including apps that currently allocate no dynamic PSRAM. Apps should not manage allocator owner IDs, callback counters, or driver-specific reachability protocols.

The same session owns the app's managed `rfscout_view_t`. The current canonical app view is 320x170 and maps to the full T-Embed display. The view carries geometry plus standardized `init`/`deinit` traits, keeping application presentation independent from physical board identity.

The application-facing lifecycle remains intentionally small:

1. **Enter** — establish inexpensive app-local state against the assigned app view.
2. **Settle** — optionally defer activation so rapid navigation stays responsive.
3. **Activate** — begin normal app/service work after the common settle point.
4. **Close** — optionally veto an unsafe semantic transition before platform teardown; otherwise platform teardown, view cleanup, and memory reclamation remain framework-owned.

Growable retained app state uses `app_session_calloc()`, `app_session_resize()`, and `app_session_free()`. The lower-level memory manager supplies ownership/accounting/bulk reclamation and is not normally an app API.

A transition is transactional:

1. ensure the source resource session/view exists;
2. allow an optional app-semantic prepare/veto before service teardown begins;
3. quiesce every platform service declared by the source descriptor;
4. if any service cannot quiesce safely, restore service state where the service supports recovery and do **not** reclaim memory or destroy the source presentation;
5. after successful service close, dispatch committed app-local LEAVE cleanup;
6. release/sanitize the source view, bulk-reclaim source-app managed memory, and verify no managed allocation remains; and
7. establish the destination session/view before ENTER can use app resources.

A prepare veto happens before session teardown, so it does not trigger service rollback. A successful service stop means no new work that can reference app state can enter, admitted callbacks/workers have drained, and shared hardware is safely inactive. Successful session close is therefore the reachability proof; apps do not implement a second reachability phase.

Once source LEAVE/view reclamation has committed, RFScout does not attempt to resurrect the source. If destination-session establishment fails after that boundary, lifecycle diagnostics report a distinct committed-destination-failure outcome and `app_manager` attempts the existing Home fallback; it never dispatches ENTER to a destination without a valid session/view.

On an aborted service close, the session restores only platform service state that was active and successfully stopped when that service exposes a safe restart operation. Failed-stop recovery is service-specific so a self-restoring worker is not started twice. There is no app-level reactivation hook: reversible platform restoration belongs exclusively to `app_session` and the service implementation, while app modules may only veto before teardown through `prepare_leave` when semantic state cannot safely be interrupted.

Current session-managed asynchronous services are Wi-Fi monitoring, BLE scanning, CC1101, nRF24, and shared audio capture/Recorder activity. Thermal acquisition is synchronous today; any future worker/DMA/callback path belongs behind a service rather than a new app-specific lifecycle protocol.

### Memory policy

App-session PSRAM is intended for retained observations, histories, captures, and other meaningfully sized growable state. Task stacks, DMA buffers, driver-required internal allocations, and latency-sensitive objects continue to use the memory class required by ESP-IDF/hardware.

There is deliberately no arbitrary per-app item quota. Collections grow according to resources and retention semantics; presentation work should be bounded separately. Under pressure, prefer current/correct data, release reconstructable scratch, evict stale/low-value history where semantics allow, surface pressure, and continue safely.

Usable external PSRAM is a supported-platform requirement. RFScout does not silently substitute internal SRAM with a hidden smaller item ceiling.

## Semantic input and UI behavior

Applications consume semantic input rather than physical GPIOs. The current T-Embed rotary/button provider maps encoder motion into signed navigation and the center button into Select/long-Select/Back behavior. The dedicated user/power key remains a separate power-policy input.

Edge capture survives temporary main-loop/display stalls. Holds of roughly 0.8-1.5 seconds produce `LONG_SELECT`, 1.5-2 seconds produce `BACK`, and two seconds toggles the device-wide input lock; the release that completes a lock toggle is consumed.

Input used only to wake a dimmed/off display is consumed before ordinary navigation or power handling. Application code therefore receives stable semantic events without depending on board wiring.

## Audio and feedback architecture

Audio capture is a board-neutral service. The current T-Embed profile advertises the PDM microphone provider on I2S0; the capture service owns probing, open/read/close, DMA/I2S lifetime, and measured PDM-clock diagnostics. Recorder and Audio Spectrum consume normalized PCM rather than raw peripheral handles.

Speaker output remains a separate semantic service on the validated T-Embed I2S1 path. UI/event/alert policy is independent from explicitly requested content playback.

## WiFi / BLE data path

```text
Wi-Fi monitor ---\
                  +--> normalized observation --> prefix identity --> surveillance classifier
BLE monitor -----/              |                        |
                                v                        v
                         recent device store       retained detections
                                |                        |
                                +-----------> WiFi / BLE UI
```

The normalized observation collection is app-session PSRAM and grows according to available resources rather than a fixed item ceiling. It uses a session-owned identity index so collector ingestion does not linearly rescan the collection for every callback. Expiration runs through housekeeping; Wi-Fi and BLE membership age independently.

Current production retention is independently configurable for Wi-Fi and BLE from 10–300 seconds in 10-second steps, default 60 seconds. Planned `Instant` / `Never` semantics and richer entity correlation remain roadmap work.

Presentation derives summaries from live state while retaining only displayable rows/scratch appropriate to the managed app view. Prefix identity tables and surveillance matching are documented in [PREFIX_IDENTITY.md](PREFIX_IDENTITY.md).

## CC1101 and nRF24

CC1101 uses a worker for receive-only RSSI sweeps and conservative behavior classification. nRF24 uses receive-only RPD sweeps plus a separate ingestion/storage path for validated Enhanced ShockBurst observations. Both workers are session services and must quiesce before app memory reclamation. Both providers are advertised by the current T-Embed profile.

## Recorder and Audio Spectrum

Recorder and Audio Spectrum consume the shared board-neutral audio-capture service. The current T-Embed PDM provider owns I2S0 capture setup, DMA lifetime, clock/data probing, and effective-rate measurement.

Recorder is the storage-backed app: it records mono PCM, performs asynchronous save/post-processing, manages the recording catalog, and plays WAV files through the speaker service. Active recording/saving blocks app transitions and terminal sleep/power-off preparation until the operation completes safely.

Audio Spectrum is microphone-only and does not depend on SD. It applies its own analysis/filtering path to live PCM without changing Recorder capture data.

## Thermal

MLX90640 is an optional hot-plug I2C/Qwiic peripheral exposed by the thermal service. Thermal acquisition is synchronous while active. Native sensor resolution is 32x24; display interpolation must not imply additional sensor resolution. Literal image pixels bypass theme translation while labels/chrome remain theme-aware.

## GPS, wall clock, and timezone

The GPS/location service remains capability-gated for future profiles or hardware integrations; the current T-Embed profile does not advertise an onboard GPS provider.

`device_time` owns RTC-backed wall-clock validity/source, Auto/Manual policy, and timezone conversion independently from any location provider.

## Power, battery, display idle, and sleep

Power actions are semantic and runtime-profile-capability aware. T-Embed retains its validated user-key behavior plus screen-off, deep-sleep, and physical-power-off capabilities.

The display service owns framebuffer presentation plus runtime physical-provider metadata. Display dim/off is presentation policy, **not** application lifecycle or ESP32 sleep. The ST7789 provider disables output and enters controller low power while apps/acquisition/logging/audio continue; wake restores the provider, presents the latest framebuffer while dark, then restores brightness. Periodic one-pixel framebuffer presentation shifts reduce long-lived static-image retention.

Battery telemetry uses the T-Embed BQ27220 fuel gauge and BQ25896 charger and can establish authoritative external-power state. Protective low-battery action requires fresh percentage plus authoritative evidence that external power is absent. High-temperature action remains constrained by active-profile power capability.

Deep sleep stores only a small RTC-backed session envelope (currently active app and input-lock state). Dirty authoritative configuration is flushed before sleep; active Recorder recording/finalization vetoes terminal transitions.

## Storage, persistence, and logging

RFScout separates device-owned durable state from removable bulk/interchange storage.

The internal `storage` LittleFS partition is owned exclusively by the persistent-record service. Core/system callers use logical `system/*` records while applications are confined to logical namespaces derived from their compiled descriptor `persistence_key`. Logical names are not filesystem paths: the service SHA-256 hashes logical keys into fixed filenames and hashes the app persistence identity into a private app-scope directory. Callers therefore do not sanitize keys or construct LittleFS paths.

The service internally serializes filesystem operations behind one mutex. Each opaque record is limited to 16 KiB and a write atomically replaces that individual record via same-directory temporary, flush/fsync, close, and rename. Atomicity does not span multiple records. Interrupted-write temporaries are removed by one startup cleanup pass rather than a background housekeeping task. Reads, existence checks, and removals do not materialize missing app directories; only writes create an app scope.

Normal app-facing calls derive ownership from the current app session. Asynchronous work may capture the existing app/session generation and stale contexts are rejected after transition. A long-running asynchronous operation that must prevent session teardown uses the normal app prepare/veto lifecycle rather than storage-specific transition machinery. Core orchestration has an explicitly privileged interface for operations such as loading/saving all app configuration records.

Ordinary device-wide user policy lives in logical `system/config`; app enablement/startup remains system policy. App-owned `config` records are sparse overrides: absence means compiled defaults, only changed app values are serialized, and returning all app settings to defaults removes that record. Configuration recovery favors sane compiled defaults rather than maintaining a journal or cross-record transaction layer. Ordinary setting changes are automatically flushed rather than requiring a Home “Save config” action.

Auto/Manual time policy is a bounded system record separate from ordinary configuration. Runtime wall-clock epoch/source state remains outside persistent configuration; timezone and clock-presentation preferences remain durable.

SD remains the removable/bulk-storage service rather than a configuration authority. Saving device/app configuration does not depend on SD presence. Recorder WAV files remain under `/sdcard/audio/`, and rotating logs remain SD-only. The SD service owns the mount for the running firmware lifetime and latches physical-media loss unavailable rather than attempting automatic unmount/remount recovery. Raw `sdmmc_get_status()` arbitration is private to the storage service; ordinary consumers never acquire/release that synchronization primitive. When a fresh GPS location exists at event creation, the log writer snapshots latitude/longitude into that immutable queued record; otherwise location is omitted. A future settings-transfer feature, if added, should be an explicit firmware-managed import/export operation rather than direct record-file editing.

The persistent-record abstraction is size-independent; consumers remain bounded and must not infer partition capacity from a particular image layout.

Internal-storage mount, recovery-format/mount-failure, and configuration-default recovery are recorded in the volatile System Events/dmesg history as distinct semantic events. GPS fix transitions are diagnostic console events and are also written through normal SD logging when logging is enabled. Startup discovery is retained without unrelated interactive notification.

The conservative hardware-tested SD baseline remains a FAT-compatible SanDisk card of 32 GB or less on the T-Embed. Logging rotates `rfscout-00.log` through `rfscout-99.log` at up to 4 MiB each and attempts to preserve at least 64 MiB free.

## Firmware image and OTA storage foundation

The OTA development line uses the ESP-IDF A/B application layout rather than a single factory application partition. The 16 MiB internal flash map reserves NVS/PHY metadata, the ESP-IDF OTA-data partition, two 6 MiB OTA application slots, and approximately 3.94 MiB of LittleFS. LittleFS consumers remain behind the persistent-record abstraction and must not infer or depend on this physical capacity.

Application rollback support is enabled in the bootloader configuration. OTA-data is bootloader/platform state rather than application-owned persistent configuration.

The combined image contains the bootloader, A/B partition table, initialized OTA metadata, and application and remains the clean-install/recovery image for this development line. The published `v0.3.2` layout predates this foundation.

The OTA installer treats the inactive ESP-IDF OTA partition as the only firmware staging area. HTTPS acquisition supplies byte fragments to a transport-neutral installer service; SD and LittleFS are never used for firmware staging. The installer does not expose partition names, offsets, or ESP-IDF OTA handles to the application.

At startup RFScout computes and logs a SHA-256 over the exact byte range occupied by the running ESP application image in flash. ESP-IDF image metadata determines the stored image length, including the appended simple-hash field when present, and RFScout reads that range back through the partition API before hashing it. The resulting value is therefore directly comparable to the SHA-256 of the packaged `*-app.bin` artifact rather than to ESP-IDF's distinct image-content digest returned by `esp_partition_get_sha256()`.

The remaining explicit build-selection UI and OTA hardware-validation work are maintained in [ROADMAP.md](ROADMAP.md#in-device-ota-update-architecture). A newly installed image remains pending verification until mandatory common startup reaches the application-manager/display-idle checkpoint; only then does RFScout call ESP-IDF's rollback-cancel confirmation. Optional peripherals and removable storage do not gate confirmation.

## Shared hardware/resource boundaries

T-Embed:

- LCD, microSD, CC1101, and nRF24 share SPI resources;
- battery/power devices and compatible external sensors share runtime-profile I2C;
- PDM capture uses I2S0 behind the audio-capture service;
- asynchronous speaker output uses I2S1.

Applications render only their assigned managed view. Common presentation overlays, service ownership, and hardware arbitration remain platform responsibilities.

## Validation invariants

Release testing should rapidly rotate every enabled app and exercise low memory, callback races, worker-stop timeouts, dependency loss, Recorder recording/save transition vetoes, Audio Spectrum entry/exit, and failed-close rollback. Successful app exit must leave zero managed allocations for the exited app; an aborted transition must preserve its memory/state and managed presentation. Baseline free PSRAM and largest-free-block behavior should not decline monotonically across repeated cycles. A post-commit destination-session failure must never dispatch ENTER without a valid view and should recover to Home when the shared presentation service remains available.

Hardware validation must prove the supported T-Embed profile is selected reliably, unknown hardware remains inert, providers never initialize outside profile legality, and the validated T-Embed display/input/RF/audio/storage/power behavior is preserved.

Persistence validation should additionally cover boot without SD, missing/app-default records, sparse app overrides and return-to-default record removal, removal/default recovery of retired monolithic configuration, internal corruption/default replacement, oversized-record rejection, concurrent store calls, arbitrary logical key text, interrupted temporary cleanup on reboot, reads of absent app records without directory creation, time-policy persistence, and repeated automatic saves without accumulating authoritative sidecar/history files. RTC testing should verify Manual/Auto behavior, GPS synchronization, retained clock validity, and durable timezone/12–24-hour preferences.

Build/release workflow is documented in [RELEASING.md](RELEASING.md); board requirements in [BOARD_PORTING.md](BOARD_PORTING.md); user-visible behavior in [CAPABILITIES.md](CAPABILITIES.md).

## Surveillance classification

Surveillance classification is evidence-driven rather than confidence-accumulating. The app-level High / Medium / Low sensitivity setting chooses the minimum evidence strength required for a single observation to establish a signature; High is the default. Repeated observations refresh active presence; they do not raise a numeric confidence score, and an active identity retains its first qualifying surveillance classification until expiry. The canonical observation store owns alert re-arm through ordinary device expiry, and the UI derives both general and surveillance projections from that same active collection rather than retaining a second surveillance list.
