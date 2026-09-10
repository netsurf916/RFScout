# Architecture

This is the canonical production architecture reference for RFScout. It describes implemented boundaries and invariants on the current development line; future design and release planning belong in [ROADMAP.md](ROADMAP.md).

## Board, app, and service boundaries

RFScout builds one ESP32-S3 firmware image for the supported **LILYGO T-Embed CC1101 Plus** and **LILYGO T-Deck Plus**. Physical hardware is selected at runtime by one minimal early fingerprint before ordinary board GPIO/peripheral initialization. The selected immutable profile lives in `services/board`; application code consumes semantic services/capabilities rather than board names. See [BOARD_PORTING.md](BOARD_PORTING.md).

Source ownership follows the same rule:

```text
main/
  apps/<app>/           app descriptor, controller, views, analysis, app-owned state
  services/board/       runtime profile selection and shared board buses
  services/<service>/   reusable hardware/platform capability implementation
  app_*.c               core app discovery/navigation/lifecycle/session framework
```

Unknown hardware is a deliberate recovery state. If no supported profile is detected, RFScout does not initialize normal board GPIO, display, SD, radio, input, audio, location, or power providers and remains awake in a simple FreeRTOS delay loop for recovery flashing.

A runtime profile owns physical facts and advertises a capability only when RFScout has a provider it is prepared to initialize safely on that profile. Known reference pins/addresses may remain metadata without making a capability available. Board-profile detection is distinct from ordinary service probing: detection selects which hardware model is legal; service probes determine whether expected hardware is actually responding. Operational provider open is a third boundary and validates that present/advertised hardware can actually run without changing the immutable board identity.

Each compiled-in application exports one `rfscout_app_descriptor_t`. The descriptor owns identity/name, a stable persistence key, availability probing, delayed activation, service dependencies, and runtime handling. `app_framework` validates persistence identities as nonempty and unique, filters descriptors against active-profile service capability before probing, and retains probed capability state as unknown, available, or unavailable. Probe error detail remains diagnostic logging rather than application state.

`app_manager` owns only cross-application policy: active-app selection, availability filtering, settle/activation timing, global navigation, and transactional lifecycle coordination. App modules own their menu/subview state, rendering decisions, app-local input interpretation, normal polling, and dependency-loss behavior. Hardware-specific stop/drain/recovery remains behind platform services and resource sessions.

Canonical registry order is System Events -> Home / Device Settings -> WiFi / BLE -> CC1101 -> nRF24 -> Recorder -> Thermal -> Audio Spectrum. Disabled or unavailable apps are skipped. T-Deck global tabs are another presentation of this same availability-filtered application set; they do not introduce a second app-switching mechanism.

### Runtime dispatch contract

The manager dispatches a deliberately small set of events to the active descriptor:

- **ENTER** — initialize app-local UI/runtime state after a transition commits and receive the managed app view;
- **ACTIVATE** — delivered after the common settle delay for apps that request delayed activation;
- **INPUT** — one board-neutral semantic input event;
- **POLL** — periodic app work/render opportunity; and
- **LEAVE** — committed app-local cleanup after platform services have safely quiesced.

An app reports whether INPUT was consumed. Unconsumed navigation remains global next/previous-app switching; unconsumed Back returns to Home. This keeps global navigation policy centralized without teaching the manager how an individual app's menu works. Rapid navigation through a delayed app can therefore leave it before activation, preserving the settle behavior that avoids starting hardware merely because the user navigates past an app.

Normal apps need only their module/descriptor, build/registry entry, and whatever semantic services they consume. Adding a normal app should not require modifying `app_manager.c`, `app_session.c`, or driver-specific teardown logic. Shared controls such as per-app notification editing are reusable helpers invoked by app modules rather than state embedded in the manager.

