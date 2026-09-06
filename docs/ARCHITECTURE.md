# Architecture

This is the canonical production architecture reference for RFScout. It describes implemented boundaries and invariants on current `main`; future design and release planning belong in [ROADMAP.md](ROADMAP.md).

## Board, app, and service boundaries

RFScout is one source tree producing board-specific firmware images. The current target is **LILYGO T-Embed CC1101 Plus**. `RFSCOUT_BOARD` selects physical definitions under `main/boards/<target>/`; application code consumes semantic services/capabilities rather than board names. See [BOARD_PORTING.md](BOARD_PORTING.md).

Source ownership follows the same rule:

```text
main/
  apps/<app>/           app descriptor, controller, views, analysis, app-owned state
  services/<service>/   reusable hardware/platform capability implementation
  boards/<target>/      physical board definitions
  app_*.c               core app discovery/navigation/lifecycle/session framework
```

Each compiled-in application exports one `rfscout_app_descriptor_t`. The descriptor owns identity/name, a stable persistence key, availability probing, delayed activation, service dependencies, and runtime handling. `app_framework` discovers descriptors through the compiled registry and retains probed capability state as unknown, available, or unavailable. Probe error detail remains diagnostic logging rather than application state.

`app_manager` owns only cross-application policy: active-app selection, availability filtering, settle/activation timing, global navigation, and transactional lifecycle coordination. App modules own their menu/subview state, rendering decisions, app-local input interpretation, normal polling, and dependency-loss behavior. Hardware-specific stop/drain/recovery remains behind platform services and resource sessions.

Canonical rotary order is System Events -> Home / Device Settings -> WiFi / BT -> CC1101 -> nRF24 -> Recorder -> Thermal -> Audio Spectrum. Disabled or unavailable apps are skipped. CC1101/nRF24/Audio Spectrum use startup-cached availability probes; Recorder/Thermal are hot-plug aware.

### Runtime dispatch contract

The manager dispatches a deliberately small set of events to the active descriptor:

- **ENTER** — initialize app-local UI/runtime state after a transition commits;
- **ACTIVATE** — delivered after the common settle delay for apps that request delayed activation;
- **INPUT** — one board-neutral semantic input event;
- **POLL** — periodic app work/render opportunity; and
- **LEAVE** — committed app-local cleanup after platform services have safely quiesced.

An app reports whether INPUT was consumed. Unconsumed navigation remains global next/previous-app switching; unconsumed Back returns to Home. This keeps global navigation policy centralized without teaching the manager how an individual app's menu works. Rapid navigation through a delayed app can therefore leave it before activation, preserving the settle behavior that avoids starting hardware merely because the user rotated past an app.

Normal apps need only their module/descriptor, build/registry entry, and whatever semantic services they consume. Adding a normal app should not require modifying `app_manager.c`, `app_session.c`, or driver-specific teardown logic. Shared controls such as per-app notification editing are reusable helpers invoked by app modules rather than state embedded in the manager.

Optional app-specific prepare hooks are reserved for semantic transition vetoes that the platform cannot infer. Recorder uses this only while recording or finalizing a recording: navigation is blocked until the recording has been stopped and saved. RFScout does not stop a recording and later attempt to reconstruct or restart it after an aborted transition. Audio Spectrum needs no special transition hooks and follows the normal descriptor/session contract.

## Application lifecycle and resource sessions

Every active app has a temporary **resource session**, including apps that currently allocate no dynamic PSRAM. Apps should not manage allocator owner IDs, callback counters, or driver-specific reachability protocols.

The application-facing lifecycle remains intentionally small:

1. **Enter** — establish inexpensive app-local state.
2. **Settle** — optionally defer activation so rapid navigation stays responsive.
3. **Activate** — begin normal app/service work after the common settle point.
4. **Close** — optionally veto an unsafe semantic transition before platform teardown; otherwise platform teardown and memory reclamation remain framework-owned.

Growable retained app state uses `app_session_calloc()`, `app_session_resize()`, and `app_session_free()`. The lower-level memory manager supplies ownership/accounting/bulk reclamation and is not normally an app API.

A transition is transactional:

1. ensure the source resource session exists;
2. allow an optional app-semantic prepare/veto before service teardown begins;
3. quiesce every platform service declared by the source descriptor;
4. if any service cannot quiesce safely, restore service state where the service supports recovery and do **not** reclaim memory;
5. after successful service close, dispatch committed app-local LEAVE cleanup;
6. bulk-reclaim all source-app managed memory and verify no managed allocation remains; and
7. establish the destination session before ENTER can use app resources.

A prepare veto happens before session teardown, so it does not trigger service rollback. A successful service stop means no new work that can reference app state can enter, admitted callbacks/workers have drained, and shared hardware is safely inactive. Successful session close is therefore the reachability proof; apps do not implement a second reachability phase.

On an aborted service close, the session restores only platform service state that was active and successfully stopped when that service exposes a safe restart operation. Failed-stop recovery is service-specific so a self-restoring worker is not started twice. There is no app-level reactivation hook: reversible platform restoration belongs exclusively to `app_session` and the service implementation, while app modules may only veto before teardown through `prepare_leave` when semantic state cannot safely be interrupted.

Current session-managed asynchronous services are Wi-Fi monitoring, BLE scanning, CC1101, nRF24, and shared audio capture/Recorder activity. Thermal acquisition is synchronous today; any future worker/DMA/callback path belongs behind a service rather than a new app-specific lifecycle protocol.

### Memory policy

App-session PSRAM is intended for retained observations, histories, captures, and other meaningfully sized growable state. Task stacks, DMA buffers, driver-required internal allocations, and latency-sensitive objects continue to use the memory class required by ESP-IDF/hardware.

There is deliberately no arbitrary per-app item quota. Collections grow according to resources and retention semantics; presentation work should be bounded separately. Under pressure, prefer current/correct data, release reconstructable scratch, evict stale/low-value history where semantics allow, surface pressure, and continue safely.

Usable external PSRAM is a supported-platform requirement. RFScout does not silently substitute internal SRAM with a hidden smaller item ceiling.

## Semantic input and UI behavior

The `input` service translates board controls into focused semantic input before app handling. Physical activity used to wake the display is a separate path and the complete wake gesture is consumed before app, lock, or power semantics. Only accepted semantic actions/state changes emit feedback events.

On the current rotary/button provider, edge capture survives temporary main-loop/display stalls. Holds of roughly 0.8–1.5 seconds produce `LONG_SELECT`, 1.5–2 seconds produce `BACK`, and two seconds toggles the global input lock; the release that completes a lock toggle is consumed.

Common settings use one interaction model:

- **Action** — center executes immediately.
- **Toggle** — center changes On/Off immediately.
- **Choice** — center cycles a small finite set when unambiguous.
- **Value** — center enters edit, rotation adjusts, center exits.
- **Submenu** — center enters a child view.

Application settings use the relative order: Back; alerts/notifications; acquisition/detection; display/presentation; storage/logging/recording; advanced/hardware-specific; destructive/reset actions. Empty placeholder categories are not added. Full-screen diagnostic lists may explicitly claim the encoder after a visible mode change (System Events Browse is the current example).

## Audio and feedback architecture

RFScout separates semantic meaning from presentation. Four audio classes are used:

| Class | Meaning | Level owner |
| --- | --- | --- |
| UI / device | navigation, accepted actions, lock/device feedback | global UI volume |
| Event | lightweight informational activity/detection cue | global Event volume |
| Alert | warning/error and configurable attention notification | global Alert volume |
| Content | explicitly requested app audio, e.g. Recorder playback | app-owned |

`app_audio_policy` describes which classes make sense for an app. `audio_policy` independently applies device-wide **Normal** / **Quiet** presentation. `feedback` combines semantic events with app/device policy; `notification` owns detection vocabulary/preferences; `alert` applies notification mode/mappings; the audio-output service owns speaker arbitration.

Normal permits all classes subject to app/user policy. Quiet suppresses UI, Event, and Alert presentation without rewriting saved volumes, app gates, alert enablement, or Morse mappings. Explicit Content remains available. Disposable feedback/alerts are dropped rather than queued behind active Content audio.

Visual themes are independent: Indoor, Outdoor, and Night alter presentation colors only.

## WiFi / BLE data path

```text
Wi-Fi monitor ---\
                  +--> normalized observation --> prefix identity --> surveillance classifier
BLE monitor -----/              |                        |
                                v                        v
                         recent device store       retained detections
                                |                        |
                                +-----------> WiFi / BT UI
```

