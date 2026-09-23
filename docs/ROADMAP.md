# RFScout Roadmap

This is the **authoritative planning document** for RFScout. Current product behavior is anchored in [README.md](../README.md), shared implemented contracts live in [ARCHITECTURE.md](ARCHITECTURE.md), and implemented app behavior lives under [apps/](apps/). Future design, priorities, unfinished work, active release scope, and brainstorming belong here.

Statuses describe implementation state, not release state. Git history and pull requests preserve implementation history; completed items should remain here only when they provide useful context for still-open work.

## v0.4.0 milestone plan

**Status:** In progress

v0.4.0 is a resource-management, lifecycle-stability, and application-quality release. The release should make application switching deterministic, allow data-heavy applications to use PSRAM opportunistically instead of being constrained by arbitrary fixed-capacity buffers, and improve each current application on top of that lifecycle model.

Several foundations originally planned for v0.4.0 are already present on `main` and were included in the published v0.3.2 firmware. Git history is authoritative for those implementation boundaries:

- **PR #60 — implemented:** independent Indoor/Outdoor/Night visual themes and Normal/Quiet audio policy.
- **PR #61 — implemented:** shared semantic system-event history, presentation, attention-wake, and initial hardware/system event sources.
- **PR #63 — implemented:** owner-aware PSRAM memory manager with application/operation ownership, safe growth, accounting, failure tracking, and bulk reclamation.
- **PR #64 — implemented:** transactional application-transition/reclamation foundation with post-reclamation accounting checks.
- **PR #65 — implemented:** optional System Events / Recent Events application over the shared volatile event history.

Release-level stress and hardware validation remain outstanding even where the underlying implementation is already merged.

### Release objective

RFScout applications should behave like isolated consumers of a common pool of dynamic memory:

- long-lived platform services keep only memory that genuinely must survive application changes;
- the active application may dynamically consume available PSRAM for retained observations, history, captures, and scratch processing;
- application exit asks the app resource session to quiesce its platform services, and a successful session close is the proof that workers/callbacks can no longer reach app-owned state;
- irreversible cleanup and reclamation happen only after that platform-owned safety gate succeeds;
- the next application starts from a predictable memory baseline and can reuse the released PSRAM;
- PSRAM is a platform requirement for supported RFScout hardware targets.

This is resource isolation by lifecycle, not a promise that literally every byte of physical PSRAM is available to an application. Platform, RTOS, display, driver, DMA, and service allocations that must remain resident are excluded from the app-owned pool. Internal SRAM remains reserved for the system, stacks, latency-sensitive state, and allocations constrained by ESP-IDF or peripheral hardware.

### Workstream A: application-scoped PSRAM memory manager

**Status:** Implemented by PR #63.

Implemented behavior:

- explicit platform/service, application, and nested operation ownership;
- PSRAM-only managed allocation for eligible application storage;
- safe growth with overflow checks and failure preserving the existing allocation;
- owner validation and diagnosable ownership errors;
- current/peak app payload usage, allocation count, and allocation-failure tracking;
- application-wide and operation-wide reclamation;
- unsupported-target failure when usable PSRAM is absent rather than silent internal-SRAM fallback.

The manager is intended for meaningfully sized retained observations, histories, captures, and processing state. Tiny transient objects and DMA/peripheral-constrained allocations should continue to use the appropriate native allocation path.

Preferred memory-pressure order remains:

1. Preserve correctness and current data.
2. Release reconstructable scratch/cache data.
3. Evict oldest/lowest-value retained history where semantics permit.
4. Surface resource pressure.
5. Continue in a bounded state.

A crash, overwrite, or undocumented conversion of an unlimited/session-retained mode into a hidden fixed item limit is not acceptable.

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

Storage/resource policy distinguishes hard lifecycle dependencies from optional operation-time facilities. Hard services remain descriptor-managed; optional facilities such as removable storage are checked only by the service/operation that explicitly requires them, while internal persistence remains a separate device-owned capability.

Outstanding validation includes rapid app rotation, repeated Home/app cycles, large histories, Recorder recording/save transition vetoes, Audio Spectrum rapid skip-through and no-SD operation, Thermal attach/detach, forced service-quiesce failures, callback/timer races, low-memory behavior, baseline-memory stability, and largest-free-block stability.