Optional app-specific prepare hooks are reserved for semantic transition vetoes that the platform cannot infer. Recorder uses this while recording or finalizing a recording: navigation is blocked until the recording has been stopped and saved. Terminal T-Embed sleep/power-off preparation uses the same semantic rule rather than stopping a recording and attempting to reconstruct it after a later failure. Audio Spectrum needs no special transition hooks and follows the normal descriptor/session contract.

## Application lifecycle, views, and resource sessions

Every active app has a temporary **resource session**, including apps that currently allocate no dynamic PSRAM. Apps should not manage allocator owner IDs, callback counters, or driver-specific reachability protocols.

The same session owns the app's managed `rfscout_view_t`. The current canonical app view is 320x170 on both supported profiles. On T-Embed it is the full physical display; on T-Deck it is the upper 320x170 portion of the 320x240 panel, leaving the lower 320x70 region to system-owned navigation/touch chrome. The view carries geometry plus standardized `init`/`deinit` traits. The session initializes it before ENTER and sanitizes it only after a committed LEAVE.

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

## Semantic input, touch, and UI behavior

The `input` service translates discrete board controls into focused semantic input before app handling. T-Embed uses the rotary/button provider; T-Deck normally uses trackball/center as its physical Select/navigation source. Physical activity used to wake the display is a separate path and the complete wake gesture is consumed before app, lock, or power semantics. Boards without an RFScout user/power key do not enter power-key sampling during that wake path.

GT911 touch is a separate board-neutral touch service. On T-Deck, hardware-validated raw/native coordinates are transformed into landscape reference coordinates and then normalized by a persisted calibration record. Touch is not authoritative without compatible calibration. Calibrated finger-downs inside the system-owned tab strip are consumed by global chrome and invoke the same transactional app-selection path as semantic navigation. Ordinary calibrated app-view touch is ignored. Only while a peripheral has temporarily suspended the physical primary Select source does an app-view finger-down translate into the existing semantic Select event; #79 uses this narrowly while ES7210 capture owns the GPIO0 conflict. Full application pointer/keyboard semantics remain a separate abstraction.

Touch calibration is system/board state rather than app config or SD content. A missing/incompatible bounded system record enters startup calibration. Explicit recalibration atomically replaces the same record after the captured geometry passes sanity checks.

On the rotary/button provider, edge capture survives temporary main-loop/display stalls. Holds of roughly 0.8–1.5 seconds produce `LONG_SELECT`, 1.5–2 seconds produce `BACK`, and two seconds toggles the global input lock; the release that completes a lock toggle is consumed. T-Deck trackball/center maps into the same semantic navigation/select contract when its physical Select source is available. The device-wide input lock is enforced before touch-derived semantic Select is admitted.

Common settings use one interaction model:

- **Action** — center executes immediately.
- **Toggle** — center changes On/Off immediately.
- **Choice** — center cycles a small finite set when unambiguous.
- **Value** — center enters edit, navigation adjusts, center exits.
- **Submenu** — center enters a child view.

Application settings use the relative order: Back; alerts/notifications; acquisition/detection; display/presentation; storage/logging/recording; advanced/hardware-specific; destructive/reset actions. Empty placeholder categories are not added. Full-screen diagnostic lists may explicitly claim navigation after a visible mode change (System Events Browse is the current example).

## Audio and feedback architecture

RFScout separates semantic meaning from presentation. Four audio classes are used:

| Class | Meaning | Level owner |
| --- | --- | --- |
| UI / device | navigation, accepted actions, lock/device feedback | global UI volume |
| Event | lightweight informational activity/detection cue | global Event volume |
| Alert | warning/error and configurable attention notification | global Alert volume |
| Content | explicitly requested app audio, e.g. Recorder playback | app-owned |

`app_audio_policy` describes which classes make sense for an app. `audio_policy` independently applies device-wide **Normal** / **Quiet** presentation. `feedback` combines semantic events with app/device policy; `notification` owns detection vocabulary/preferences; `alert` applies notification mode/mappings; the audio-output service owns speaker arbitration.

