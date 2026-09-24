# RFScout Platform Architecture

This is the canonical platform reference for RFScout's **implemented** shared architecture. The [README](../README.md) is the product/current-state anchor, application behavior lives under [`docs/apps/`](apps/), and future design or release planning belongs only in [ROADMAP.md](ROADMAP.md).

## Documentation and ownership boundary

Platform documentation answers questions about shared contracts: board/profile legality, application lifecycle, resource ownership, memory, persistence, input/presentation, power, storage, audio, networking, and OTA.

Application pages own app-specific UI, interpretation, settings, and current limitations:

- [Home / Device Settings](apps/HOME.md)
- [System Events](apps/SYSTEM_EVENTS.md)
- [WiFi / BLE](apps/WIFI_BLE.md)
- [CC1101](apps/CC1101.md)
- [nRF24](apps/NRF24.md)
- [Recorder](apps/RECORDER.md)
- [Audio Spectrum](apps/AUDIO_SPECTRUM.md)
- [Thermal](apps/THERMAL.md)

## Board, app, and service boundaries

RFScout currently supports one runtime hardware profile, **LILYGO T-Embed CC1101 Plus**, while preserving the profile/capability abstraction for future boards.

A runtime profile owns physical facts and expected capabilities: GPIO assignments, buses, display geometry, peripheral addresses, control pins, and which semantic providers RFScout is prepared to initialize. Applications depend on services/capabilities rather than board names or IDs.

Board detection and provider probing are separate:

1. perform the smallest passive fingerprint needed to recognize a supported profile;
2. select one immutable runtime profile;
3. initialize only buses/pins legal for that profile; and
4. let ordinary provider probes determine whether expected devices actually respond.

The current T-Embed fingerprint uses the early I2C path and onboard BQ25896/BQ27220 presence. The temporary detection bus is deleted before normal shared-I2C initialization.

Unknown or ambiguous hardware is a deliberate recovery state: RFScout leaves ordinary board GPIO/peripherals untouched and remains awake for recovery flashing.

### Current T-Embed profile

The hardware-validated profile owns the current 320x170 ST7789 display, rotary/center input, CC1101, nRF24, PDM microphone, speaker, SD, battery/charger telemetry, internal-temperature protection, display low-power behavior, deep sleep, and physical power-off semantics.

Shared physical resources include:

- LCD, microSD, CC1101, and nRF24 on shared SPI resources;
- battery/power devices and compatible external sensors on shared I2C;
- PDM capture on I2S0 behind the audio-capture service; and
- asynchronous speaker output on I2S1.

Providers must respect the selected runtime profile before configuring chip-select, interrupt, control, or destructive power pins.

### Runtime dispatch contract

The compiled application descriptor registry is authoritative for application identity, persistence identity, service membership, probe policy, activation behavior, runtime dispatch, and optional semantic transition veto.

Application availability is derived from configured enablement plus services/capabilities legal and available on the selected profile. Board-specific branching belongs in board/provider integration code, not application modules.

## Application lifecycle, views, and resource sessions

Every active app has a temporary **resource session**, including apps that currently allocate no dynamic PSRAM. The same session owns the app's managed `rfscout_view_t`.

The current canonical view is 320x170 and maps to the full T-Embed display. Applications render through their assigned view rather than owning the physical display.

The application-facing lifecycle is intentionally small:

1. **Enter** — establish inexpensive app-local state against the assigned view.
2. **Settle** — allow rapid navigation before expensive activation.
3. **Activate** — begin normal app/service work after the common settle point.
4. **Close** — optionally veto an unsafe semantic transition; otherwise platform teardown, view cleanup, and memory reclamation remain framework-owned.

A transition is transactional:

1. ensure the source session/view exists;
2. allow the source app's optional semantic `prepare_leave` veto;
3. quiesce each platform service declared by the source descriptor;
4. if service close fails, restore only safely restartable service state and keep source memory/presentation intact;
5. dispatch committed LEAVE cleanup after successful service close;
6. release/sanitize the source view;
7. bulk-reclaim the exited app's managed memory and verify no managed allocation remains; and
8. establish the destination session/view before ENTER can use destination resources.

A successful service stop is the platform reachability proof: new work that can reference app state cannot enter, admitted workers/callbacks have drained, and shared hardware is inactive. Apps do not implement a second allocator/reachability protocol.

Once source cleanup has committed, RFScout does not resurrect the old application. If destination-session creation fails, lifecycle diagnostics record a committed-destination failure and the app manager attempts Home fallback without dispatching ENTER against an invalid view.

