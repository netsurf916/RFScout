# RFScout Roadmap

This is the canonical planning document for RFScout. Production documentation describes implemented behavior; future design, priorities, follow-up work, and brainstorming belong here so intent is not scattered across operational docs.

Statuses describe implementation state, not release state. Active release planning is maintained here rather than in release-specific roadmap files.

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

## In-device OTA update architecture

**Status:** A/B flash/rollback, separate Home-owned Network and Update menus, remembered-network persistence, passive AP discovery, explicit STA association, automatic NTP synchronization, deterministic Home-session teardown, embedded build identity, Pages OTA catalogs, stable/manual build selection, streaming installation, and first-boot rollback confirmation are implemented on the OTA development branch. Hardware validation remains.

**Design goal:** Preserve RFScout's passive-by-default behavior and strict application/service lifecycle boundaries while allowing a user to deliberately obtain temporary Internet connectivity, check for a release, install an application image into the inactive OTA slot, and recover automatically if the new image does not boot successfully.

### Home-owned Network and Update menus

Firmware update is a nested **Home / Device Settings → Update** menu rather than a top-level application. Infrastructure networking exists only while that menu is in use; RFScout does not gain a device-wide "known networks" or always-connected policy.

Opening the Network menu must not associate with an access point. While disconnected, it may perform passive AP scans and present remembered networks that have been observed in range. Passive scan results are volatile Home-session state. Association is always a deliberate user action. After DHCP succeeds, the infrastructure station automatically starts SNTP and updates the authoritative device clock when synchronization completes.

The initial UI contract is:

- show the running firmware version and current OTA-network state;
- show remembered networks that have been observed by a passive scan, including useful signal information;
- selecting a visible remembered network opens an action screen rather than connecting immediately;
- the network action screen offers at least **Connect**, **Forget**, and **Back**;
- provide a network-management/add flow for entering credentials for a new SSID;
- after an explicit connection succeeds, expose **Check for update**;
- after a compatible release is discovered, show the current and target versions and require an explicit install action;
- download/install progress is presented inside the Update menu and makes cancellation semantics clear.

The Update menu may retain credentials for multiple access points. Those credentials are application-owned durable state under the update feature's persistence identity, not fields in device-wide configuration and not state owned by the Wi-Fi service. The Update menu chooses which credential set to offer/use based on its own saved list and current passive scan results. No automatic association occurs merely because a remembered AP is visible.

Hidden-network support is not required initially because discovering a hidden SSID generally requires active probing or a direct connection attempt. It can be added later as an explicit user action without changing the passive default.

### Update-menu lifecycle boundary

The infrastructure station is owned by the Home app session rather than either nested menu. Leaving Network or Update preserves the connection so the user can configure networking and then enter Update without reconnecting. An explicit Disconnect or leaving Home tears down the station; leaving Update independently stops OTA acquisition/installation.

When the Update menu closes or Home exits, teardown must:

1. stop/cancel passive scanning owned by the Update session;
2. cancel any manifest or firmware HTTP request;
3. abort an in-progress OTA write before it is made bootable;
4. disconnect the infrastructure STA session;
5. release Wi-Fi/network resources through the normal service/session teardown path;
6. clear transient credential/password copies and scan/manifest/download state; and
7. leave only the intentionally persisted remembered-network configuration.

A firmware download is therefore cancelable by leaving Update or Home. Partial data in the inactive OTA slot is not resumable state and may be overwritten from the beginning on the next attempt. The currently running partition remains the boot target until the installer has completely received and validated the candidate and enters the final commit/reboot phase.

The installer must avoid a long-lived "staged for next reboot" state. After final validation, selecting the new boot partition and restarting should be treated as one tightly coupled terminal operation. Before that commit point, menu exit can cancel safely; after it, ordinary app navigation is no longer meaningful because reboot is immediate.

### Network ownership and abstraction

Credential ownership, connection policy, and user intent belong to the Home Network menu. Low-level infrastructure Wi-Fi and SNTP belong behind semantic service/provider boundaries.