Normal permits all classes subject to app/user policy. Quiet suppresses UI, Event, and Alert presentation without rewriting saved volumes, app gates, alert enablement, or Morse mappings. Explicit Content remains available where a provider exists. Disposable feedback/alerts are dropped rather than queued behind active Content audio.

Speaker output and audio capture are separate profile capabilities. T-Embed advertises its validated I2S speaker and PDM capture paths. T-Deck advertises its hardware-validated standard-I2S speaker output independently of its ES7210 audio-capture provider. Recorder and Audio Spectrum therefore remain board-neutral consumers of the audio-capture service while UI/event/alert and playback audio use the separate speaker provider.

Visual themes are independent: Indoor, Outdoor, and Night alter presentation colors only.

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

CC1101 uses a worker for receive-only RSSI sweeps and conservative behavior classification. nRF24 uses receive-only RPD sweeps plus a separate ingestion/storage path for validated Enhanced ShockBurst observations. Both workers are session services and must quiesce before app memory reclamation. Their providers are currently advertised by the T-Embed profile and withheld on T-Deck.

nRF24's RPD bit is threshold evidence (roughly whether received power exceeded the device threshold), not RSSI and not packet proof. The production scanner currently generates RPD energy observations only; packet browser/detail surfaces consume only separately CRC-validated observations. RFScout does not generate ACKs, inject, replay, pair, probe, or actively discover nRF24 devices. Future receive-only ESB discovery must keep acquisition/validation separate from presentation and preserve that passive boundary; detailed implementation planning remains in the roadmap.

RF acquisition and LCD presentation run at different cadences. nRF24 sweep work occurs in its worker while completed display snapshots are coalesced; input edge capture is designed to survive temporary synchronous LCD/main-loop stalls.

## Recorder and Audio Spectrum

`audio_capture` is the board-neutral capture boundary. It owns provider selection, profile-legality checks, provider probing, operational open/read/close, and capture-transport lifetime. A successful open returns normalized mono PCM through `audio_capture_read()`; no application or Recorder code receives the provider's I2S handle. The active provider is the only layer permitted to configure, enable, read, or close its capture transport.

T-Embed retains its established I2S0/PDM provider behind this service, including PDM clock/data probing and effective-rate measurement. T-Deck uses an ES7210 provider: the shared board I2C bus and Espressif codec-control interface own only ES7210 register/control operations, while RFScout's provider exclusively owns I2S0/DMA. The ES7210 runs in four-slot Philips-format TDM slave mode with all four ADC slots enabled so frame alignment remains stable; the ESP32-S3 supplies MCLK/BCLK/LRCK and the provider compacts MIC1/slot 0 into normalized mono 16-bit PCM. ES7210 presence probing is a lightweight I2C check and does not start I2S/ADC or claim GPIO0.

Recorder remains above this transport boundary. It owns the live FFT monitor worker, SD recording machinery, WAV finalization, playback integration, and the common low-frequency filter primitive. This keeps DMA/provider lifecycle independent from recording/spectrum policy while preserving the established pull model: ESP-IDF owns DMA buffers, and Recorder/Spectrum own only their destination/processing buffers.

**Recorder** is the storage-backed app. It provides recording, asynchronous save/post-processing, recording catalog/delete, and playback through the shared speaker service. Capture is written without software pre-amplification. The provider-reported nominal sample rate is used when capture opens, and final WAV metadata may be corrected from measured delivered PCM throughput when a valid effective rate is available. Finalization then trims startup/tail artifacts, removes DC/very-low-frequency energy through the shared one-pole high-pass filter, and applies bounded normalization before the file is finalized. Recorder playback volume remains app-owned durable state. Recorder files are independent of runtime app-memory reclamation. Active recording/saving blocks app transitions and terminal T-Embed sleep/power-off preparation until the operation is safely complete; playback may be stopped during normal app exit or terminal power preparation. While T-Deck ES7210 capture is active, its physical GPIO0 Select source is suspended before codec/I2S activation and restored when the provider closes; calibrated app-view touch supplies the same semantic Select event only for that conflict interval so Recorder can still be stopped without acquiring touch-specific behavior.

