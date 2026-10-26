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

Stack safety is part of the v0.4.0 resource contract. Nontrivial or growth-prone structures should be passed by pointer/reference rather than copied or returned by value when practical, especially across lifecycle, logging, collector, and application boundaries. Large retained state belongs in app-owned PSRAM where appropriate; fixed task stacks should not silently absorb structure growth. Ownership and lifetime should remain explicit so avoiding a stack copy does not introduce ambiguous reachability.

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

Hardware validation should rely on the existing structured RFScout log as the primary evidence source whenever SD logging is available rather than requiring a parallel serial-console transcript. Extend the current logging path to preserve the lifecycle diagnostics already computed by the transition coordinator — sequence, source/destination app, outcome/error, reclaimed bytes, remaining managed allocations, transition duration, free PSRAM, and largest free PSRAM block. Reuse the existing lifecycle diagnostics and JSONL writer rather than creating a second lifecycle-event or logging subsystem. The serial console remains the fallback evidence path when storage is intentionally absent.

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

#### Adaptive passive Wi-Fi scan foundation

**Status:** Implemented by PR #92.

The collector now keeps channels 1-14 in every scan cycle, uses canonical retained AP/BSSID membership to choose short discovery versus longer observation dwell, and selectively interprets useful management/data/control frame roles. The implemented current-state behavior belongs in [WiFi / BLE](apps/WIFI_BLE.md).

This remains only a foundation for the broader semantic AP/client topology work above. Full entity correlation, association confidence/provenance, probe relationships, and stable relationship drill-down remain open.

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

This release should replace the classifier's source-named priority concept with an explicit accumulating evidence-case model while preserving evidence-source reporting. The canonical active observation remains the case lifetime/identity boundary; no parallel active-surveillance list or independent evidence-expiry clock is introduced.

The target strength semantics are:

- **Possible** — credible researched association, with false positives possible.
- **Probable** — strong association, with false positives expected to be uncommon.
- **Distinctive** — evidence intended to be uniquely identifying.

The user-facing **Evidence Threshold** selects a minimum numeric case score. The normal strength levels map Possible / Probable / Distinctive to 2 / 4 / 6, and the implemented opt-in **Everything** mode exposes score-1 matches for detector testing and field validation without changing their evidence strength. Unique matched facts contribute database-defined 1 / 2 / 4 weights; repeated observations of the same fact do not add score, while every distinct matching fact in an observation can accumulate immediately. Cases retain evidence in a generator-sized 32-bit bitset, avoiding hand-maintained evidence-count limits. Product-specific signal values, weights, and provenance belong in `data/surveillance_signatures.csv`, not classifier control flow.

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
- [ ] Add tests proving evidence thresholds, unique-evidence accumulation, duplicate suppression, case expiry/reset, and competing-signature selection.
- [ ] Research and re-score every existing surveillance signature/category against the documented strength contract.
- [ ] Expand detection fingerprints where research supports useful passive evidence, including older/newer hardware or firmware generations.
- [ ] Add negative/near-match tests for researched fingerprints so permissive Possible-strength rules have explicit false-positive boundaries.
- [x] Make the surveillance signature CSV authoritative for signal values, numeric weights, and provenance; generate the compact firmware representation from it.
- [ ] Complete durable provenance review for every curated database fact.
- [ ] Hardware/field validate representative detections where suitable devices or captures are available.

#### Remaining WiFi/BLE checklist

- [ ] Rename user-facing WiFi / BT to WiFi / BLE across UI and production documentation.
- [x] Move the normalized retained observation collection to growable app-owned PSRAM. Implemented by PR #68.
- [x] Remove the fixed 1000-item retained-store and UI-mirror limit. Implemented by PR #68.
- [x] Add a generic internal persistent-record service with atomic system-owned records and descriptor-confined app namespaces; migrate device/app configuration onto that ownership model and remove SD configuration authority. Implemented by PR #77.
- [ ] Add Instant / Timed / Never Wi-Fi and BLE retention.
- [x] Keep surveillance classification on the canonical active observation record, sharing normal device expiry and re-arm semantics without a parallel active-surveillance list. Implemented by PR #73.
- [x] Accumulate evidence from unique matched surveillance facts for the lifetime of that canonical observation; expiry resets the case rather than expiring individual evidence.
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