### Workstream C: dynamic WiFi/BLE retention and observation model

**Status:** In progress. PR #68 begins the application-data migration onto Workstreams A/B and establishes the simplified session abstraction across current apps.

PR #68 implements the first storage/lifecycle slice:

- remove the boot-time fixed 1000-entry retained observation allocation;
- allocate retained Wi-Fi/BLE observations lazily in app-owned PSRAM;
- grow observation capacity geometrically as observations arrive rather than imposing a fixed item ceiling;
- reclaim the observation collection through the WiFi/BLE resource-session close after both collectors have drained;
- make the session the sole platform-service quiescence/restart authority for WiFi/BLE, CC1101, nRF24, and audio-capture safety;
- preserve current finite timeout behavior and independent Wi-Fi/BLE source aging;
- remove the former 1000-item UI mirror/capacity assumption;
- build the current summary directly from the live collection using growable app-owned scratch, while keeping only the physically displayable top brand rows in the UI itself.

The remaining WiFi/BLE design is intentionally broader than this first storage PR.

#### Retention semantics

Support three semantic classes:

- **Instant** — no intentional retention grace period. An observation becomes removable on the next normal housekeeping pass once it is no longer current.
- **Timed** — configurable finite rolling retention.
- **Never** — no age-based removal while WiFi/BLE remains active. This is session-scoped, not persistent storage; leaving the application still releases app-owned history.

Timeouts define whether evidence is current, not whether an object under active inspection must immediately disappear. A selected/deep-dive object may become stale while temporarily retained for UI stability; once unreferenced, normal retention and memory-pressure policy apply.

#### Identity and Wi-Fi semantics

- Stop equating every observed MAC address with a physical device.
- Preserve raw observed addresses while introducing stable app-internal entity IDs, observed-identity sets, and confidence/provenance for correlations.
- Preserve independent source aging where higher-level entities contain evidence from multiple radios.
- Treat randomized-address correlation as evidence, never proof.
- Do not merge stable identities solely because fingerprints look similar.
- Keep randomized Probe Requests as useful transient evidence while preventing rotating probe MACs from inflating the primary device count one-for-one.
- Parse enough 802.11 metadata to preserve address roles/BSSID, frame type/subtype, sequence behavior, channel/RSSI, SSID labels, relevant capabilities, and association evidence.
- Use authentication/association/reassociation exchanges and ordinary associated data traffic as useful topology evidence; EAPOL/handshake traffic strengthens confidence but is not required for ordinary associated-client discovery.
- Treat SSID grouping as label-based analysis only. Same-named APs remain distinct unless separate evidence supports a stronger relationship.

#### BLE semantics

BLE views should reflect what passive observation can actually establish:

- **Devices** — entity-centric observations where correlation has adequate evidence.
- **Advertisers** — rawer advertisement/address activity.
- **Beacons** — recognized beacon/manufacturer-specific formats where supported.
- **Services/Capabilities** — advertised services, manufacturer data, names, appearance/capability metadata, and related advertisement-derived information.

Do not invent a BLE AP hierarchy or claim unobserved central/peripheral connections. Address rotation/private-address correlation must retain confidence and provenance.

#### WiFi/BLE UI

Make radio type a first-class view dimension:

- **Combined / Wi-Fi / BLE**
- Wi-Fi: **SSIDs / APs / Clients / Unassociated / Probes**
- BLE: **Devices / Advertisers / Beacons / Services**

The main screen should remain an actively updated analytical projection. Deeper relationships belong in context-sensitive drill-down. Internal IDs plus active/stale state should keep selection stable while observations continue changing.

#### Surveillance detection quality

This release should replace the classifier's current source-named priority concept with an explicit evidence-strength model while preserving the existing evidence-source bitmask and non-accumulating classifier architecture.

The target strength semantics are:

- **Possible** — credible researched association suitable for High sensitivity, with false positives possible.
- **Probable** — strong association suitable for Medium sensitivity, with false positives expected to be uncommon.
- **Distinctive** — evidence intended to be uniquely identifying and suitable for Low sensitivity.

High / Medium / Low sensitivity therefore select the minimum acceptable evidence strength rather than assigning inherent trust to an evidence source such as a name, OUI, company ID, service UUID, or protocol. Individual rules own their strength; combinations of evidence from the same source may legitimately be stronger than an individual observation.

