# RFScout roadmap

This is the canonical planning document for RFScout. Production documentation describes implemented behavior; future design, priorities, follow-up work, and brainstorming belong here so intent is not scattered across operational docs.

## v0.4.0 priorities

### Workstream A: hardware abstraction and board portability

**Status:** In progress.

The firmware should continue moving board-specific display, input, bus, sensor, and peripheral details behind platform/service contracts so additional devices such as T-Deck class hardware can select appropriate services at startup without turning the firmware into a universal monolith.

### Workstream B: deterministic app-exit reclamation

**Status:** Transactional foundation implemented by PR #64; simplified resource-session model implemented by PR #68; descriptor-driven runtime and simplified semantic-veto model implemented by PR #70; hardware stress/fault-injection validation remains open.

Application changes use a single platform-owned resource-session boundary rather than parallel app-specific reachability or rollback protocols:

1. Ensure the source application has a resource session.
2. Allow an optional app-semantic prepare hook to veto the transition **before** service teardown when an operation cannot safely be interrupted.
3. Ask the session to stop/quiesce every platform service associated with the source app.
4. Abort without reclaiming memory and restore only the successfully stopped source services that expose a safe restart operation if any service cannot quiesce safely.
5. Treat successful session close as the platform reachability proof; applications do not implement separate worker/callback reachability callbacks.
6. Commit app-visible LEAVE cleanup only after platform services are safely quiesced.
7. Reclaim all remaining memory owned by the source application and verify zero managed allocations remain.
8. Create the destination session before the destination app begins using app-scoped resources.

PR #68 removes duplicate Wi-Fi/BLE, CC1101, and nRF24 stop/restart bookkeeping from `app_manager`; service failure/recovery semantics live with the platform session. Service membership is platform-defined rather than negotiated through a public registration API or arbitrary service-slot quota.

PR #70 moves normal app runtime behavior behind compiled descriptors and removes the former app-level reactivation hook. Recorder now uses only a pre-teardown veto while recording or finalizing a file; RFScout does not stop an active recording and later attempt to reconstruct/restart it after an aborted transition. Playback may stop during an ordinary committed leave. Audio Spectrum follows the normal resource-session lifecycle with no Recorder-specific rollback behavior.

Outstanding validation includes rapid app rotation, repeated Home/app cycles, large histories, Recorder recording/save transition vetoes, Audio Spectrum rapid skip-through and no-SD operation, Thermal attach/detach, forced service-quiesce failures, callback/timer races, low-memory behavior, baseline-memory stability, and largest-free-block stability.

### Workstream C: dynamic WiFi/BLE retention and observation model

**Status:** Initial dynamic observation storage implemented; richer retention semantics remain open.

PR #68 implements the first storage/lifecycle slice:

- allocate retained Wi-Fi/BLE observations lazily in app-owned PSRAM;
- grow observation capacity geometrically as observations arrive rather than imposing a fixed item ceiling;
- reclaim the observation collection through the WiFi/BLE resource-session close after both collectors have drained;
- make the session the sole platform-service quiescence/restart authority for WiFi/BLE, CC1101, nRF24, and audio-capture safety;
- preserve current finite timeout behavior and independent Wi-Fi/BLE source aging;
- remove the former 1000-item UI mirror/capacity assumption;
- build the current summary directly from the live collection using growable app-owned scratch, while keeping only the physically displayable top brand rows in the UI itself.

Still planned:

- Instant / Timed / Never semantics for Wi-Fi, BLE, and surveillance history;
- richer entity/identity semantics and Wi-Fi relationship views;
- memory-pressure/eviction behavior for retained histories;
- migration of surveillance history to the same dynamic model.

### Workstream D: internal persistence and durable app records

**Status:** Configuration persistence and flash rebalance implemented by PR #71; generic app record/database service remains future work.

PR #71 establishes the storage split needed for durable state that survives app teardown without depending on removable media:

- resize the 16 MiB flash layout to an 8 MiB application partition plus approximately 8 MiB SPIFFS persistence;
- make one internal `/internal/rfscout.json` file authoritative for device configuration;
- use only a transient `.tmp` plus atomic rename for internal updates, with no retained `.bak`/`.bad` copies;
- replace missing/corrupt/incompatible internal configuration with compiled defaults;
- treat `/sdcard/rfscout.json` as an explicit one-shot import and preserve invalid imports as `/sdcard/rfscout.json.bad`;
- maintain `/sdcard/rfscout-example.json` as the current authoritative configuration and skip writes when the file already matches;
- keep logs, WAV recordings, and other bulk/exportable data SD-only.

Future work should expose the unused internal persistence capacity through a generic system-owned record/database service rather than letting apps directly own SPIFFS files. The service should support namespaced durable records that survive app session teardown while preserving the rule that app RAM is fully reclaimable on exit.

## App-specific review