The normalized observation collection is app-session PSRAM and grows according to available resources rather than a fixed item ceiling. It uses a session-owned identity index so collector ingestion does not linearly rescan the collection for every callback. Expiration runs through housekeeping; Wi-Fi and BLE membership age independently.

Current production retention is independently configurable for Wi-Fi, BLE, and surveillance history from 10–300 seconds in 10-second steps, default 60 seconds. Planned `Instant` / `Never` semantics and richer entity correlation remain roadmap work.

Presentation derives summaries from live state while retaining only displayable rows/scratch appropriate to the view. Prefix identity tables and surveillance matching are documented in [PREFIX_IDENTITY.md](PREFIX_IDENTITY.md).

## CC1101 and nRF24

CC1101 uses a worker for receive-only RSSI sweeps and conservative behavior classification. nRF24 uses receive-only RPD sweeps plus a separate ingestion/storage path for validated Enhanced ShockBurst observations. Both workers are session services and must quiesce before app memory reclamation.

nRF24's RPD bit is threshold evidence (roughly whether received power exceeded the device threshold), not RSSI and not packet proof. The production scanner currently generates RPD energy observations only; packet browser/detail surfaces consume only separately CRC-validated observations. RFScout does not generate ACKs, inject, replay, pair, probe, or actively discover nRF24 devices. Future receive-only ESB discovery must keep acquisition/validation separate from presentation and preserve that passive boundary; detailed implementation planning remains in the roadmap.

RF acquisition and LCD presentation run at different cadences. nRF24 sweep work occurs in its worker while completed display snapshots are coalesced; input edge capture is designed to survive temporary synchronous LCD/main-loop stalls.

## Recorder and Audio Spectrum

The shared audio-capture service owns I2S0/PDM microphone access, runtime effective-rate measurement, the live FFT monitor worker, SD recording machinery, and the microphone/channel teardown contract. App modules consume those semantic operations rather than owning I2S/DMA details.

**Recorder** is the storage-backed app. It provides recording, asynchronous save/post-processing, recording catalog/delete, and I2S1 playback through the shared speaker service. Recordings are 16-bit mono PCM WAV; the effective delivered sample rate is written to the WAV header and save processing applies the current trim/DC-removal/bounded-normalization pipeline. Recorder files are independent of runtime app-memory reclamation. Active recording/saving blocks an app transition until the operation is safely complete; playback may be stopped during normal app exit.

**Audio Spectrum** is a separate microphone-only app. It can run without SD storage, starts monitoring after the common app settle point, renders the live FFT spectrum, and uses raw microphone PCM with continuous high-pass/DC-block filtering plus a smoothed automatic dB display range, avoiding pre-FFT digital amplification and clipping. The live `AUTO N dB` value is the current display-window span rather than input gain. Rotary input remains global app navigation. Audio Spectrum has no Recorder-specific semantic rollback path.

Further calibration/generalization of the shared Audio Input service remains roadmap work.

## Thermal

MLX90640 is an optional hot-plug I2C/Qwiic peripheral exposed by the thermal service. Thermal acquisition is synchronous while active. Native sensor resolution is 32x24; display interpolation must not imply additional sensor resolution. Literal image pixels bypass theme translation while labels/chrome remain theme-aware.

## Power, display idle, and sleep

Power actions are semantic and board-capability aware. Short press may be None, Screen off, Sleep, or Power off; long press may be None or Power off. Display wake has priority over ordinary input and power-button handling.

The display service owns framebuffer presentation plus runtime provider metadata (provider, geometry, capabilities). Display dim/off is presentation policy, **not** application lifecycle or ESP32 sleep. The current ST7789 provider disables output and enters controller low power while apps/acquisition/logging/audio continue; wake restores the provider, presents the latest framebuffer while dark, then restores brightness.

Protective policy can react to sustained low battery or high ESP32 internal temperature. Low-battery action requires five consecutive fresh fuel-gauge reads plus fresh evidence that external power is absent; it rearms after charge rises three percentage points above threshold. High-temperature action requires five consecutive samples and rearms 5 C below threshold. High-temperature Power off falls back to Sleep when external-power absence cannot be freshly established. Manual/protective transitions share the global transition overlay and semantic execution path.