The implementation pass should first perform the semantic refactor without mechanically preserving the old source-based strength assumptions. After that baseline is stable, perform evidence-backed research and tuning across every currently supported surveillance category/signature. Research should prefer primary/vendor/protocol material, device teardowns or captures, and independently corroborated field observations; community detector projects are useful discovery sources but should not be copied without evaluating false-positive behavior.

The category review must cover at least:

- **ALPR** — Flock, Vigilant, and other supported ALPR evidence;
- **body cameras** — Axon and other supported body-camera evidence;
- **cameras / doorbells** — Ring, Verkada, Axis, Hikvision, Dahua, and other supported camera evidence;
- **smart glasses** — Meta, Brilliant Frame, Even G1, Solos AirGo, Rokid, and other supported smart-glasses evidence;
- **acoustic sensors** — Flock Raven and other supported acoustic-sensor evidence; and
- **drones** — Remote ID and any other passive evidence that can be classified without active probing.

For each signature/category, record the evidence source, provenance, expected uniqueness/false-positive risk, assigned strength, and any known hardware/firmware-generation limitations. Prefer explicit known fingerprints and combinations over broad numeric ranges or generic standard services. Do not infer an exact product from vendor/ecosystem evidence when the observed signal only supports the broader vendor or category.

- [x] Rename classifier `priority` semantics to evidence `strength` without introducing a parallel confidence/priority mechanism.
- [x] Define typed Possible / Probable / Distinctive strengths and expose the same Possible / Probable / Distinctive values directly through the Evidence Threshold UI.
- [x] Preserve evidence-source reporting independently from strength.
- [ ] Add tests proving sensitivity thresholds, strongest-evidence selection, and non-accumulation behavior.
- [ ] Research and re-score every existing surveillance signature/category against the documented strength contract.
- [ ] Expand detection fingerprints where research supports useful passive evidence, including older/newer hardware or firmware generations.
- [ ] Add negative/near-match tests for researched fingerprints so permissive High-sensitivity rules have explicit false-positive boundaries.
- [ ] Document durable provenance for curated fingerprints and generated surveillance-prefix data.
- [ ] Hardware/field validate representative detections where suitable devices or captures are available.

#### Remaining WiFi/BLE checklist

- [ ] Rename user-facing WiFi / BT to WiFi / BLE across UI and production documentation.
- [x] Move the normalized retained observation collection to growable app-owned PSRAM. Implemented by PR #68.
- [x] Remove the fixed 1000-item retained-store and UI-mirror limit. Implemented by PR #68.
- [x] Add a generic internal persistent-record service with atomic system-owned records and descriptor-confined app namespaces; migrate device/app configuration onto that ownership model and remove SD configuration authority. Implemented by PR #77.
- [ ] Add Instant / Timed / Never Wi-Fi and BLE retention.
- [x] Keep surveillance classification on the canonical active observation record, sharing normal device expiry and re-arm semantics without accumulated confidence or a parallel active-surveillance list. Implemented by PR #73.
- [ ] Add active/stale UI semantics for selected objects.
- [ ] Replace MAC-equals-device counting with semantic entities, identity sets, confidence, and provenance.
- [ ] Parse richer Wi-Fi roles/subtypes/metadata and association evidence.
- [ ] Conservatively cluster randomized probe activity where evidence supports it.
- [ ] Add Combined/Wi-Fi/BLE radio selection and radio-specific projections.
- [ ] Add stable relationship drill-down without cursor/selection instability.
- [ ] Define/test memory-pressure eviction behavior favoring current/high-confidence data over stale/transient evidence where appropriate.
- [ ] Prove both collectors stop before all app-owned observation/identity memory is released.
- [ ] Perform repeated enter/exit, large-growth, low-memory, and collector/reclamation race testing on hardware.

### Workstream D: application-by-application improvement pass

Every current application receives an explicit v0.4.0 review rather than treating the memory manager as an isolated infrastructure change.

#### Home / Device Settings

- [ ] Add memory/lifecycle diagnostics or a compact diagnostics entry point.
- [ ] Show free/total PSRAM and largest free block where supported.
- [ ] Show current/peak active-app owned memory.
- [ ] Show the latest transition/reclaimed-byte result where useful.
- [ ] Audit toggles/choices/values against semantic menu behavior.
- [ ] Keep Home a low-allocation baseline application.
- [ ] Derive useful compact capability/status indicators from semantic application availability.

