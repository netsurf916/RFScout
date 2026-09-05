# Architecture

This is the canonical production architecture reference for RFScout. It describes implemented boundaries and invariants on current `main`; future design and release planning belong in [ROADMAP.md](ROADMAP.md).

## Board, app, and service boundaries

RFScout is one source tree producing board-specific firmware images. The current target is **LILYGO T-Embed CC1101 Plus**. `RFSCOUT_BOARD` selects physical definitions under `main/boards/<target>/`; application code consumes semantic services/capabilities rather than board names. See [BOARD_PORTING.md](BOARD_PORTING.md).

Source ownership follows the same rule:

```text
main/
  apps/<app>/           app descriptor, views, analysis, app-owned state
  services/<service>/   reusable hardware/platform capability implementation
  boards/<target>/      physical board definitions
  app_*.c               core app orchestration/lifecycle/session framework
```

Compiled-in applications export `rfscout_app_descriptor_t` metadata: identity/name, availability probe policy/function, delayed activation policy, and platform-service dependencies. `app_framework` discovers descriptors through the compiled registry. Normal navigation remains centralized in `app_manager`; hardware-specific stop/drain/recovery behavior does not.

Canonical rotary order is System Events -> Home / Device Settings -> WiFi / BT -> CC1101 -> nRF24 -> Recorder -> Thermal. Disabled or unavailable apps are skipped. CC1101/nRF24 use startup-cached availability probes; Recorder/Thermal are hot-plug aware.

## Application lifecycle and resource sessions

Every active app has a temporary **resource session**, including apps that currently allocate no dynamic PSRAM. Apps should not manage allocator owner IDs, callback counters, or driver-specific reachability protocols.

The application-facing lifecycle is intentionally small:

1. **Enter** — establish inexpensive UI/runtime state.
2. **Settle** — optionally defer hardware activation so rapid navigation remains responsive.
3. **Activate** — use declared platform services and app-session memory.
4. **Close** — preserve/finalize only user-visible semantic state the app itself understands.

Growable retained app state uses `app_session_calloc()`, `app_session_resize()`, and `app_session_free()`. The lower-level memory manager supplies ownership/accounting/bulk reclamation and is not normally an app API.

A transition is transactional:

1. ensure the source resource session exists;
2. capture reversible semantic rollback state when needed;
3. quiesce every platform service declared by the source descriptor;
4. if any service cannot quiesce safely, restore the source service/semantic state and do **not** reclaim memory;
5. after successful service close, commit irreversible app-visible cleanup;
6. bulk-reclaim all source-app managed memory and verify no managed allocation remains; and
7. establish the destination session before it can use app resources.

A successful service stop means no new work that can reference app state can enter, admitted callbacks/workers have drained, and shared hardware is safely inactive. Successful session close is therefore the reachability proof; apps do not implement a second reachability phase.

On an aborted close, the session restarts services that were active and successfully stopped. Failed-stop recovery is service-specific so a self-restoring worker is not started twice. App-specific rollback is reserved for semantic state the platform cannot infer. Recorder is the current example: worker/channel safety is session-owned, while restoring the exact prior recording/monitor/playback operation is semantic state.

Current session-managed asynchronous services are Wi-Fi monitoring, BLE scanning, CC1101, nRF24, and Recorder. Thermal acquisition is synchronous today; any future worker/DMA/callback path belongs behind a service rather than a new app-specific lifecycle protocol.

### Memory policy

App-session PSRAM is intended for retained observations, histories, captures, and other meaningfully sized growable state. Task stacks, DMA buffers, driver-required internal allocations, and latency-sensitive objects continue to use the memory class required by ESP-IDF/hardware.

There is deliberately no arbitrary per-app item quota. Collections grow according to resources and retention semantics; presentation work should be bounded separately. Under pressure, prefer current/correct data, release reconstructable scratch, evict stale/low-value history where semantics allow, surface pressure, and continue safely.

Usable external PSRAM is a supported-platform requirement. RFScout does not silently substitute internal SRAM with a hidden smaller item ceiling.

## Semantic input and UI behavior

Physical controls are translated to semantic input before app handling. Only accepted semantic actions/state changes emit feedback events; raw encoder/contact movement does not itself imply sound.