**Audio Spectrum** is a separate microphone-only app. It can run without SD storage, starts monitoring after the common app settle point, renders the live FFT spectrum, and uses raw microphone PCM through the same continuous one-pole high-pass/DC-block primitive before a smoothed automatic dB display range. The live `AUTO N dB` value is the current display-window span rather than input gain, with an 18 dB minimum span. The monitor measures delivered PCM throughput over one-second windows and maps displayed FFT bins using that effective rate once available, using the provider-reported nominal rate only as the startup fallback. Semantic navigation remains available for global app switching. Audio Spectrum has no Recorder-specific semantic rollback path.

T-Deck ES7210 capture is an enabled, hardware-validated provider behind this shared service boundary. Hardware testing has confirmed sustained Audio Spectrum and Recorder capture, MIC1/slot-0 compaction, recording finalization/playback, and the GPIO0-to-touch Select handoff; T-Embed PDM Recorder/Spectrum regression testing also passed on the same universal image. T-Deck speaker output remains independently available to semantic feedback/notification and Recorder playback consumers. Further calibration/generalization of the shared Audio Input service remains roadmap work.

## Thermal

MLX90640 is an optional hot-plug I2C/Qwiic peripheral exposed by the thermal service. Thermal acquisition is synchronous while active. Native sensor resolution is 32x24; display interpolation must not imply additional sensor resolution. Literal image pixels bypass theme translation while labels/chrome remain theme-aware.

## GPS, wall clock, and timezone

The T-Deck GPS service owns UART/NMEA parsing and exposes a snapshot only after a checksum-valid supported RMC/GGA sentence has proved the receiver is present. Location and GPS UTC time have independent freshness/validity flags; consumers must not infer one from the other.

`device_time` owns RTC-backed wall-clock validity/source, Auto/Manual synchronization policy, and timezone conversion. In Auto mode, a fix-valid GPS UTC timestamp may update the wall clock. In Manual mode GPS clock offers are ignored. Once the wall clock has been synchronized it remains a valid clock even if the live GPS fix later expires; consumers needing current location/time-fix state query GPS separately.

Timezone is durable user presentation policy and remains independent of GPS, which supplies UTC. The synchronization mode is a small system-owned persistent record rather than app configuration. Policy changes are persisted before the new in-memory mode is published; setting a manual clock therefore cannot return a persistence failure after silently changing the reboot policy.

## Power, battery, display idle, and sleep

Power actions are semantic and runtime-board-capability aware. T-Embed retains its short/long user-key behavior plus screen-off, deep-sleep, and physical-power-off capabilities. T-Deck exposes no RFScout user/power key and currently advertises no semantic deep-sleep or physical-power-off capability; GPIO0 normally belongs to trackball-center/BOOT input and is temporarily unavailable as a physical Select source while ES7210 capture is active.

The display service owns framebuffer presentation plus runtime physical provider metadata. Display dim/off is presentation policy, **not** application lifecycle or ESP32 sleep. The ST7789 provider disables output and enters controller low power while apps/acquisition/logging/audio continue; wake restores the provider, presents the latest framebuffer while dark, then restores brightness. T-Deck wake handling does not sample an absent power key.

Battery telemetry is provider-specific. T-Embed uses its fuel gauge/charger and can establish authoritative external-power state. T-Deck uses filtered ADC voltage plus a profile-owned divider to estimate LiPo state of charge and deliberately reports charging/external-power state as unavailable.