The Network menu may ask that station service to:

- perform a passive AP scan;
- establish a STA connection using one credential set supplied for the current Home session;
- report connection/DHCP status; and
- disconnect/close.

The Wi-Fi service must not maintain a global remembered-network database or silently reconnect after Home exits. Likewise, the Network and Update menus must not take ownership of driver handles, event-loop internals, DHCP/SNTP implementation details, or hardware-specific Wi-Fi teardown.

The existing WiFi / BT observation application remains a separate passive-monitoring concern. Infrastructure STA operation must acquire/quiesce shared Wi-Fi resources through the platform lifecycle rather than reaching into the observation app or allowing monitor and OTA modes to conflict implicitly.

### GitHub Pages OTA publication

OTA is published as part of RFScout's existing GitHub Pages deployment. The device fetches:

`https://netsurf916.github.io/RFScout/ota/catalog.json`

GitHub Releases and successful Actions candidate artifacts remain the source builds. Pages creates one atomic deployment containing the browser flasher and an OTA catalog derived from the **same build inventory**:

```text
/
    index.html
    catalog.json
    firmware/
        releases/<tag>/...
        candidates/main/...
        candidates/pr-<number>/...
    ota/
        catalog.json
```

The OTA catalog points at the application-only image already mirrored under each build's `firmware/...` directory. It does not duplicate firmware bytes into a separate OTA release tree. Builds are eligible for the device catalog only when they carry valid `ota-image.json` metadata whose version and packaged build commit agree with the browser-flasher entry and whose project, hardware target, ESP-IDF target, partition-layout generation, application size, and SHA-256 pass publication-time validation. Pull-request entries retain the PR head separately as source provenance while integrity remains tied to the merge-ref commit that produced the tested artifact.

Eligible OTA entries come from the current `main` candidate, current open-PR candidates, draft releases, prereleases, and published releases when those builds are present in the web flasher and carry compatible OTA metadata. Pages serializes at most 8 KiB, retaining the newest official stable releases first and then the newest remaining eligible builds, so lower-priority entries may be omitted when the catalog is full. The normal **Check for update** path filters the retained catalog to published stable releases and selects the newest compatible SemVer. The explicit/manual build-selection UI allows choosing an exact retained main, PR, draft, prerelease, or published build without applying stable-version ordering.

### Running-version identity

The updater must not infer its version from filenames, Git state, or hard-coded UI text. The running image is authoritative. Release and candidate builds embed their package version into ESP-IDF application metadata, and packaging verifies that metadata before producing artifacts.

Stable-channel ordering applies only to semantic release versions. Candidate identifiers remain valid explicit install targets but do not participate in SemVer ordering.

### Pre-1.0 compatibility policy

RFScout does not maintain a historical OTA compatibility graph before `v1.0.0`. Once this OTA mechanism reaches `main`, the project may reset the development release line and remove older releases that are incompatible with the current OTA contract. Thereafter, builds exposed by the web flasher are expected to derive from the current compatible main line.

If a later pre-1.0 change deliberately breaks the OTA/partition contract, incompatible old releases should again be removed from the active published set and affected devices should use the combined clean-install/recovery image. The device still performs hard sanity checks rather than trusting catalog hygiene alone.

Stable selection therefore requires only:

1. valid catalog schema;
2. exact RFScout project, supported hardware target, ESP-IDF target, and partition-layout generation;
3. a published stable release entry;
4. a valid semantic target version newer than the running semantic version; and
5. validated artifact path, size, and SHA-256 metadata.

Explicit/manual selection does not require the target to be newer and does not interpret candidate identifiers as semantic versions. It still requires all hard project/hardware/layout/image sanity checks.

Normal in-device OTA consumes only the application image. The combined `0x0` image remains the clean-install/recovery artifact and is the required mechanism for incompatible partition-layout changes.

The first implementation does not require firmware signing. HTTPS certificate validation is therefore the initial authenticity boundary, while SHA-256 and size protect transfer/storage integrity. The schema remains extensible so image signatures can be added later without redesigning acquisition or installation.