#### CC1101

- [ ] Move eligible history/capture storage to app-owned PSRAM.
- [ ] Add burst duration and inter-burst timing observations.
- [ ] Add conservative modulation/behavior inference without presenting heuristics as decoded identity.
- [ ] Add structured growable capture/history and useful drill-down.
- [ ] Define memory-pressure behavior and prove worker quiesce before memory release.

#### nRF24

- [ ] Move eligible packet/history/classifier storage to app-owned PSRAM.
- [ ] Strengthen passive Enhanced ShockBurst candidate discovery/validation.
- [ ] Populate packet/history surfaces only from validated observations.
- [ ] Grow validated packet/capture history dynamically and improve drill-down/confidence presentation.
- [ ] Preserve strict receive-only behavior: no ACK generation, injection, replay, or active discovery.
- [ ] Prove worker quiesce before packet/history memory release.

#### Recorder

- [ ] Finish the shared calibrated Audio Input service/generalization described later in this roadmap.
- [ ] Separate app-owned PSRAM work buffers from DMA/internal-memory requirements.
- [ ] Release mode/operation scratch at appropriate boundaries while keeping SD recordings independent of runtime reclamation.
- [ ] Prove microphone/playback workers stop before app memory release and validate repeated transitions.
- [ ] Keep speaker/output loudness in percentage semantics and microphone/input digital gain in dB.
- [ ] Validate that recording/save transition vetoes cannot strand, truncate, or restart an active recording.

#### Audio Spectrum

- [x] Keep Audio Spectrum analysis on raw PCM rather than Recorder capture gain; replace the clipping-prone pre-FFT `0..+48 dB` control with continuous high-pass/DC-block filtering and smoothed automatic dB display scaling.
- [ ] Tune automatic range attack/decay and low-frequency cutoff on hardware if field use shows the defaults need adjustment.
- [ ] Add further spectrum controls only where they improve interpretation (for example averaging/smoothing or peak hold) without making the app contract complex.
- [ ] Validate rapid skip-through before delayed activation, repeated entry/exit, and microphone-only operation without SD storage.

#### Thermal

- [ ] Move eligible interpolation/history work buffers to app-owned PSRAM and allocate large work state only while needed.
- [ ] Add a small useful history/statistics inspection feature using dynamic storage.
- [ ] Audit controls against semantic menu behavior and prove acquisition/callback shutdown before release.
- [ ] Implement MLX90640 calibration-data extraction and calibrated per-pixel temperature conversion before presenting absolute temperature values.
- [ ] Validate calibrated temperatures on hardware against a reasonable reference.
- [ ] Evaluate higher-quality interpolation/upscaling while preserving truthful 32x24 sensor resolution and aspect ratio.
- [ ] Define useful palette mapping, automatic range behavior, and optional user-lockable range.
- [ ] Keep min/max/center presentation stable while ranges change.
- [ ] Add sensible filtering and invalid-pixel handling without hiding real transients.

### Workstream E: visual theme and audio-policy separation

**Status:** Implemented by PR #60; release-level combination/configuration validation remains open.

Implemented behavior:

- visual themes are Indoor, Outdoor, and Night;
- Audio Policy is independently Normal or Quiet;
- Quiet suppresses incidental UI/event feedback and audible detection notifications without mutating saved notification configuration;
- explicitly requested content audio such as Recorder playback is not suppressed by Quiet;
- unsupported or invalid current-schema field values retain their compiled defaults and are rewritten canonically;
- incompatible internal configuration schemas are replaced with current compiled defaults rather than migrated or retained as recovery copies.

Outstanding validation:

- [ ] Verify all Indoor/Outdoor/Night + Normal/Quiet combinations.
- [ ] Verify Quiet suppression versus explicit content audio.
- [ ] Verify incompatible-schema/default regeneration.
- [ ] Verify invalid current-schema values retain compiled defaults and theme/audio independence.

### Workstream F: system events, transient history, and notification wake

**Status:** Initial implementation landed in PR #61; Recent Events UI landed in PR #65; internal-persistence/configuration events added by PR #71.