- [x] Separate frequency discovery from fixed-frequency receive so the spectrum/activity view is a discovery surface rather than the application's core data model.
- [x] Capture asynchronous CC1101 GDO timing with ESP32-S3 RMT/GDMA into a bounded two-entry internal DMA-RAM pool while preserving native RMT symbol semantics.
- [x] Process completed captures zero-copy through a hardware-agnostic analyzer/decoder registry, with compact timing results and an initial repeated 24-bit Princeton decoder.
- [x] Keep decoded, timing-histogram, and raw-waveform presentation independent from acquisition; retain a raw snapshot only when the waveform viewer actually needs it.
- [x] Integrate framework-owned application locking so locked rotary navigation cycles CC1101 Discovery / Decoded / Timing / Waveform surfaces without leaving or rebuilding the CC1101 resource session.
- [x] Remove the global Back input semantic and Back-to-Home fallback; explicit Back rows remain ordinary Select-driven local UI actions.
- [x] Expose the current app-lock state from CC1101 settings while keeping lock ownership in the application manager.
- [ ] Hardware-tune RMT resolution, glitch/idle limits, discovery thresholds, and OOK receive presets across representative fast and slow remotes.
- [ ] Expand receive-only protocol coverage, including useful fixed-code and rolling-code garage/remote families, without adding transmit/replay semantics.
- [ ] Add longer-lived raw capture/history/drill-down only where it has clear value and explicit retention/memory-pressure semantics.
- [ ] Prove RMT callback/worker quiesce, DMA-pool release, overrun/truncation behavior, and stable PSRAM baselines across repeated application transitions.

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
- [x] Validate rapid skip-through before delayed activation without acquiring microphone hardware.
- [x] Validate microphone-only Audio Spectrum operation without SD storage.
- [ ] Validate repeated Audio Spectrum entry/exit with lifecycle diagnostics sufficient to rule out leaked shared audio/service state.

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