Protective low-battery action requires fresh percentage plus authoritative evidence that external power is absent. It therefore remains effective on the T-Embed fuel-gauge/charger path and is intentionally disabled for the current voltage-only T-Deck provider. High-temperature action requires five consecutive samples and rearms 5 C below threshold; requested actions remain constrained by active-profile power capability. Manual/protective transitions share the global transition overlay and semantic execution path.

Deep sleep, where supported, stores only a small RTC-backed session envelope (currently active app and input-lock state). Menus, edit state, queued sounds, and active Recorder operations are not resumed. Dirty authoritative configuration is flushed before sleep. Wall-clock epoch/source state is not checkpointed into ordinary configuration; the RTC-backed system clock remains clock authority.

Terminal sleep/power-off preparation quiesces removable-storage users but does not unmount the FAT volume. Active recording/finalization vetoes the transition before Recorder is altered. The logger drains and closes its durable batch; playback is stop-safe. A successful terminal transition then discards the running firmware's VFS state naturally. If physical power-off fails, the existing mount remains authoritative and logging can restart without a remount/rollback state machine.

## Storage, persistence, and logging

RFScout separates device-owned durable state from removable bulk/interchange storage.

The internal `storage` LittleFS partition is owned exclusively by the persistent-record service. Core/system callers use logical `system/*` records while applications are confined to logical namespaces derived from their compiled descriptor `persistence_key`. Logical names are not filesystem paths: the service SHA-256 hashes logical keys into fixed filenames and hashes the app persistence identity into a private app-scope directory. Callers therefore do not sanitize keys or construct LittleFS paths.

The service internally serializes filesystem operations behind one mutex. Each opaque record is limited to 16 KiB and a write atomically replaces that individual record via same-directory temporary, flush/fsync, close, and rename. Atomicity does not span multiple records. Interrupted-write temporaries are removed by one startup cleanup pass rather than a background housekeeping task. Reads, existence checks, and removals do not materialize missing app directories; only writes create an app scope.

Normal app-facing calls derive ownership from the current app session. Asynchronous work may capture the existing app/session generation and stale contexts are rejected after transition. A long-running asynchronous operation that must prevent session teardown uses the normal app prepare/veto lifecycle rather than storage-specific transition machinery. Core orchestration has an explicitly privileged interface for operations such as loading/saving all app configuration records.

Ordinary device-wide user policy lives in logical `system/config`; app enablement/startup remains system policy. App-owned `config` records are sparse overrides: absence means compiled defaults, only changed app values are serialized, and returning all app settings to defaults removes that record. Configuration recovery favors sane compiled defaults rather than maintaining a journal or cross-record transaction layer. Ordinary setting changes are automatically flushed rather than requiring a Home “Save config” action.

Touch calibration and Auto/Manual time policy are bounded system records separate from ordinary configuration. Runtime wall-clock epoch/source state remains outside persistent configuration; timezone and clock-presentation preferences remain durable.

SD remains the removable/bulk-storage service rather than a configuration authority. Saving device/app configuration does not depend on SD presence. Recorder WAV files remain under `/sdcard/audio/`, and rotating logs remain SD-only. The SD service owns the mount for the running firmware lifetime and latches physical-media loss unavailable rather than attempting automatic unmount/remount recovery. Raw `sdmmc_get_status()` arbitration is private to the storage service; ordinary consumers never acquire/release that synchronization primitive. When a fresh GPS location exists at event creation, the log writer snapshots latitude/longitude into that immutable queued record; otherwise location is omitted. A future settings-transfer feature, if added, should be an explicit firmware-managed import/export operation rather than direct record-file editing.

The persistent-record abstraction is size-independent; consumers remain bounded and must not infer partition capacity from a particular image layout.

Internal-storage mount, recovery-format/mount-failure, and configuration-default recovery are recorded in the volatile System Events/dmesg history as distinct semantic events. GPS fix transitions are diagnostic console events and are also written through normal SD logging when logging is enabled. Startup discovery is retained without unrelated interactive notification.