Implemented behavior includes the RAM-only semantic event history/UI, transient attention presentation, power/storage/configuration events, and descriptor-driven application/hardware availability. PR #72 generalizes first-known availability and input/display provider diagnostics while keeping startup discovery silent.

Remaining work:

- [ ] Detect reliable runtime SD insertion/removal and write-failure transitions where the storage stack permits it.
- [ ] Add events for future hot-pluggable peripherals/capabilities that are not represented by the descriptor availability framework.
- [ ] Decide whether SD-backed logs need a dedicated system-event record type beyond ESP diagnostics.
- [ ] Refine event payloads where one compact integer is insufficient without turning events into preformatted strings.
- [ ] Add per-event presentation controls only if practical use demonstrates a real need.
- [ ] Define per-event presentation policy separately for history retention, transient overlay/display wake, and audible feedback; keep startup discovery silent unless field use demonstrates a need.
- [ ] Validate overlay/wake behavior from active, dimmed, and hardware-sleep states.
- [ ] Validate protective power overlays retain priority.
- [ ] Validate rapid event bursts overwrite history safely without creating an audible backlog.

### Cross-application consistency and audio controls

- [ ] Apply semantic menu behavior consistently across all applications.
- [ ] Audit labels for consistent `volume`, `level`, and `gain` semantics.
- [ ] Keep speaker/output loudness user-facing as percentages unless calibration justifies another unit.
- [ ] Keep microphone/input gain in dB.
- [ ] Decide whether notification and Recorder/content playback levels remain independent or participate in an explicit shared/master output policy.
- [ ] If a master output policy is introduced, define composition with Normal/Quiet, environmental gain, content audio, defaults, persistence, clamping, and migration.

### v0.4.0 release validation gate

v0.4.0 is not complete until:

- [ ] All tests/build/format checks pass.
- [ ] App-switch stress testing shows a stable memory baseline, no ownership violations, and zero app-owned allocations after successful exit.
- [ ] Pre-reclamation safety gates prove no worker/callback/timer can reference app memory; forced service-close failures restore only safely restartable platform services before any irreversible cleanup.
- [ ] Recorder recording/save transitions are vetoed until the file is safely finalized; no interrupted recording is reconstructed or restarted after an aborted transition.
- [ ] Audio Spectrum can be skipped before delayed activation without touching microphone hardware, runs without SD storage, and repeatedly enters/exits without leaking service state.
- [ ] Largest free block remains stable enough to rule out systematic fragmentation/leak growth.
- [ ] WiFi/BLE Instant, finite timed, and Never behavior is verified, including full reclamation on exit and memory-pressure paths.
- [ ] Wi-Fi primary device counts are not inflated one-for-one by rotating randomized Probe Request MACs.
- [ ] Wi-Fi AP/client topology and probe relationships are derived from appropriate 802.11 evidence; handshake observation is not required for ordinary associated-client discovery.
- [ ] SSID groupings remain non-unique labels unless separate evidence supports a stronger relationship.
- [ ] Unassociated/probe-only identities remain clearly lower-confidence/transient and conservative clustering does not merge unrelated stable clients.
- [ ] Combined/Wi-Fi/BLE main views and deep dives remain actively updated without destabilizing user selection.
- [ ] BLE views use BLE-native semantics and do not claim AP-like hierarchy or unobserved connections.
- [ ] Rotating Wi-Fi/BLE addresses preserve raw identity/provenance and correlation remains evidence rather than guaranteed physical identity.
- [ ] Supported hardware provides usable PSRAM and fails clearly when the platform requirement is unmet.
- [ ] Recorder DMA/internal-memory requirements remain in compatible memory and audio controls use documented percentage-versus-dB semantics.
- [ ] Thermal absolute-temperature presentation is gated on calibrated MLX90640 conversion and hardware validation.
- [ ] Shared capability/status presentation derives from semantic application availability.
- [ ] Theme selection and Normal/Quiet audio policy operate independently with migration/default behavior verified.
- [ ] System-event history remains bounded/volatile, notifications do not disturb application lifecycle state, and attention wake returns to the original idle timeline.
- [ ] Production documentation reflects implemented behavior and release notes distinguish user-visible changes from internal resource-management work.

## Completed foundation before v0.4.0