### Partition-layout generation

Each OTA-compatible release identifies the partition-layout generation it expects, beginning with `ab-v1`. Normal OTA must reject a candidate whose layout generation does not match the running firmware's layout.

The application image cannot safely redefine the bootloader, partition table, or OTA-data placement that governs the currently running updater. A future partition-layout migration therefore requires either:

- an explicitly designed/tested migration mechanism with its own recovery guarantees; or
- a clean/manual combined-image installation.

It must never be smuggled through the ordinary app-image OTA path.

### Installer and A/B partition ownership

The Update UI must not know flash offsets, partition labels, OTA sequence counters, or which physical slot is called `ota_0` versus `ota_1`. Those are platform/update-service implementation details.

A semantic updater/installer service owns the ESP-IDF OTA API and exposes operations such as begin/write/finish/abort/progress without exposing `esp_partition_t` or raw flash addresses to the UI.

For an install, that service:

1. asks ESP-IDF for the currently running application partition;
2. asks `esp_ota_get_next_update_partition(NULL)` for the eligible update destination;
3. verifies that a valid non-running OTA destination exists and that the declared image can fit;
4. begins the OTA write with `esp_ota_begin()`;
5. streams the HTTPS response directly into the OTA handle with `esp_ota_write()` while computing SHA-256 and byte count;
6. requires the final byte count and streaming SHA-256 to match the catalog and completes ESP-IDF image validation with `esp_ota_end()`;
7. reads the completed image metadata from the inactive partition and requires its embedded RFScout project/version identity to agree with the selected catalog entry;
8. reads back exactly the declared application-artifact byte count from the destination partition, computes SHA-256 over those stored bytes, and requires that flash-readback SHA-256 to match the catalog;
9. only after streaming integrity, ESP-IDF image validation, and flash-readback integrity all succeed, marks that destination as the next boot image with `esp_ota_set_boot_partition()`; and
10. immediately enters the platform reboot path.

With the current two-slot layout, ESP-IDF's "next update partition" policy naturally resolves to the inactive slot; RFScout code does not encode that fact by offset or slot name. OTA-data state remains owned by ESP-IDF/bootloader logic.

Errors before the boot-partition commit abort the OTA handle and leave the currently running application selected. Leaving the Home Update menu during download follows the same abort path.

### First-boot confirmation and rollback

Rollback confirmation is a boot/platform responsibility, not an Update-menu responsibility. The new image must be able to prove basic operability even if the user never opens the Update menu again.

With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, `esp_ota_set_boot_partition()` marks a newly selected image for first-boot verification. Early common startup must inspect the running OTA partition state. When it is `ESP_OTA_IMG_PENDING_VERIFY`, RFScout runs a bounded core-health check and then either:

- calls `esp_ota_mark_app_valid_cancel_rollback()` after the image has demonstrated minimum viable operation; or
- calls `esp_ota_mark_app_invalid_rollback_and_reboot()` when the new image is known to be unusable.

An unexpected reset/power loss before confirmation intentionally allows the ESP-IDF bootloader to reject the unconfirmed image and return to the previous working slot.

The confirmation test should prove RFScout can reach a stable core runtime on the supported hardware, but it must not depend on optional/hot-plug peripherals, Internet availability, an OTA-server response, or opening the Update menu. The exact health checklist is separate implementation/test work and should be conservative enough to avoid accepting a fundamentally broken build without turning an unrelated optional-device failure into a firmware rollback.

The updater must not start another OTA while the currently running image is still pending verification. ESP-IDF already rejects that state; RFScout should surface it as a platform/update-state error rather than attempting to work around it.

### Manifest/image validation

Before a candidate can become bootable, validation must cover at least:

- supported manifest schema and selected build identity/provenance;
- exact RFScout project/hardware target;
- ESP-IDF chip target;
- matching partition-layout generation;
- safe relative artifact path under the OTA origin;
- declared image size that fits the selected OTA partition;
- received byte count equal to declared size;
- streaming SHA-256 equal to the catalog value;
- post-write flash-readback SHA-256 over exactly the declared artifact byte count equal to the catalog value;
- valid ESP application image structure; and
- embedded application version/project metadata consistent with the catalog.