The conservative hardware-tested SD baseline remains a FAT-compatible SanDisk card of 32 GB or less on the T-Embed. T-Deck SD/shared-SPI behavior remains a hardware-validation item. Logging rotates `rfscout-00.log` through `rfscout-99.log` at up to 4 MiB each and attempts to preserve at least 64 MiB free.

## Shared hardware/resource boundaries

T-Embed:

- LCD, microSD, CC1101, and nRF24 share SPI resources;
- battery/power devices and external sensors share board I2C;
- PDM capture owns I2S0 through the audio-capture provider;
- the asynchronous speaker service uses I2S1.

T-Deck:

- LCD, microSD, and the currently disabled SX1262 share SPI resources;
- GT911, ES7210 control, and other compatible board devices share runtime-profile I2C;
- GPS uses the profile UART;
- battery telemetry uses the ESP32-S3 ADC;
- standard-I2S speaker output is enabled and hardware-validated;
- ES7210 capture exclusively owns I2S0 RX/DMA in four-slot Philips-format TDM mode while active, compacts MIC1/slot 0 to mono PCM, and temporarily conflicts with the physical GPIO0 Select source;
- reference keyboard and SX1262 metadata does not imply an enabled provider.

Application views render only their assigned app content; the display/presentation layer owns physical-panel composition, common top chrome, T-Deck system tabs, and final global overlays. The RAM-only System Events ring is a platform service and its optional app is a read-only browse view.

## Validation invariants

Release testing should rapidly rotate every enabled app and exercise low memory, callback races, worker-stop timeouts, dependency loss, Recorder recording/save transition vetoes, Audio Spectrum entry/exit, and failed-close rollback. Successful app exit must leave zero managed allocations for the exited app; an aborted transition must preserve its memory/state and managed presentation. Baseline free PSRAM and largest-free-block behavior should not decline monotonically across repeated cycles. A post-commit destination-session failure must never dispatch ENTER without a valid view and should recover to Home when the shared presentation service remains available.

Universal-image validation must additionally prove the same image selects both known profiles correctly, leaves unknown hardware inert, never enters a provider illegal for the selected profile, and preserves the hardware-validated T-Embed behavior. T-Deck validation includes display/app-view/chrome separation, touch calibration/persistence/tabs and conflict-only semantic app-view Select, trackball behavior, battery ADC plausibility, GPS receiver/fix/time behavior, SD/shared-SPI operation, supported speaker output, ES7210 I2C presence probing, four-slot TDM MIC1/slot-0 mapping and sustained Recorder/Audio Spectrum capture, GPIO0 Select suspension/restoration, and absence of unintended sleep/power-off behavior.

Persistence validation should additionally cover boot without SD, missing/app-default records, sparse app overrides and return-to-default record removal, removal/default recovery of retired monolithic configuration, internal corruption/default replacement, oversized-record rejection, concurrent store calls, arbitrary logical key text, interrupted temporary cleanup on reboot, reads of absent app records without directory creation, touch-calibration compatibility/replacement, time-policy persistence, and repeated automatic saves without accumulating authoritative sidecar/history files. RTC testing should verify Manual/Auto behavior, GPS synchronization, retained clock validity, and durable timezone/12–24-hour preferences.

Build/release workflow is documented in [RELEASING.md](RELEASING.md); board requirements in [BOARD_PORTING.md](BOARD_PORTING.md); user-visible behavior in [CAPABILITIES.md](CAPABILITIES.md).

## Surveillance classification

Surveillance classification is evidence-driven rather than confidence-accumulating. The app-level High / Medium / Low sensitivity setting chooses the minimum evidence strength required for a single observation to establish a signature; High is the default. Repeated observations refresh active presence; they do not raise a numeric confidence score, and an active identity retains its first qualifying surveillance classification until expiry. The canonical observation store owns alert re-arm through ordinary device expiry, and the UI derives both general and surveillance projections from that same active collection rather than retaining a second surveillance list.