The following baseline work is already complete and remains relevant context for later roadmap items:

- **PRs #1-#7:** native display/framebuffer support, board/SPI ownership, flash layout, WiFi/BLE surveillance groundwork, bounded SD logging, shared status presentation, and multi-application lifecycle/navigation.
- **PRs #8-#11:** receive-only CC1101 and nRF24 analyzer baselines.
- **PRs #12-#19:** early audio experiments, shared status/power presentation, unified application logging, review cleanup, and clang-format 18 canonicalization.
- **PRs #20-#24:** device configuration/time, Home-era settings foundation, notifications/audio output, and release packaging.
- **PRs #25-#26:** randomized/private address distinction and IEEE prefix attribution.
- **PR #27:** SD-backed Recorder replacing the former standalone Audio analyzer.
- **PR #28:** shared Audio Input architecture captured for later implementation.
- **PR #29:** shared UI layout/chrome consistency.
- **PR #30:** browser flasher.
- **PR #32:** Home and configurable startup.
- **PR #33:** global visual themes.

## Near-term work after the current v0.4.0 slice

### Shared calibrated Audio Input service

**Priority:** High

PR #70 established the shared audio-capture boundary by moving microphone/recording implementation under `services/audio/` and allowing Recorder and Audio Spectrum to consume the same microphone service. The current provider already uses the deterministic LEFT PDM slot and measures physical PDM clock with PCNT; the remaining work is calibration/generalization.

Goals:

- [x] Move shared I2S0/PDM microphone ownership out of the Recorder app module and behind a reusable audio service boundary. PR #70.
- [x] Keep the board microphone on its deterministic LEFT slot.
- [x] Measure physical PDM clock with PCNT.
- [ ] Configure toward a desired physical PDM outcome rather than assuming an ESP-IDF request produces the requested clock/rate.
- [ ] Use bounded reconfiguration when measured clock indicates the configured request is not producing the desired physical outcome.
- [ ] Clamp requests to known-safe limits and reject measured clocks outside the supported range.
- [x] Measure delivered PCM throughput in the current Recorder/monitor consumers, use valid measured rate for WAV metadata, and use it for live FFT frequency interpretation.
- [ ] Promote requested rate, measured clock, effective rate, calibration result, level, peak, and clipping telemetry into a generalized shared `audio_capture` contract so future consumers do not need Recorder-specific measurement.
- [ ] Generalize fixed digital input-gain policy so Recorder capture and live analysis have explicit, documented persistence/composition semantics.
- [x] Serialize microphone ownership so recording and monitoring cannot independently own I2S0. PR #70 resource-session/audio-capture boundary.
- [x] Keep whole-recording operations such as DC correction, trim, and normalization in Recorder-specific behavior.

Future consumers may include tuner/pitch analysis and microphone-side CW/Morse receive. Those remain distinct from RF-side decoding and are not v0.4.0 release blockers unless separately promoted.

### Log format, levels, and maintained parser

**Priority:** Near term

Rework application/system logging as a common policy rather than letting each feature independently decide how much diagnostic or observational data to emit. Logging levels should provide a practical way to reduce log volume while retaining important operational and decoded/observed records at the selected level.

- define a small, consistent set of logging levels and their intended semantics;
- make the configured log level part of authoritative device configuration;
- classify existing log records by level and tune emitted detail so lowering the level predictably reduces volume rather than disabling arbitrary subsystems;
- keep important application observations, warnings, and failures semantically distinct from verbose/debug diagnostics;
- allow applications such as future CW/Morse receive to emit structured decoded-message records through the common logger when enabled at the selected level;
- keep internal app-browsable history and SD logging separate: a decoded message may be retained internally for browsing and independently mirrored to the SD log when policy and removable storage permit;
- use stable `key=value` fields and quote/escape free-form values containing spaces;
- use stable field naming/order by record type and an explicit schema/version marker if needed;
- maintain a reference parser producing JSON and/or CSV;
- add regression tests for levels, missing/unknown fields, long values, and quoting;
- preserve UTC ISO-8601 timestamps and explicit uptime fallback.

## OTA and release hardening

The A/B updater, Home-owned infrastructure networking, NTP gating, Pages OTA catalog, stable/manual build selection, streaming installer validation, first-boot confirmation, and rollback foundation are implemented. Their current contracts are documented in [Platform architecture](ARCHITECTURE.md), [Home / Device Settings](apps/HOME.md), and the [release runbook](operations/RELEASING.md).

