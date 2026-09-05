# RFScout architecture

RFScout separates **applications**, **platform services**, and **board definitions** so app behavior does not own hardware teardown, memory reachability, or physical pin/bus details.

Source ownership follows the same rule:

```text
main/
  apps/<app>/           app descriptor, controller, views, analysis, app-owned state
  services/<service>/   reusable hardware/platform capability implementation
  boards/<target>/      physical board definitions
  app_*.c               core app discovery/navigation/lifecycle/session framework
```

Each compiled-in application exports one `rfscout_app_descriptor_t`. The descriptor owns its identity/name, availability probe policy/function, delayed-activation policy, platform-service dependencies, and runtime handler. `app_framework` discovers descriptors through the compiled registry; normal app behavior is not implemented through app-ID switches in `app_manager`.

`app_manager` owns only cross-application policy: active-app selection, availability filtering, settle/activation timing, global navigation, and transactional lifecycle coordination. App modules own their menu/subview state, rendering decisions, app-local input interpretation, normal polling, and dependency-loss behavior. Hardware-specific stop/drain/recovery remains behind platform services and resource sessions.

Canonical rotary order is System Events -> Home / Device Settings -> WiFi / BT -> CC1101 -> nRF24 -> Recorder -> Thermal -> Audio Spectrum. Disabled or unavailable apps are skipped. CC1101/nRF24/Audio Spectrum use startup-cached availability probes; Recorder/Thermal are hot-plug aware.

## Runtime dispatch contract

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

## Memory policy

PSRAM is a supported-platform requirement. Large/growable application collections use app-session memory and disappear when the app session ends. Internal SRAM remains for stacks, DMA/hardware-constrained buffers, low-level driver state, and latency-sensitive/system use.

The design deliberately separates persistent storage from app-session RAM. Future app-owned durable records should be mediated by a platform persistence service rather than keeping app RAM alive across exits.

## WiFi / BT

WiFi/BLE observations use app-session PSRAM and grow dynamically while the app is active. Wi-Fi and BLE collectors are shared services; callback admission/drain is complete before retained observation memory can be reclaimed.

## CC1101 and nRF24

Both radios are receive-only analysis paths. Their persistent workers are platform services associated with the owning app session. Shared-SPI ownership cannot move until the source worker has observed its stop request and hardware is inactive.

nRF24's RPD bit is threshold evidence (roughly whether received power exceeded the radio threshold), not decoded-packet RSSI. Packet/fingerprint heuristics are therefore descriptive evidence rather than protocol guarantees.

RF acquisition and LCD presentation run at different cadences. nRF24 sweep work occurs in its worker while completed display snapshots are coalesced; input edge capture is designed to survive temporary synchronous LCD/main-loop stalls.

## Recorder and Audio Spectrum

The shared audio-capture service owns I2S0/PDM microphone access, runtime effective-rate measurement, the live FFT monitor worker, SD recording machinery, and the microphone/channel teardown contract. App modules consume those semantic operations rather than owning I2S/DMA details.

**Recorder** is the storage-backed app. It provides recording, asynchronous save/post-processing, recording catalog/delete, and I2S1 playback through the shared speaker service. Recordings are 16-bit mono PCM WAV; the effective delivered sample rate is written to the WAV header and save processing applies the current trim/DC-removal/bounded-normalization pipeline. Recorder files are independent of runtime app-memory reclamation. Active recording/saving blocks an app transition until the operation is safely complete; playback may be stopped during normal app exit.

**Audio Spectrum** is a separate microphone-only app. It can run without SD storage, starts monitoring after the common app settle point, renders the live FFT spectrum, and uses center to enter its local settings view. The current setting is fixed digital microphone input gain from 0 through +48 dB in 3 dB steps. With settings closed, rotary input remains global app navigation. Audio Spectrum has no Recorder-specific semantic rollback path.

Further calibration/generalization of the shared Audio Input service remains roadmap work.

## Thermal

Thermal acquisition currently runs synchronously from the app's normal poll/render path. MLX90640 availability is hot-plug aware. A future asynchronous implementation belongs behind a platform service before it may retain references across app teardown.

## Power and session state

Manual and policy-triggered sleep/power-off use the same semantic power path. Deep sleep stores only a small RTC-backed session envelope (currently active app and input-lock state). Menus, edit state, queued sounds, and active Recorder operations are not resumed. Dirty authoritative configuration is flushed internally before sleep.

## Storage and persistence

RFScout has two deliberately distinct persistent-storage roles:

- **Internal SPIFFS** is device-owned durable state. The 16 MiB flash currently allocates 8 MiB to the application and approximately 8 MiB to SPIFFS. `/internal/rfscout.json` is the sole authoritative configuration file; updates use only a transient `.tmp` followed by atomic rename. Missing/corrupt/incompatible internal configuration is replaced with compiled defaults rather than retaining backup/quarantine copies.
- **SD/FAT storage** is removable interchange and bulk storage. `/sdcard/rfscout.json` is a one-shot configuration import, `/sdcard/rfscout-example.json` mirrors the internal configuration when contents differ, Recorder WAV files live under `/sdcard/audio/`, and rotating logs remain SD-only.

A valid SD configuration import is copied through the internal temporary file and committed before the source import is deleted. Invalid imports are renamed to `/sdcard/rfscout.json.bad` so user input is preserved without repeated boot-time retries. SD absence never prevents ordinary configuration persistence.

The remaining internal SPIFFS capacity is intentionally unused today. A future generic persistent-record/database service may use it so apps can retain durable records across session teardown without weakening the app-memory lifecycle contract.

The conservative board-tested SD baseline is a FAT-compatible SanDisk card of 32 GB or less. The current board exposes no dedicated card-detect GPIO, so availability is established by successful mount/probe. Logging rotates `rfscout-00.log` through `rfscout-99.log` at up to 4 MiB each and attempts to preserve at least 64 MiB free.

## Shared buses and presentation

On the current target:

- LCD, microSD, CC1101, and nRF24 share SPI resources;
- battery/power devices and external sensors share board I2C;
- shared microphone/audio capture uses I2S0;
- the asynchronous speaker service uses I2S1.

Application views render their content; the display service owns common top chrome and final global overlays/presentation. The RAM-only System Events ring is a platform service and its optional app is a read-only browse view.

## Validation invariants

Release testing should rapidly rotate every enabled app and exercise low memory, callback races, worker-stop timeouts, dependency loss, Recorder recording/save transition vetoes, Audio Spectrum entry/exit, and failed-close rollback. Successful app exit must leave zero managed allocations for the exited app; an aborted transition must preserve its memory/state. Baseline free PSRAM and largest-free-block behavior should not decline monotonically across repeated cycles.

Persistence validation should additionally cover boot without SD, valid/invalid SD config imports, internal corruption/default replacement, interrupted internal temp writes, example no-op synchronization when unchanged, and repeated configuration saves without accumulating internal files.

Build/release workflow is documented in [RELEASING.md](RELEASING.md); board requirements in [BOARD_PORTING.md](BOARD_PORTING.md); user-visible behavior in [CAPABILITIES.md](CAPABILITIES.md).
