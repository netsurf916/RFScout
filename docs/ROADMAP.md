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

**Status:** Transactional foundation implemented by PR #64; simplified resource-session model implemented by PR #68; hardware stress/fault-injection validation remains open.

Application changes now use a single platform-owned resource-session boundary rather than parallel app-specific reachability protocols:

1. Ensure the source application has a resource session.
2. Preserve any user-visible semantic rollback state the platform cannot infer, such as the exact active Recorder operation.
3. Ask the session to stop/quiesce every platform service associated with the source app.
4. Abort without reclaiming memory and restore the source service set if any service cannot quiesce safely.
5. Treat successful session close as the platform reachability proof; applications do not implement separate worker/callback reachability callbacks.
6. Commit irreversible app-visible exit cleanup.
7. Reclaim all remaining memory owned by the source application and verify zero managed allocations remain.
8. Create the destination session before the destination app begins using app-scoped resources.

PR #68 also removes duplicate Wi-Fi/BLE, CC1101, and nRF24 stop/restart bookkeeping from `app_manager`; service failure/recovery semantics now live with the platform session. Service membership is platform-defined rather than negotiated through a public registration API or arbitrary service-slot quota.

Outstanding validation includes rapid app rotation, repeated Home/app cycles, large histories, Recorder transitions, Thermal attach/detach, forced service-quiesce failures, callback/timer races, low-memory behavior, baseline-memory stability, and largest-free-block stability.

### Workstream C: dynamic WiFi/BLE retention and observation model

**Status:** In progress. PR #68 begins the application-data migration onto Workstreams A/B and establishes the simplified session abstraction across current apps.

PR #68 implements the first storage/lifecycle slice:

- remove the boot-time fixed 1000-entry retained observation allocation;
- allocate retained Wi-Fi/BLE observations lazily in app-owned PSRAM;
- grow observation capacity geometrically as observations arrive rather than imposing a fixed item ceiling;
- reclaim the observation collection through the WiFi/BLE resource-session close after both collectors have drained;
- make the session the sole platform-service quiescence/restart authority for WiFi/BLE, CC1101, nRF24, and Recorder safety;
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
- [ ] Move retained surveillance history to growable app-owned memory.
- [ ] Add Instant / Timed / Never Wi-Fi and BLE retention.
- [ ] Apply equivalent semantics to surveillance retention where appropriate.
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

- [ ] Implement the shared calibrated Audio Input service described later in this roadmap.
- [ ] Separate app-owned PSRAM work buffers from DMA/internal-memory requirements.
- [ ] Release mode/operation scratch at appropriate boundaries while keeping SD recordings independent of runtime reclamation.
- [ ] Prove microphone/playback workers stop before app memory release and validate repeated transitions.
- [ ] Keep speaker/output loudness in percentage semantics and microphone/input digital gain in dB.
- [ ] Revisit the current `0..+24 dB` input-gain range against measured hardware behavior.

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

**Status:** Implemented by PR #60; release-level combination/migration validation remains open.

Implemented behavior:

- visual themes are Indoor, Outdoor, and Night;
- Audio Policy is independently Normal or Quiet;
- Quiet suppresses incidental UI/event feedback and audible detection notifications without mutating saved notification configuration;
- explicitly requested content audio such as Recorder playback is not suppressed by Quiet;
- legacy Discipline migrates to Night + Quiet with the intended migrated environmental gains;
- recognized older configuration is migrated best-effort while unsupported/unusable pre-1.0 configuration may be regenerated from current defaults.

Outstanding validation:

- [ ] Verify all Indoor/Outdoor/Night + Normal/Quiet combinations.
- [ ] Verify Quiet suppression versus explicit content audio.
- [ ] Verify recognized schema migration/default regeneration.
- [ ] Verify migrated gains and complete theme/audio independence.

### Workstream F: system events, transient history, and notification wake

**Status:** Initial implementation landed in PR #61; Recent Events UI landed in PR #65.

Implemented behavior includes:

- shared semantic system-state event IDs on the existing event dispatcher;
- fixed-size RAM-only recent-event history independent from normal diagnostics;
- charger connect/disconnect, startup SD mount/mount-failure, and Thermal availability events;
- transient global event overlay without changing application lifecycle state;
- notification-driven display attention wake preserving the original inactivity deadline;
- common acquired/lost procedural cues under Normal/Quiet audio policy;
- boot-state discovery retained/logged without interactive notification behavior;
- optional default-disabled System Events application with stable browse snapshot.

Remaining work:

- [ ] Detect reliable runtime SD insertion/removal and write-failure transitions where the storage stack permits it.
- [ ] Add further hot-pluggable peripheral/capability events as hardware support expands.
- [ ] Decide whether SD-backed logs need a dedicated system-event record type beyond ESP diagnostics.
- [ ] Refine event payloads where one compact integer is insufficient without turning events into preformatted strings.
- [ ] Add per-event presentation controls only if practical use demonstrates a real need.
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
- [ ] Pre-reclamation safety gates prove no worker/callback/timer can reference app memory; forced failures fully restore the source app before irreversible cleanup.
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

The microphone is a board resource and should not remain Recorder-specific.

Goals:

- move I2S0/PDM ownership out of `recorder.c` into a shared service;
- keep the board microphone on its deterministic LEFT slot;
- configure toward a desired physical PDM outcome rather than assuming an ESP-IDF request produces the requested clock/rate;
- measure physical PDM clock with PCNT and use bounded reconfiguration when necessary;
- clamp requests to known-safe limits and reject measured clocks outside the supported range;
- measure delivered PCM throughput and expose an effective sample rate;
- expose requested rate, measured clock, effective rate, calibration result, level, peak, and clipping telemetry;
- centralize fixed digital input gain so Recorder and live analysis consume the same post-gain stream;
- serialize microphone ownership so recording and monitoring cannot independently open I2S0;
- keep whole-recording operations such as DC correction, trim, and normalization in Recorder;
- make FFT frequency interpretation use the measured effective PCM rate.

Future consumers may include tuner/pitch analysis and microphone-side CW/Morse receive. Those remain distinct from RF-side decoding and are not v0.4.0 release blockers unless separately promoted.

### CW/Morse sender application

**Priority:** Near term

- compose strings with the rotary encoder and center button;
- persist user-defined strings in `/sdcard/rfscout.json`;
- browse/select/add/delete saved messages;
- send complete messages through the shared Morse/speaker service;
- keep detection-notification one-character mappings separate from CW messages.

### Log format and maintained parser

**Priority:** Near term

- stable `key=value` fields;
- quote/escape free-form values containing spaces;
- stable field naming/order by record type;
- explicit schema/version marker if needed;
- maintained reference parser producing JSON and/or CSV;
- regression tests for missing, unknown, long, and multi-word values;
- preserve UTC ISO-8601 timestamps and explicit uptime fallback.

## Medium-term platform work

### Location and external time authorities

- discover supported external RTCs without silently changing clock authority;
- explicitly offer **Read RTC** or **Set RTC** when an RTC is detected;
- add GPS/GNSS as a shared Location & Time service rather than only a standalone UI;
- use GNSS UTC as an explicit time authority;
- expose fix/location/satellite/accuracy information;
- optionally attach location metadata to observations/captures when enabled;
- add NTP only under explicit networking policy;
- define authority transitions among manual, saved, RTC, GNSS, and NTP sources.

### Diagnostics and self-measurement

Add a dedicated diagnostics surface that reports measured subsystem state rather than only initialization success.

Candidate diagnostics include board/reset identity, flash/PSRAM/heap, SD state/errors, battery/power, WiFi/BLE, CC1101/nRF24, microphone clock/rate/gain/clipping, speaker state, and GPS/RTC state when implemented.

Favor **desired physical outcome -> configure -> measure -> correct/validate** for timing-sensitive peripherals.

### Board/peripheral inventory

Systematically inventory each supported board schematic and implementation so every onboard peripheral, power rail, GPIO, bus device, interrupt, and expansion interface is used, intentionally reserved, or documented as conflicting/unavailable.

### Multi-target build, release, and browser flasher

**Priority:** Medium/platform prerequisite

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