Current session-managed asynchronous services include Wi-Fi monitoring, infrastructure Wi-Fi, OTA acquisition/installation, BLE monitoring, CC1101, nRF24, and shared audio capture/Recorder activity. Thermal acquisition is synchronous today.

## Memory policy

Growable retained app state uses `app_session_calloc()`, `app_session_resize()`, and `app_session_free()`. The lower-level memory manager supplies owner accounting, failure tracking, and bulk reclamation.

App-session **PSRAM** is for retained observations, histories, captures, and other meaningfully sized/growable state. Internal SRAM remains for RTOS/system state, task stacks, DMA/peripheral-constrained buffers, and latency-sensitive allocations.

There is deliberately no arbitrary per-app item quota. Collections grow according to resources and semantics; presentation scratch can remain bounded separately.

Usable external PSRAM is a supported-platform requirement. RFScout does not silently substitute internal SRAM with a hidden lower capacity.

## Semantic input and presentation

Applications consume semantic input rather than GPIOs. The current T-Embed provider maps the rotary encoder/buttons into signed navigation, Select, long-Select, and Back behavior. The dedicated user/power key remains a separate power-policy input.

Edge capture survives temporary main-loop/display stalls. Input used only to wake a dimmed/off display is consumed before normal navigation or power handling. The device-wide input lock suppresses ordinary semantic navigation/select input without disabling the dedicated T-Embed power key.

Common presentation overlays are platform-owned. Applications render their view, while power/system-event/app-modal presentation is composed by the shared display service so application lifecycle state is not replaced by a transient notification.

## Audio architecture

Audio capture is board-neutral. The current T-Embed PDM provider uses I2S0; the service owns provider probing, open/read/close, DMA/I2S lifetime, and physical PDM-clock diagnostics. Recorder and Audio Spectrum consume normalized PCM rather than raw I2S handles.

The low-level `audio_capture` provider reports the configured capture rate and measured PDM-clock diagnostics. The shared Recorder/monitor layer currently measures delivered PCM throughput over wall time for its consumers: Recorder uses a valid measured rate in WAV metadata, and Audio Spectrum uses a valid measured rate for FFT frequency labels. That measurement has not yet been promoted into a generic calibrated `audio_capture` telemetry contract for future consumers; bounded clock/rate correction and generalized calibration remain roadmap work.

Speaker output is a separate semantic service on the validated T-Embed I2S1 path. UI/event/alert feedback policy is independent from explicitly requested content playback such as Recorder audio.

## Radio-service boundaries

The Wi-Fi and BLE monitor services normalize passive observations for the [WiFi / BLE app](apps/WIFI_BLE.md). Their app-owned collection/index lives in the active app session and both collectors must quiesce before that session is reclaimed.

CC1101 and nRF24 are receive-only scanner services advertised by the current T-Embed profile. Each owns a session-managed worker that must stop before app memory release. App-specific interpretation and current retained-history limits are documented in [CC1101](apps/CC1101.md) and [nRF24](apps/NRF24.md).

Infrastructure Wi-Fi is a different platform concern from the passive observation monitor. It is owned by the Home session and is used for explicit network/NTP/OTA workflows without teaching the WiFi/BLE observation app about station/update state.

## Configuration and persistent records

RFScout starts from compiled defaults and overlays firmware-managed records from internal LittleFS. Removable SD is **not** configuration authority and editable `rfscout.json` files are not imported or synchronized.

Durable state uses logical owner/key namespaces:

- `system/config` — ordinary device-wide policy such as display/power behavior, themes, timezone/clock presentation, SD logging, app enablement/startup choice, global audio policy/gains, and alert policy;
- `app/<persistence_key>/config` — descriptor-confined app preference overrides;
- `system/time.policy` — bounded Auto/Manual wall-clock policy;
- `system/breadcrumbs` — breadcrumb enablement/retention policy; and
- additional bounded `system/*` or app-scoped records owned by their service/application.

These are logical API names, not literal filesystem paths. The persistent-store service hashes logical keys and app identities into filesystem-safe private paths.

### Store guarantees

The persistent-store service owns LittleFS mount/path construction and serializes filesystem operations internally.

Current rules include:

- records are bounded to 16 KiB;
- a write atomically replaces one record using same-directory temporary + flush/fsync + rename;
- atomicity is per record, not a multi-record transaction;
- interrupted temporary files are ignored as authority and cleaned once at startup;
- reads/existence checks do not create missing app directories; and
- asynchronous app work can use a captured app/session generation so stale contexts are rejected after transition.