Remaining planning is validation/hardening rather than an updater redesign:

- [ ] Validate leaving Update and leaving Home at each cancellable acquisition/install stage.
- [ ] Validate station/network loss during catalog and firmware acquisition.
- [ ] Validate truncated, corrupt, wrong-size, wrong-SHA, wrong-project, wrong-target, and wrong-layout images remain non-bootable.
- [ ] Validate explicit main/PR/draft/prerelease/release candidate installation on hardware.
- [ ] Validate power loss/reset before, during, and after boot-partition commit and prove rollback behavior.
- [ ] Validate first-boot confirmation does not depend on optional peripherals, SD, Internet, or reopening Update.
- [ ] Validate combined-image recovery flashing for an intentionally incompatible/broken application image.
- [ ] Keep tag/build provenance exact: a release tag must identify the tested source commit and must never be silently moved.
- [ ] Keep browser-flasher and in-device OTA inventory derived from the same validated build metadata.
- [ ] Revisit image signing/authenticity only as an explicit future security design; do not weaken the existing HTTPS + identity/layout + SHA/readback checks in the meantime.

## Medium-term platform work

### Bounded persistent collection/database layer

**Priority:** Platform prerequisite for applications that need small durable browsable histories without removable storage.

The system-owned persistent-record service for bounded named records and configuration is already implemented. The remaining proposal is a higher-level bounded collection/iteration layer for small durable histories so applications still do not know LittleFS paths or filesystem topology.

- [ ] Define collection-owned namespacing/record identity and bounded retention semantics.
- [ ] Provide compact put/get/delete/iterate operations suitable for on-device browsing.
- [ ] Define corruption/recovery behavior without exposing raw filesystem ownership to applications.
- [ ] Keep ordinary configuration, durable collections, SD logging/export, and transient/session PSRAM semantically separate.
- [ ] Validate write endurance and bounded-space behavior for expected low-volume histories.

### CW/Morse application

**Priority:** After the bounded persistent collection/database layer.

**Dependency:** Implement after the collection layer above so receive/decode history can be browsed persistently without requiring an SD card.

- compose transmit strings with the rotary encoder and center button;
- store user-defined transmit message **presets** as ordinary descriptor-owned CW app configuration through the persistent-record service;
- browse/select/add/delete transmit presets through the app UI;
- send complete messages through the shared Morse/speaker service;
- keep detection-notification one-character mappings separate from CW transmit presets;
- if receive/decoding is added, store a small browsable decoded-message history through the bounded persistent collection layer rather than ordinary configuration;
- emit structured decoded-message records through the common logger when receive/decoding is enabled, subject to the configured logging level;
- keep decoded-message SD logging independent from internal app history so the app remains fully useful without removable storage;
- reserve SD-specific files for genuinely bulk/raw capture data if such capture is added later.

### Location and external time authorities

- discover supported external RTCs without silently changing clock authority;
- explicitly offer **Read RTC** or **Set RTC** when an RTC is detected;
- add GPS/GNSS as a shared Location & Time service rather than only a standalone UI;
- use GNSS UTC as an explicit time authority;
- expose fix/location/satellite/accuracy information;
- optionally attach location metadata to observations/captures when enabled;
- [x] add NTP to the explicit Home networking workflow; a successful connection synchronizes UTC before Update becomes available.
- [ ] define longer-term authority transitions among manual, saved, RTC, GNSS, and NTP sources beyond the current one-shot/network-session behavior.

### Diagnostics and self-measurement

Add a dedicated diagnostics surface that reports measured subsystem state rather than only initialization success.

Candidate diagnostics include board/reset identity, flash/PSRAM/heap, SD state/errors, battery/power, WiFi/BLE, CC1101/nRF24, microphone clock/rate/gain/clipping, speaker state, and GPS/RTC state when implemented.

Favor **desired physical outcome -> configure -> measure -> correct/validate** for timing-sensitive peripherals.

### Board/peripheral inventory

Systematically inventory each supported board schematic and implementation so every onboard peripheral, power rail, GPIO, bus device, interrupt, and expansion interface is used, intentionally reserved, or documented as conflicting/unavailable.