Unknown optional manifest fields should be ignored when schema v1 semantics remain intact; an unsupported schema version is a hard failure.

### Implementation slicing

Keep the feature reviewable and preserve abstraction boundaries by landing it in small pieces on the OTA integration branch:

1. document/finalize manifest, version, partition, rollback, and app/network ownership contracts;
2. **implemented on the OTA development branch:** embed authoritative candidate/release version metadata, verify it during packaging, and generate immutable OTA release metadata;
3. **implemented on the OTA development branch:** add the Home-owned Update menu plus OTA-owned remembered-network persistence and reusable credential/text-entry UI;
4. **implemented on the OTA development branch:** add session-scoped passive scan and explicit infrastructure-STA connect/disconnect support behind a semantic service, including explicit SSID-targeted active search for remembered networks not seen passively;
5. **implemented on the OTA development branch:** add explicit static HTTPS catalog retrieval, hard build-identity/layout validation, stable-release selection, explicit main/PR/draft/prerelease/release build browsing, cancellation on Update-menu/Home exit, and Update UI reporting;
6. **implemented on the OTA development branch:** add the transport-neutral installer abstraction and ESP-IDF A/B streaming implementation, including exact byte count, streaming SHA-256, ESP-IDF image validation, embedded project/version checks, flash-readback SHA-256, boot commit, and immediate reboot;
7. **implemented on the OTA development branch:** add boot-time pending-image confirmation after bounded mandatory common startup, preserving automatic rollback across crash/reset/power loss before confirmation;
8. **implemented on the OTA development branch:** derive the device OTA catalog from the same main/PR/release inventory used by the atomic GitHub Pages browser-flasher deployment; and
9. validate cancellation, leaving Update/Home, app switching, network loss, truncated/corrupt images, power loss at each OTA stage, explicit candidate installs, rollback, and recovery flashing on hardware.

## Medium-term platform work

### System-owned internal persistent record/database service

**Priority:** Platform prerequisite for applications that need small durable browsable histories without removable storage.

Build a compact service over internal persistence that owns record layout, bounds/retention, durability, and iteration so applications do not know LittleFS paths or filesystem topology. It should support small app-specific durable collections while keeping configuration, removable bulk files, and transient/session PSRAM data semantically separate.

- [ ] Define service-owned namespacing/record identity and bounded retention semantics.
- [ ] Provide compact put/get/delete/iterate operations suitable for on-device browsing.
- [ ] Define corruption/recovery behavior without exposing raw filesystem ownership to applications.
- [ ] Keep SD logging/export optional and separate from authoritative internal record state.
- [ ] Validate write endurance and bounded-space behavior for expected low-volume records.

### CW/Morse application

**Priority:** After the system-owned internal persistent record/database service.

**Dependency:** Implement after the internal record/database service above so receive/decode history can be browsed persistently without requiring an SD card.

- compose transmit strings with the rotary encoder and center button;
- store user-defined transmit message **presets** as ordinary CW app configuration in authoritative `rfscout.json`;
- browse/select/add/delete transmit presets through the app UI;
- send complete messages through the shared Morse/speaker service;
- keep detection-notification one-character mappings separate from CW transmit presets;
- if receive/decoding is added, store a small browsable decoded-message history through the system-owned internal record service rather than configuration;
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

- `ROADMAP.md` is the single canonical planning location, including active release milestones.
- When a planned feature becomes implemented, move factual behavior into the appropriate production documentation and mark the roadmap item complete here.
- Do not create parallel release-specific roadmap/status documents unless the material genuinely cannot be maintained coherently here.
- Do not leave future-tense design promises in README, Architecture, Capabilities, Configuration, SD-card, or Release docs.
- Keep speculative/vision material here unless a separate technical research document is genuinely needed for a specialized subject.
- A merged planning-only PR does not mean its feature is implemented; status must distinguish design capture from production code.