Normal app-facing operations cannot choose another app's namespace. Core orchestration has the privileged path needed to load/save system and app configuration records.

### Save/default semantics

Ordinary setting setters mark owned configuration dirty and the normal service loop flushes it automatically. There is no Home **Save config** action.

A missing record means compiled defaults. Malformed/unsupported owned records fall back to defaults for that owner. App configuration records are sparse overrides: returning all app preferences to their defaults removes the app config record.

The retired monolithic internal `rfscout.json` representation is not authoritative or migrated forward as an editable interchange format.

## Time, location, and sleep state

`device_time` owns RTC-backed wall-clock validity/source, Auto/Manual policy, timezone conversion, and manual time independently from any location provider.

The current T-Embed profile does not advertise onboard GPS. A compatible GPS/location provider remains capability-gated. Home infrastructure networking can provide NTP after DHCP; NTP synchronization does not change the stored Auto/Manual policy.

Deep sleep loses ordinary RAM, so supported profiles keep a small RTC-backed session envelope separate from persistent configuration. Current state preserves active application and input-lock state; menus, edit state, queued sounds, and active Recorder operations are not reconstructed.

## Power, battery, and display idle

Power actions are semantic and filtered by runtime profile capability. T-Embed supports screen-off, deep-sleep, and physical-power-off behavior.

Display dim/off is presentation policy, not application lifecycle or ESP32 sleep. The display provider can disable output/enter controller low power while acquisition/logging/audio continue; wake restores the latest framebuffer before brightness.

The current T-Embed fuel gauge/charger provides battery percentage and authoritative external-power evidence. Protective low-battery action requires fresh battery data plus evidence that external power is absent. High internal-temperature protection is also constrained by the active profile's supported terminal actions.

Terminal sleep/power-off preparation flushes dirty configuration and quiesces required storage users. Active Recorder recording/finalization can veto the transition.

## SD storage and logging

SD is removable/bulk storage for recordings, logs, breadcrumbs, captures, and future explicit transfer packages. Internal configuration must remain usable without an SD card.

The storage service owns the SD mount for the running firmware lifetime and latches physical-media loss unavailable rather than inventing an application-visible unmount/remount protocol.

Current paths include:

- Recorder WAV files under `/sdcard/audio/`;
- rotating event logs under `/sdcard/logs/`; and
- optional breadcrumb CSV files under `/sdcard/logs/breadcrumbs/`.

The conservative hardware-tested SD baseline remains a FAT-compatible SanDisk card of 32 GB or less. Rotating logs use `rfscout-00.log` through `rfscout-99.log`, up to 4 MiB each, while attempting to preserve at least 64 MiB free.

Ordinary RFScout logs are newline-delimited structured records produced through the shared asynchronous log writer. Application lifecycle/telemetry logging also preserves each new transactional app-transition diagnostic as a `type:"transition"` record. The record reuses the lifecycle coordinator's existing diagnostic state rather than reconstructing transition results later and includes:

- `seq` — monotonic transition-attempt sequence for the current boot;
- `from` / `to` — application names;
- `outcome` and `error` — lifecycle result and ESP error name;
- `reclaimed` / `remaining` — managed bytes reclaimed from the source app and managed source allocations still present after committed reclamation;
- `elapsed_us` — lifecycle transition duration; and
- `psram_free` / `psram_largest` — free PSRAM and largest free PSRAM block captured when the transition result is finalized.

The same lifecycle diagnostics continue to be emitted through the ESP console for development, but when SD logging is available the structured record is the preferred source for field validation because it can be correlated directly with adjacent app and telemetry records. Logging is nevertheless **best-effort and non-authoritative**: application/lifecycle correctness must never depend on a record being accepted or persisted. If logging is disabled, unavailable, backpressured beyond the writer's bounded capacity, or fails, the record may be dropped and the producer continues without recovery coupling.

`app_logging` observes each new lifecycle transition sequence once and offers one structured transition record to the existing writer. It does not retry or retain a second logging backlog. This keeps logging failure below the application/lifecycle boundary: individual apps do not know whether SD logging is enabled or whether a particular record was accepted. Transition `seq` values remain monotonic for the boot, so gaps between persisted transition records indicate omitted observations without turning logging into a reliable-delivery protocol.