- visual themes are Indoor, Outdoor, Night, and Pixel Quest; the shared current presentation contract is documented in [Architecture](ARCHITECTURE.md#visual-presentation);
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

- [x] Detect runtime SD removal when a storage health probe observes media loss and latch storage unavailable for the running session.
- [ ] Add supported hot insertion/remount behavior and reliable write-failure transitions where the storage stack permits it.
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

### Workstream G: application framework isolation and platform services

**Status:** In progress. G1 and G2 are implemented pending hardware validation.

Applications should consume complete, owner-scoped platform services rather than reaching into shared filesystem paths, central configuration structures, or core boot code. Each slice below should leave the documented framework contracts in [ARCHITECTURE.md](ARCHITECTURE.md) true for every existing consumer, not just for the newest application.

#### G1: SD media store

**Status:** Implemented as documented in [ARCHITECTURE.md](ARCHITECTURE.md#media-store-contract); hardware validation open.

Replace hard-coded SD paths and direct stdio/POSIX card access with one storage-owned media store addressed as `/sdcard/<owner>/<collection>/<name>`, where the owner is `system` or an application's descriptor `persistence_key`.

- [x] Add `media_store` with system collections, app-session collections (generation-checked), validated collection/file names, and on-demand directory creation for writes only.
- [x] Provide generation-checked file handles from a fixed pool sized to the FAT open-file limit: open (read / create-exclusive / append / replace), read, write, seek, size, truncate, sync, close.
- [x] Provide stat, remove, list, free-space, availability, and a combined hot-plug presence probe.
- [x] Run every media operation as one bounded transaction under storage I/O arbitration; make the mount point and arbitration lock private to the storage service.
- [x] Track app-owned handles and force-close them during committed app exit; report the count as `media_closed` in transition diagnostics/logs and treat nonzero as a resource-invariant violation.
- [x] Validate descriptor persistence keys as media owner names and reserve `system`.
- [x] Move the SD log writer to `system/logs`.
- [x] Move breadcrumbs to `system/breadcrumbs`.
- [x] Move Recorder recording, enumeration, validation, deletion, and post-processing to the `recordings` app collection, and WAV playback to collection/name addressing.
- [x] Remove `RFSCOUT_STORAGE_MOUNT_POINT`, `RFSCOUT_STORAGE_LOG_DIRECTORY`, and `RFSCOUT_RECORDER_DIRECTORY` from public headers; status/power/Home/Recorder code uses media-store availability instead of storage internals.
- [x] No migration: files written by earlier firmware under `/sdcard/audio/` and `/sdcard/logs/` remain on the card untouched. Release notes must call this out.
- [ ] Hardware-validate logging, breadcrumbs (when a GPS provider is present), Recorder record/save/browse/delete/playback, SD removal while each is active, forced-close diagnostics, and repeated Recorder enter/exit with zero remaining media handles.

#### G2: Hotspots application (Hotspotter parity)

**Status:** Implemented as documented in [Hotspots](apps/HOTSPOTS.md); hardware validation open.

RFScout's port of the standalone Hotspotter project: nearby access points, whether each currently provides Internet connectivity, and a durable, grouped history of the ones that did, adapted to the rotary interface.

Decisions:

- [x] Automatic checking **on by default** (Hotspotter's -80 dBm floor and +4 dB retry gating, strongest first), with a per-visit toggle and manual Recheck.
- [x] **Open, OWE and WPA2/WPA3 personal** networks are eligible; other secured modes are listed as unsupported, and hidden networks are not reported.
- [x] One credential store shared with Home, with stable profile IDs so several profiles can share an SSID, learned associated/rejected BSSIDs, and evidence-based automatic profile selection ([Architecture](ARCHITECTURE.md#shared-network-profiles)).
- [x] Durable SD history of green/yellow results with configuration epochs, movement-triggered locations, background replay and compaction.
- [x] Hotspotter's evidence-based network grouping for Live and History; Live, History and Saved views switched by landing rotary under app lock.
- [x] A fresh random, locally administered station MAC for every check; factory MAC restored when the station session stops.
- [x] Named **Hotspots**, placed after WiFi/BLE in navigation order.
- [x] NTP is required before HTTPS: NTP failure or timeout marks the AP yellow without attempting HTTPS.

Platform prerequisites:

- [x] Station service per-BSSID scan projection with security classification (Home's strongest-per-SSID Network view unchanged).
- [x] Credential-free BSSID-pinned Open/OWE association with optional per-connection MAC randomization.
- [x] Caller-controlled post-association SNTP with observable success (Home keeps automatic NTP).
- [x] Station abort usable from any connecting/connected state.
- [x] Session-managed, cancellable connectivity-probe service (verified HTTPS to Cloudflare `/cdn-cgi/trace`) declared through a descriptor service bit.
- [x] App descriptor, view, registry/config/logging wiring, and documentation page.

Remaining:

- [ ] Hardware-validate open, OWE, captive-portal, NTP-blocked, and no-Internet networks; MAC randomization and factory-MAC restore; rapid enter/exit during each check stage.
- [ ] Hardware-validate WPA2, WPA3 and WPA2/WPA3 transition networks, a same-SSID AP with another passphrase, credential round trips between Home and Hotspots, SD removal during history writes, journal replay with torn and malformed lines, and history with no or stale GPS.
- [ ] Decide whether captive-portal detection (plain-HTTP generate_204-style check) should refine yellow.

#### Hotspots general-purpose service fingerprinting (gateway-only test integration)

- [x] Add an allocation-free, bounded metadata classifier independent of surveillance signatures, with host tests.
- [x] Wire a bounded, cancellable single-HTTP-HEAD metadata probe to the current DHCP gateway only for LIMITED checks; expose tentative CAM tags while retaining internal LIMITED outcomes.
- [ ] Add explicit authorization/opt-in and constrained target selection before probing anything beyond the connected gateway.
- [ ] Validate DNS-SD / WS-Discovery response parsing and build a separately curated fingerprint rule database with provenance and uncertainty.
- [ ] Evaluate authenticated, explicitly requested snapshots for owned/authorized devices as a separate feature; do not silently collect third-party images.

See [Service fingerprinting design](SERVICE_FINGERPRINTING.md) for evidence semantics, lifecycle and acceptance gates.

#### Further framework isolation candidates

Not yet scheduled; recorded so later application work does not deepen these couplings:

- [ ] Descriptor-owned app settings schemas instead of app-specific fields/setters in `device_config` (Recorder volume, Wi-Fi/BLE timeouts and feedback, evidence threshold).
- [ ] Move WiFi/BLE observation handling (`on_observation`, observation logging, store initialization) out of `rfscout.c` and behind the app's activation path.
- [ ] Descriptor-declared hard dependencies (for example SD media, microphone) so availability is derived uniformly instead of in app-specific probe code.

### v0.4.0 release validation gate

v0.4.0 is not complete until:

- [ ] All tests/build/format checks pass.
- [ ] App-switch stress testing shows a stable memory baseline, no ownership violations, and zero app-owned allocations after successful exit.
- [ ] Pre-reclamation safety gates prove no worker/callback/timer can reference app memory; forced service-close failures restore only safely restartable platform services before any irreversible cleanup.
- [ ] Recorder recording/save transitions are vetoed until the file is safely finalized; no interrupted recording is reconstructed or restarted after an aborted transition.
- [x] Audio Spectrum can be skipped before delayed activation without acquiring the microphone.
- [x] Audio Spectrum runs without SD storage while Recorder is unavailable when its storage dependency is absent.
- [ ] Audio Spectrum repeatedly enters/exits without leaking shared audio/service state.
- [ ] When structured lifecycle-transition records are present, they provide enough SD-log evidence to validate outcome/error, reclaimed bytes, zero remaining app allocations, transition timing, free PSRAM, and largest free block without requiring a parallel serial-console capture; missing records remain acceptable under the platform's best-effort logging contract.
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
- [ ] All SD access goes through the media store; no module outside the storage service constructs SD paths, and app exit leaves zero open app media handles.
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

The A/B updater, Home-owned infrastructure networking and automatic NTP attempt, Pages OTA catalog, stable/manual build selection, streaming installer validation, first-boot confirmation, and rollback foundation are implemented. Their current contracts are documented in [Platform architecture](ARCHITECTURE.md), [Home / Device Settings](apps/HOME.md), and the [release runbook](operations/RELEASING.md).

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
- parse NMEA GSV to expose satellites in view separately from GGA satellites used;
- capture GGA HDOP and, where emitted, GSA DOP; report horizontal accuracy only from a defensible receiver accuracy measurement, never from DOP alone;
- add fix type and GPS UTC presentation as supported by receiver messages (per-satellite detail stays out of scope);
- show the receiver's read-back GPS Timepulse state in menus instead of only the saved preference;
- [x] add NTP to the explicit Home networking workflow; a successful connection attempts UTC synchronization, while Update remains available if time sync fails or is cancelled.
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

### Surveillance receiver-location journal and live view

- [x] Track receiver GPS latitude/longitude bounds on canonical active surveillance identities.
- [x] Queue individual GPS-qualified surveillance packet observations for append-only JSONL SD writes.
- [x] Batch journal writes in app poll; remove timeout-triggered persistence and historical replay.
- [x] Render active surveillance devices directly from the canonical store with receiver-bound distances.
- [x] Keep rotary view switching and center-click settings access.
- [ ] Validate SD removal, interrupted writes, low-memory behavior, source-specific aging,
      GPS freshness, classification threshold changes, and application exit on hardware.


## Future Wi-Fi active discovery (design only)

## Planned active Wi-Fi discovery policy (design only; not implemented)

Active Wi-Fi discovery is an **opportunistic supplement** to continuous passive promiscuous packet capture, never a replacement for raw 802.11 capture or a mutually exclusive Passive/Active operating mode. The proposed policy is **enabled** in this design profile, but active Wi-Fi firmware, menu controls, persistence and scheduler are **not** implemented on `main`. Existing firmware continues passive Wi-Fi capture only. When implementing, retain a passive-only default for existing installations unless users explicitly opt into transmitting Probe Requests; the On value below is the requested design configuration, not a deployed default.

### Conditional Wi-Fi menu

```text
Wi-Fi settings
  Channel policy            Auto / Manual
  Channels                  [Manual only]
  Scan order                [Manual only]
  Dwell time                [Manual only]
  Active discovery          On                 [Off / On]
  Active scanning policy    >                  [visible when On]
    Use GPS                 On                 [Off / On]
    Minimum distance        shared Movement trigger [GPS On]
    Stationary duration     10 sec             [GPS On]
    GPS unavailable         Timed fallback     [GPS On; Timed / Skip]
    Minimum interval        120 sec            [GPS Off OR Timed fallback]
    Active duty ceiling     10%                [always shown]
    Back
```

Hide inapplicable controls rather than leaving selectable controls that have no effect. The timed interval setting remains saved when GPS is enabled, even if normally hidden: it governs the configured GPS-unavailable fallback. Keep a bounded number of visible rows and scroll to the selection on the RFScout screen. Source gating continues to apply: these menus are unavailable while that app's Wi-Fi source is disabled. Wardriving must also prevent changes during a recording.

### Policy inputs and defaults

| Setting | Requested design profile | Behavior |
| --- | --- | --- |
| Active discovery | **On** | Authorize occasional wildcard active AP discovery; passive capture remains primary |
| Use GPS | **On** | Prefer location-triggered active discovery with valid, accurate fixes |
| Minimum displacement | **50 m** | Distance from the last completed active scan's accepted location, not consecutive fixes |
| Stationary duration | **10 sec** | Stable stationary interval after sufficient movement |
| GPS unavailable | **Timed fallback** | Use the timed eligibility rule if reliable GPS data is unavailable |
| Minimum timed interval | **120 sec** | Earliest possible subsequent active burst under the timed policy; not an obligation to scan |
| Active duty ceiling | **10%** | Hard upper bound on the fraction of elapsed wall-clock time occupied by active scans, including startup/transition cost |

The duty ceiling applies with **or without GPS**; GPS does not bypass it. Limit each burst's duration and channel coverage, and consider smaller channel batches so passive receive gaps remain short. Enforce the radio's configured regulatory domain for active transmissions. A change of scan policy never resets or expands Wardriving's retained PCAPNG packet count; active discovery results are marked separately from raw captured frames.

### Proposed scheduler state machine

```text
DISABLED
  Active discovery Off -> remain passive; no active scans
  Active discovery On  -> PASSIVE_CAPTURE

PASSIVE_CAPTURE
  Continue normal channel hopping, observation, and raw packet capture
  At a safe passive sweep boundary -> CHECK_ELIGIBILITY

CHECK_ELIGIBILITY
  Wi-Fi source disabled / app owns no radio / recording policy disallows -> PASSIVE_CAPTURE
  GPS On and reliable fix:
    Require displacement >= 50 m from the last *completed* active scan location
    AND stationary for >= 10 sec
  GPS On but unusable fix:
    Timed fallback -> require >= 120 sec since last completed active burst
    Skip -> PASSIVE_CAPTURE
  GPS Off:
    Require >= 120 sec since last completed active burst
  In every case: enforce <= 10% rolling active duty budget,
    minimum cooldown, permitted channels, and capture priority
  Not eligible -> PASSIVE_CAPTURE
  Eligible -> PREPARE_ACTIVE_BURST

PREPARE_ACTIVE_BURST
  Pause the framework's passive channel-hop scheduler at a safe boundary
  Coordinate sole Wi-Fi scan ownership with connection/OTA services
  Configure native ESP-IDF STA active scan without SSID/BSSID targeting
  Setup/ownership failure -> RECOVER_PASSIVE

ACTIVE_BURST
  Native ESP-IDF scan transmits wildcard Probe Requests on allowed channels
  Respect bounded channel batch, duration, timeout and cancellation
  Collect AP records separately; do not label scan records as captured frames
  Completion / timeout / error / app exit -> RECOVER_PASSIVE

RECOVER_PASSIVE
  Stop/cancel scan as needed; restore promiscuous reception and hop ownership
  Reconcile scan-discovered AP observations with source provenance
  Update cooldown/budget, and store last scan location only after success
  If recovery fails, surface radio failure rather than claiming capture resumed
  Successful restoration -> PASSIVE_CAPTURE
  App exit or source disabled -> DISABLED
```

The first eligible burst may occur only after a meaningful passive capture window, never immediately on menu activation. Prevent concurrent ESP-IDF scans, avoid hand-crafted directed probes, and do not invoke active scanning in the promiscuous RX callback. Stationary detection must use multiple GPS samples and position-accuracy uncertainty, not only a speed field, to prevent GPS jitter from generating repeated scans. If a previously scanned location cannot be established, require an initial stationary confirmation plus cooldown before the first GPS-triggered burst. If the system is receiving substantial raw packet traffic, defer active discovery when practical while retaining a bounded retry/deferral policy. An active scan interrupts full-fidelity passive coverage even when promiscuous mode remains technically available during STA scanning; record these gaps if capture-session metadata permits.



## Future framework and power lifecycle coordination (design only)

## Proposed coordinated transition

The power manager requests a **target**; a single framework coordinator performs the transition.
The coordinator tracks phase, target, deadlines, and the services already affected.

1. **Prepare**: ask participating services whether the requested target is safe. An active
   recorder may veto. This phase should avoid irreversible operations.
2. **Quiesce**: in dependency order, stop acquisition and asynchronous callbacks, drain and
   flush writers, persist required state, and make peripherals safe. Return a completion result,
   not merely a fire-and-forget notification.
3. **Commit**: store supported session resume metadata, place presentation hardware in its
   required state, configure wake sources, and invoke the board-specific deep-sleep,
   power-off, or reboot operation.
4. **Abort / rollback**: if preparation or quiescence fails, restore any affected components
   in reverse dependency order where possible; report partial failure and do not proceed to
   sleep with unflushed data. A failed physical power-off must also restore interactive
   display and storage behavior.

A possible *illustrative*, not committed, interface is:

```c
typedef enum {
    SYSTEM_TARGET_DEEP_SLEEP,
    SYSTEM_TARGET_POWER_OFF,
    SYSTEM_TARGET_RESTART
} system_target_t;

typedef struct {
    esp_err_t (*prepare)(system_target_t target);
    esp_err_t (*quiesce)(system_target_t target);
    esp_err_t (*abort)(system_target_t target);
} system_lifecycle_ops_t;
```

Precise ownership, callback signature, registration, asynchronous completion,
timeout policy, error propagation and dependency declarations must be settled
before implementation. Do **not** add a second application lifecycle or treat
the example above as an ABI.

### Why not only use system events?

State notifications such as *sleep requested*, *shutting down*, or *transition aborted*
are useful for diagnostics and observability. But broadcasts cannot guarantee that
storage was flushed or peripheral activity stopped. Operations requiring confirmation
must run through the coordinator; notifications may describe their outcome.

## CASIC GPS candidate behavior

The current GPS service uses NMEA for fixes and CASIC commands for receiver policy,
including baud and navigation update-rate configuration. Potential CASIC-controlled
navigation stop/start belongs to the GPS service implementation, **not** `power.c`.

Before introducing receiver control:

- Verify the HT1818Z3G5L's exact supported command, response/ACK and stop/wake semantics
  from module documentation and on-device tests. Do not conflate a navigation stop command
  with a verified low-current sleep state.
- Determine whether wake is UART-command-driven, requires a reset/power cycle, or depends
  on receiver wiring. The installed configuration has no separate power-control line.
- Measure receiver current in active, stopped, and restarted states. Document whether
  backup data and fast reacquisition survive each transition.
- Preserve GPS/NMEA presence detection, GPS-versus-nRF24 UART ownership/arbitration,
  the baud-probing sequence and valid-fix requirements.
- On failed or unsupported CASIC commands, maintain safe generic NMEA behavior; never
  block system shutdown indefinitely waiting for GPS.
- On ordinary app switches, retain system GPS service ownership unless the framework's
  service policy specifically requires stopping it.

Receiver timepulse configuration belongs to GPS Timepulse (service-owned, shared Home/GPS On/Off); directional ring configuration is GPS-app-only. Future experiments should
not be interpreted as evidence of power-saving or guaranteed LED behavior.

## Implementation strategy (future work)

1. Agree on these semantics and the relationship to existing `app_session` transitions.
2. Inventory every system-owned service and existing shutdown-veto/flush contract.
3. Introduce coordinator tests for ordering, veto, timeout, failure and rollback, including
   failed physical power-off, unavailable SD media, and Recorder activity.
4. Migrate existing `power.c` cleanup into registered participants without changing
   display-only idle or hardware-capability gating.
5. Add an optional GPS participant; only then introduce validated CASIC stop/start commands.
6. Treat resumable light sleep as a separate design decision; avoid introducing a
   general-purpose `resume()` lifecycle method prematurely.

## Open design questions

- Should services register static dependencies, or should board profiles provide the order?
- How are long-running asynchronous drains signaled and cancelled within a bounded deadline?
- Which service failures should veto sleep, and which should only be logged?
- Is recovery from a failed quiesce always possible, or must a degraded-state policy exist?
- How should a future light-sleep wake map onto `start()` versus an explicit reversible suspend?
- What happens when the GPS is electrically powered while the ESP32 enters deep sleep?



### Hotspots Live admission: validation follow-up

The four-tier Hotspots-only capacity admission policy is implemented in the
application model; see [Hotspots](apps/HOTSPOTS.md#capacity-protection) for the
current contract. Before shipping, validate 128-entry saturation with dense
real-world Wi-Fi captures, including whether equal-tier replacements in one
sweep cause excessive churn. If churn is observed, choose an explicit, minimal
same-tier stability rule rather than an app setting or shared admission engine.
Keep profile evidence fresh enough for admission without blocking receive
callbacks; never copy saved passwords into Live table entries. Existing manual
checks and independent normal time/GPS expiration must remain protected.