Deep sleep stores only a small RTC-backed session envelope (currently active app and input-lock state). Menus, edit state, queued sounds, and active Recorder operations are not resumed. Dirty authoritative configuration is flushed before sleep. Wall-clock epoch/source state is not checkpointed into configuration; the RTC-backed system clock remains the clock authority.

## Storage and persistence

RFScout separates device-owned durable state from removable bulk/interchange storage.

The internal `storage` LittleFS partition is owned by the persistent-record service. Core/system callers use privileged `system/*` records while applications are confined to namespaces derived from their compiled descriptor `persistence_key`. Records are opaque to the storage layer, limited to 16 KiB, and atomically replaced; callers own schema/version validation. Interrupted-write temporaries are removed by one startup cleanup pass rather than a background housekeeping task.

Configuration is the first consumer. Device-wide user policy lives in `system/config`; app enablement/startup remains system policy. App-owned `config` records are sparse overrides: absence means compiled defaults, only changed app values are serialized, and returning all app settings to defaults removes that record. Runtime wall-clock epoch/source state is deliberately outside persistent configuration; timezone and clock-presentation preferences remain durable.

SD remains the removable/bulk-storage service rather than a configuration authority. Recorder WAV files remain under `/sdcard/audio/`, and rotating logs remain SD-only. A future settings-transfer feature, if added, should be an explicit firmware-managed import/export operation rather than direct record-file editing.

The current 16 MiB mainline target allocates 8 MiB to the factory application and approximately 8 MiB to internal LittleFS. The persistent-record abstraction is size-independent because the OTA work plans to reduce the durable partition to roughly 4 MiB; consumers must remain bounded and must not infer partition capacity from the current board image.

Internal-storage mount, recovery-format/mount-failure, and configuration-default recovery are recorded in the volatile System Events/dmesg history as distinct semantic events. Startup discovery is retained without interactive notification.

The conservative board-tested SD baseline is a FAT-compatible SanDisk card of 32 GB or less. The current board exposes no dedicated card-detect GPIO, so availability is established by successful mount/probe. Logging rotates `rfscout-00.log` through `rfscout-99.log` at up to 4 MiB each and attempts to preserve at least 64 MiB free.

## Shared hardware/resource boundaries

On the current target:

- LCD, microSD, CC1101, and nRF24 share SPI resources;
- battery/power devices and external sensors share board I2C;
- shared microphone/audio capture uses I2S0;
- the asynchronous speaker service uses I2S1.

Application views render their content; the display service owns common top chrome and final global overlays/presentation. The RAM-only System Events ring is a platform service and its optional app is a read-only browse view.

## Validation invariants

Release testing should rapidly rotate every enabled app and exercise low memory, callback races, worker-stop timeouts, dependency loss, Recorder recording/save transition vetoes, Audio Spectrum entry/exit, and failed-close rollback. Successful app exit must leave zero managed allocations for the exited app; an aborted transition must preserve its memory/state. Baseline free PSRAM and largest-free-block behavior should not decline monotonically across repeated cycles.

Persistence validation should additionally cover boot without SD, missing/app-default records, sparse app overrides and return-to-default record removal, legacy internal configuration migration, internal corruption/default replacement, oversized-record rejection, interrupted temporary cleanup on reboot, and repeated saves without accumulating authoritative sidecar/history files. RTC testing should verify that arbitrary RTC values are used directly, manual/GPS/NTP adjustments are runtime-only, and timezone/12–24-hour preferences remain durable.

Build/release workflow is documented in [RELEASING.md](RELEASING.md); board requirements in [BOARD_PORTING.md](BOARD_PORTING.md); user-visible behavior in [CAPABILITIES.md](CAPABILITIES.md).

## Surveillance classification

Surveillance classification is evidence-driven rather than confidence-accumulating. The app-level High / Medium / Low sensitivity setting chooses the minimum evidence strength required for a single observation to establish a signature; High is the default. Repeated observations refresh active presence; they do not raise a numeric confidence score, and an active identity retains its first qualifying surveillance classification until expiry. The canonical observation store owns alert re-arm through ordinary device expiry, and the UI derives both general and surveillance projections from that same active collection rather than retaining a second surveillance list.