Breadcrumb recording is independent from the ordinary SD logging toggle and requires fresh valid GPS position **and GPS UTC**. Accepted fixes are sampled at most once every five seconds and bucketed into ten-minute GPS-UTC filenames. Current retention is file-count based.

## Firmware image and OTA architecture

The current 16 MiB layout uses ESP-IDF A/B application slots rather than a single factory application partition. It reserves NVS/PHY metadata, OTA-data, two approximately 6 MiB application slots, and the remaining internal LittleFS storage.

Rollback support is bootloader-enabled. OTA-data is ESP-IDF/bootloader state rather than application configuration.

The combined image remains the clean-install/recovery artifact and contains the bootloader, A/B partition table, initialized OTA metadata, and application. Normal in-device OTA writes only the application image.

### Home network/update ownership

Infrastructure networking and Update are nested Home workflows, but the station session belongs to Home itself. Leaving Network or Update preserves a connected Home session; explicit Disconnect or leaving Home tears it down.

Update requires both station connectivity and NTP-ready state before HTTPS catalog/install operations are offered.

### Acquisition and installer

Catalog/image HTTPS acquisition is cancellable and uses ESP-IDF certificate-bundle validation.

The transport-neutral installer:

1. asks ESP-IDF for the running and next eligible OTA partition;
2. verifies a non-running destination and image fit;
3. streams bytes directly with exact byte counting and SHA-256;
4. completes ESP-IDF image validation;
5. verifies embedded RFScout project/version identity;
6. reads back exactly the declared artifact byte count and verifies SHA-256;
7. commits the destination boot partition only after every check succeeds; and
8. reboots immediately after commit.

SD and LittleFS are never firmware staging areas. The app/UI does not know partition offsets, labels, or OTA sequence counters.

The running firmware also logs SHA-256 over the exact on-flash application artifact range so it can be compared directly with the packaged `*-app.bin` digest.

### First-boot confirmation

A newly installed image remains pending verification until bounded mandatory common startup reaches the application-manager/display-idle checkpoint. RFScout then cancels rollback. Optional peripherals, removable storage, Internet availability, and reopening Update are not health prerequisites.

A reset/power loss before confirmation intentionally leaves ESP-IDF rollback semantics able to return to the previous working slot.

Pages/browser-flasher publication details and release procedure are operational concerns documented in [Firmware releases](operations/RELEASING.md). Remaining OTA fault-injection/hardware validation belongs in [ROADMAP.md](ROADMAP.md).

## System events and diagnostics boundary

The common System Events facility owns a bounded volatile structured event ring. Applications/services can emit semantic events without taking over presentation/lifecycle state. The [System Events app](apps/SYSTEM_EVENTS.md) is only a view of that platform history.

Persistent configuration/storage failures, hardware/service discoveries, and other supported diagnostics can therefore be represented independently of ordinary SD logs.

## Adding another board profile

A future compatible board profile should:

1. add an immutable profile under `services/board`;
2. add a distinct, minimally invasive early fingerprint;
3. leave unknown/ambiguous hardware inert;
4. advertise only capabilities RFScout can initialize safely;
5. keep applications dependent on semantic services/managed views;
6. implement new providers behind existing service boundaries where practical; and
7. validate power, shared buses, display transforms, RF/audio/storage behavior, and destructive/low-power actions on real hardware before advertising them.

Do not add a second profile merely because hardware is electrically similar. A profile should have a meaningful RFScout use case and enough hardware validation to justify its maintenance cost.

## Validation invariants

Release validation should exercise rapid app rotation, low memory, callback/timer races, worker-stop failures, dependency loss, Recorder recording/save vetoes, Audio Spectrum entry/exit, and failed-close restoration.

Key invariants are:

- successful app exit leaves zero managed allocations for the exited app;
- an aborted transition preserves source memory/state/presentation;
- no worker/callback can reach reclaimed app state;
- baseline free PSRAM and largest-free-block behavior do not decline monotonically through repeated cycles;
- destination-session failure never dispatches ENTER without a valid view;
- unknown hardware remains inert and providers never initialize outside profile legality;
- internal configuration works without SD and corrupted/missing records recover to defaults;
- Recorder operations cannot be truncated by application or terminal power transitions; and
- OTA never marks an image bootable before identity, structure, byte-count, streaming SHA, and flash-readback checks all succeed.

Build/release procedure is documented in [Firmware releases](operations/RELEASING.md). Application-specific behavior is documented under [`docs/apps/`](apps/).