On the current one-center-button target, completed button edges are captured independently from the main presentation loop so a click can survive a temporary synchronous LCD stall. Hold timing is part of the semantic input contract: approximately 0.8 to under 1.5 seconds produces `LONG_SELECT`, 1.5 to under 2 seconds produces `BACK`, and a two-second hold toggles the global input lock. The final release after a lock-toggle hold is consumed.

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

## Recorder

Recorder currently uses I2S0/PDM microphone capture, runtime effective-rate measurement, live monitor FFT, app-owned input gain, asynchronous recording post-processing, SD recording catalog, and I2S1 playback through the shared speaker service.

Recordings are 16-bit mono PCM WAV. The effective delivered sample rate is written to the WAV header. Save processing applies the current trim/DC-removal/bounded-normalization pipeline. Recorder files are independent of runtime app-memory reclamation.

The planned calibrated shared Audio Input service is not yet implemented.

## Thermal

MLX90640 is an optional hot-plug I2C/Qwiic peripheral exposed by the thermal service. Thermal acquisition is synchronous while active. Native sensor resolution is 32x24; display interpolation must not imply additional sensor resolution. Literal image pixels bypass theme translation while labels/chrome remain theme-aware.

## Power, display idle, and sleep

Power actions are semantic and board-capability aware. Short press may be None, Screen off, Sleep, or Power off; long press may be None or Power off. Display wake has priority: a complete gesture that wakes a dim/off display is consumed before app, lock, or power-button handling.

Display dim/off is presentation policy, **not** application lifecycle or ESP32 sleep. The off state sets backlight to zero, disables ST7789 output, enters panel hardware sleep, and suppresses framebuffer SPI presentation while apps/acquisition/logging/audio continue. Wake performs panel sleep-out/settle, presents the latest framebuffer while dark, then restores brightness.

Protective policy can react to sustained low battery or high ESP32 internal temperature. Low-battery action requires five consecutive fresh fuel-gauge reads plus fresh evidence that external power is absent; it rearms after charge rises three percentage points above threshold. High-temperature action requires five consecutive samples and rearms 5 C below threshold. High-temperature Power off falls back to Sleep when external-power absence cannot be freshly established. Manual/protective transitions share the global transition overlay and semantic execution path.

Deep sleep stores only a small RTC-backed session envelope (currently active app and input-lock state). Menus, edit state, queued sounds, and active Recorder operations are not resumed. Dirty canonical configuration is flushed before sleep.

## Storage and persistence

The storage service owns SD mount/state and asynchronous rotating logs. Mount failure is nonfatal; radio/sensor apps continue, while persistence and Recorder file operations require mounted storage.

The conservative board-tested baseline is a FAT-compatible SanDisk card of 32 GB or less. The current board exposes no dedicated card-detect GPIO, so availability is established by successful mount/probe. Logging rotates `rfscout-00.log` through `rfscout-99.log` at up to 4 MiB each and attempts to preserve at least 64 MiB free.

Canonical preferences live in `/sdcard/rfscout.json`; see [CONFIGURATION.md](CONFIGURATION.md). Recorder files live under `/sdcard/audio/`.

## Shared hardware/resource boundaries

On the current target:

- LCD, microSD, CC1101, and nRF24 share SPI resources;
- battery/power devices and external sensors share board I2C;
- Recorder microphone uses I2S0;
- the asynchronous speaker service uses I2S1.

Application views render their content; the display service owns common top chrome and final global overlays/presentation. The RAM-only System Events ring is a platform service and its optional app is a read-only browse view.

## Validation invariants

Release testing should rapidly rotate every enabled app and exercise low memory, callback races, worker-stop timeouts, dependency loss, Recorder transitions, and failed-close rollback. Successful app exit must leave zero managed allocations for the exited app; an aborted transition must preserve its memory/state. Baseline free PSRAM and largest-free-block behavior should not decline monotonically across repeated cycles.

Build/release workflow is documented in [RELEASING.md](RELEASING.md); board requirements in [BOARD_PORTING.md](BOARD_PORTING.md); user-visible behavior in [CAPABILITIES.md](CAPABILITIES.md).