### Multi-target build, release, and browser flasher

**Priority:** Medium/platform prerequisite

Current production support is intentionally T-Embed-only. The runtime profile/capability abstraction remains the extension point for a future board, but adding another target should follow a concrete hardware use case and hardware validation rather than being treated as a commitment to restore T-Deck Plus support.

- give every supported board a stable hardware target ID and display name independent of MCU family;
- make board metadata the canonical source for build/package identity;
- package each target into a deterministic, self-describing target directory/manifest;
- include hardware target ID/name, IDF target/chip family, flash assumptions, version, commit, and image metadata;
- allow candidate/release artifacts to contain any number of successfully built targets;
- let the web catalog discover target manifests instead of maintaining a hard-coded board list;
- model the catalog as build/version -> hardware targets -> ESP Web Tools manifest;
- present Hardware and Firmware as separate choices and filter versions by actual target availability;
- keep chip-family compatibility distinct from board identity;
- preserve explicit manual hardware selection for clean-install/bootloader flashing;
- cover candidates, releases, missing targets, and deterministic catalog generation with tests;
- revisit Pages publication concurrency so PR candidates and `main` builds do not unnecessarily replace one another.

## Longer-term applications and capabilities

- NFC/PN532 tools.
- IR analyzer/transmit tools, with transmit behavior explicit and separate from passive monitoring.
- GPS map/location UI beyond the shared Location & Time service.
- Structured export/capture browsing.
- More detailed Wi-Fi channel/utilization and management-frame statistics where hardware APIs provide defensible data.
- Deeper BLE advertisement/manufacturer/service-data interpretation and conservative address-rotation correlation.

## Brainstorming

These are ideas worth preserving, not committed scope or priority.

### ESP32-S3 platform features

- USB OTG / USB Serial-JTAG for maintenance CLI, log export, diagnostics, or explicit USB modes.
- Light/deep-sleep strategy and more deliberate low-power operation.
- Hardware RNG for identifiers/nonces and future cryptographic uses.
- NVS for boot-critical/calibration state that should not depend on SD.
- Persistent watchdog/crash breadcrumbs.
- Internal SoC temperature as a diagnostic measurement, clearly not ambient temperature.
- ADC inventory for battery/power/optional analog measurements.
- RMT for hardware-timed pulse measurement/generation, IR, and OOK/ASK analysis.
- PCNT as a reusable frequency/pulse measurement facility beyond PDM calibration.
- GPTimer for high-quality burst/protocol interval measurement.
- LEDC for hardware PWM/tone/backlight cases where cleaner than software timing.
- Hardware AES/SHA acceleration for authenticated/encrypted logs or future secure communications.
- Secure Boot and flash encryption once field-deployment/recovery policy is mature.
- Preserve a documented JTAG/debug path.

### Security/communications experiments

- Evaluate authenticated/encrypted data exchange using ESP32 hardware crypto.
- Explore phone-paired encrypted audio/data relay concepts only after the basic RFScout platform and audio service are stable.
- Keep key management, threat model, interoperability, and user-controlled recovery explicit before treating any such experiment as a product feature.

### Presentation ideas

- Automatic Indoor/Outdoor/Night selection from ambient light or explicit schedule, with manual override and hysteresis.
- Additional compact data visualizations that preserve readability on the 320x170 display.
- Theme-specific status/icon variants where useful outdoors/night.
- Optional onboarding/help hints that do not permanently consume data-display area.

## Roadmap maintenance rules

- `ROADMAP.md` is the single authoritative planning location, including active release milestones.
- `README.md` is the main current-product/documentation anchor.
- `ARCHITECTURE.md` owns implemented shared platform contracts; `apps/*.md` own implemented app-specific behavior.
- When planned behavior becomes implemented, move the lasting factual description into the owning current-state document and reduce/remove completed design prose here unless it remains necessary context for unfinished work.
- Do not create parallel release-specific roadmap/status documents unless the material genuinely cannot be maintained coherently here.
- Do not leave future-tense design promises in README, Architecture, app pages, Getting Started, Coding Style, or the release runbook.
- Keep speculative/vision material here unless a separate technical research document is genuinely needed for a specialized subject.
- A merged planning-only PR does not mean its feature is implemented; status must distinguish design capture from production code.