Every current application receives an explicit v0.4.0 review rather than treating lifecycle work as only a framework concern.

### WiFi / BT

- [ ] Implement Instant/Timed/Never retention semantics.
- [ ] Migrate surveillance history to growable app-session storage.
- [ ] Add richer device/entity correlation and relationships.
- [ ] Validate low-memory behavior and app-exit reclamation repeatedly.

### CC1101

- [ ] Review retained spectrum/fingerprint history sizing and app-session ownership.
- [ ] Validate repeated shared-SPI transitions and worker stop failures.

### nRF24

- [ ] Review retained activity/packet history sizing and app-session ownership.
- [ ] Validate repeated shared-SPI transitions and worker stop failures.

### Recorder

- [ ] Finish the shared calibrated Audio Input service/generalization described later in this roadmap.
- [ ] Separate app-owned PSRAM work buffers from DMA/internal-memory requirements.
- [ ] Release mode/operation scratch at appropriate boundaries while keeping SD recordings independent of runtime reclamation.
- [ ] Prove microphone/playback workers stop before app memory release and validate repeated transitions.
- [ ] Keep speaker/output loudness in percentage semantics and microphone/input digital gain in dB.
- [ ] Validate that recording/save transition vetoes cannot strand, truncate, or restart an active recording.

### Audio Spectrum

- [ ] Validate the current `0..+48 dB` live digital input-gain range against measured hardware behavior and clipping/noise characteristics.
- [ ] Decide whether live Spectrum gain and Recorder capture gain should share one persisted setting or remain deliberately independent; document/migrate the schema accordingly if persistence changes.
- [ ] Add useful spectrum controls only where they improve interpretation (for example averaging/smoothing, peak hold, or display scale) without making the app contract complex.
- [ ] Validate rapid skip-through before delayed activation, repeated entry/exit, and microphone-only operation without SD storage.

### Thermal

- [ ] Calibrate/validate thermal output and repeated attach/detach behavior.
- [ ] If acquisition becomes asynchronous, move it behind a platform service before retaining app references.

## Shared Audio Input service

**Priority:** High

PR #70 establishes the first shared audio-capture boundary by moving microphone/recording implementation under `services/audio/` and allowing Recorder and Audio Spectrum to consume the same microphone service. The remaining work is calibration/generalization rather than making the microphone a Recorder-owned resource.

Goals:

- [x] Move shared I2S0/PDM microphone ownership out of the Recorder app module and behind a reusable audio service boundary. PR #70.
- [ ] Keep the board microphone on its deterministic LEFT slot.
- [ ] Configure toward a desired physical PDM outcome rather than assuming an ESP-IDF request produces the requested clock/rate.
- [ ] Measure physical PDM clock with PCNT and use bounded reconfiguration when necessary.
- [ ] Clamp requests to known-safe limits and reject measured clocks outside the supported range.
- [ ] Measure delivered PCM throughput and expose an effective sample rate.
- [ ] Expose requested rate, measured clock, effective rate, calibration result, level, peak, and clipping telemetry.
- [ ] Generalize fixed digital input-gain policy so Recorder capture and live analysis have explicit, documented persistence/composition semantics.
- [x] Serialize microphone ownership so recording and monitoring cannot independently own I2S0. PR #70 resource-session/audio-capture boundary.
- [ ] Keep whole-recording operations such as DC correction, trim, and normalization in Recorder-specific behavior.
- [ ] Make FFT frequency interpretation use the measured effective PCM rate.

Future consumers may include tuner/pitch analysis and microphone-side CW/Morse receive. Those remain distinct from RF-side decoding and are not v0.4.0 release blockers unless separately promoted.

## v0.4.0 validation gate

v0.4.0 is not complete until:

- [ ] All tests/build/format checks pass.
- [ ] App-switch stress testing shows a stable memory baseline, no ownership violations, and zero app-owned allocations after successful exit.
- [ ] Pre-reclamation safety gates prove no worker/callback/timer can reference app memory; forced service-close failures restore only safely restartable platform services before any irreversible cleanup.
- [ ] Recorder recording/save transitions are vetoed until the file is safely finalized; no interrupted recording is reconstructed or restarted after an aborted transition.
- [ ] Audio Spectrum can be skipped before delayed activation without touching microphone hardware, runs without SD storage, and repeatedly enters/exits without leaking service state.
- [ ] Internal configuration survives SD removal, invalid SD imports do not replace internal truth, internal corruption regenerates defaults, and repeated saves do not accumulate internal sidecar files.
- [ ] Largest free block remains stable enough to rule out systematic fragmentation/leak growth.
- [ ] WiFi/BLE Instant, finite timed, and Never behavior is verified, including full reclamation on exit and memory-pressure paths.
- [ ] Wi-Fi primary device counts are not inflated one-for-one by rotating randomized Probe Request MACs.
