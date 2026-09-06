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

RFScout applications should behave like isolated consumers of a common pool of dynamic memory. Long-lived platform services keep only memory that genuinely must survive application changes; active applications may dynamically consume available PSRAM; application exit must quiesce platform services before app-owned memory is reclaimed; and the next application should start from a predictable memory baseline. PSRAM remains a platform requirement for supported RFScout hardware targets.

### Workstream A: application-scoped PSRAM memory manager

**Status:** Implemented by PR #63.

- [x] Explicit platform/service, application, and nested operation ownership.
- [x] PSRAM-only managed allocation for eligible application storage.
- [x] Safe growth, ownership validation, accounting, and bulk reclamation.
- [ ] Continue hardware stress and memory-pressure validation.

### Workstream B: deterministic app-exit reclamation

**Status:** Transactional foundation implemented by PR #64; simplified resource-session model implemented by PR #68; descriptor-driven runtime and simplified semantic-veto model implemented by PR #70; hardware stress/fault-injection validation remains open.

Application changes use the platform-owned resource-session boundary. Hard service dependencies are descriptor-managed and quiesced before app memory is reclaimed. Optional facilities remain operation-time decisions owned by the relevant service rather than becoming descriptor-level filesystem/capability topology.

### Workstream C: dynamic WiFi/BLE retention and observation model

**Status:** In progress.

- [x] Move normalized retained observations to growable app-owned PSRAM. PR #68.
- [x] Remove the fixed 1000-item retained-store and UI-mirror limit. PR #68.
- [ ] Add Instant / Timed / Never Wi-Fi and BLE retention.
- [ ] Apply equivalent semantics to surveillance retention where appropriate.
- [ ] Add active/stale UI semantics and richer Wi-Fi/BLE entity/correlation views.
- [ ] Preserve confidence/provenance for inferred identity relationships.
- [ ] Perform repeated enter/exit, large-growth, low-memory, and collector/reclamation race testing on hardware.

### Workstream D: application-by-application improvement pass

Every current application receives an explicit v0.4.0 review rather than treating the memory manager as isolated infrastructure.

#### Home / Device Settings

- [ ] Add useful memory/lifecycle diagnostics.
- [ ] Keep Home a low-allocation baseline application.
- [ ] Derive compact capability/status indicators from semantic application availability.

#### CC1101

- [ ] Move eligible history/capture storage to app-owned PSRAM.
- [ ] Add burst duration/inter-burst observations and structured drill-down.
- [ ] Prove worker quiesce before memory release.

#### nRF24

- [ ] Move eligible packet/history/classifier storage to app-owned PSRAM.
- [ ] Strengthen passive Enhanced ShockBurst candidate discovery/validation.
- [ ] Preserve strict receive-only behavior and prove worker quiesce before release.

#### Recorder

- [ ] Finish shared calibrated Audio Input generalization.
- [ ] Separate app-owned PSRAM work buffers from DMA/internal-memory requirements.
- [ ] Keep SD recordings independent of runtime reclamation.
- [ ] Validate recording/save transition vetoes and repeated transitions.

#### Audio Spectrum

- [ ] Validate the current `0..+48 dB` live digital input-gain range.
- [ ] Decide whether Spectrum and Recorder gain should share persisted settings.
- [ ] Validate rapid skip-through, repeated entry/exit, and microphone-only operation without SD.

#### Thermal

- [ ] Move eligible interpolation/history work buffers to app-owned PSRAM.
- [ ] Implement and validate calibrated MLX90640 temperature conversion.
- [ ] Improve interpolation, palette/range behavior, filtering, and invalid-pixel handling without overstating sensor resolution.

### Workstream E: visual theme and audio-policy separation

**Status:** Implemented by PR #60; release-level validation remains open.

- visual themes are Indoor, Outdoor, and Night;
- Audio Policy is independently Normal or Quiet;
- the legacy `discipline` token is normalized to Night + Quiet when encountered inside an otherwise current-schema configuration;
- incompatible internal configuration schemas are replaced with current compiled defaults rather than migrated or retained as recovery copies.

### Workstream F: system events, transient history, and notification wake

**Status:** Initial implementation landed in PR #61; Recent Events UI landed in PR #65; internal-persistence/configuration events added by PR #71.

Implemented behavior includes shared semantic events, bounded RAM-only history, startup hardware/storage discovery, internal-storage mount/recovery status, configuration import/default recovery, transient overlays, notification-driven attention wake, and the optional System Events application.

Remaining work includes runtime removable-storage transitions where reliable, further hot-pluggable capability events, payload refinement where needed, and release-level overlay/wake/burst validation.

### v0.4.0 release validation gate

- [ ] All tests/build/format checks pass.
- [ ] App-switch stress testing shows a stable memory baseline and zero app-owned allocations after successful exit.
- [ ] Recorder transition vetoes, Audio Spectrum no-SD operation, WiFi/BLE retention/correlation behavior, Thermal calibration, theme/audio independence, and System Events behavior are validated on hardware.
- [ ] Production documentation reflects implemented behavior.

## Completed foundation before v0.4.0

The following baseline work remains relevant context for later roadmap items:

- **PRs #1-#7:** display/framebuffer, board/SPI ownership, flash layout, WiFi/BLE surveillance groundwork, bounded SD logging, status presentation, and multi-application lifecycle/navigation.
- **PRs #8-#11:** receive-only CC1101 and nRF24 analyzer baselines.
- **PRs #12-#33:** audio experiments/services, configuration/time, notifications, release packaging, address attribution, Recorder, shared UI, browser flasher, Home/startup, and global visual themes.

## Near-term work after the current v0.4.0 slice

### Shared calibrated Audio Input service

**Priority:** High

PR #70 establishes the shared audio-capture boundary. Remaining work is calibration/generalization: deterministic microphone slot/configuration, physical PDM measurement and correction, effective PCM-rate measurement, telemetry, gain-policy generalization, and FFT interpretation using measured effective rate.

Future consumers may include tuner/pitch analysis and microphone-side CW/Morse receive.

### Log format, levels, and maintained parser

**Priority:** Near term

Rework application/system logging as a common policy rather than letting each feature independently decide how much diagnostic or observational data to emit. Logging levels should provide a practical way to reduce log volume while retaining important operational and decoded/observed records at the selected level.

- define a small, consistent set of logging levels and their intended semantics;
- make the configured log level part of authoritative device configuration;
- classify existing log records by level and tune emitted detail so lowering the level predictably reduces volume rather than disabling arbitrary subsystems;
- keep important application observations, warnings, and failures semantically distinct from verbose/debug diagnostics;
- allow applications such as future CW/Morse receive to emit structured decoded-message records through the common logger when enabled at the selected level;
- keep internal app-browsable history and SD logging separate: a decoded message may be retained internally for browsing and independently mirrored to the SD log when policy and removable storage permit;
- use stable `key=value` fields and quoting/escaping;
- use stable naming/order and an explicit schema/version marker if needed;
- maintain a reference parser producing JSON and/or CSV;
- add regression tests for levels, missing/unknown fields, long values, and quoting;
- preserve UTC ISO-8601 timestamps and explicit uptime fallback.

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
- add GPS/GNSS as a shared Location & Time service;
- use GNSS UTC as an explicit time authority and optionally attach location metadata to observations/captures;
- add NTP only under explicit networking policy.

### Diagnostics and self-measurement

Add a dedicated diagnostics surface reporting measured subsystem state rather than only initialization success, including flash/PSRAM/heap, internal persistence, SD, power, radios, audio, and GPS/RTC where implemented.

### Board/peripheral inventory

Systematically inventory each supported board so every onboard peripheral, power rail, GPIO, bus device, interrupt, and expansion interface is used, intentionally reserved, or documented as conflicting/unavailable.

### Multi-target build, release, and browser flasher

**Priority:** Medium/platform prerequisite

Give each supported board a stable target identity and deterministic package/manifest; allow release artifacts and the web catalog to discover multiple hardware targets without hard-coded board lists; keep chip-family compatibility distinct from board identity; and preserve explicit manual hardware selection for clean-install flashing.

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
- ADC, RMT, PCNT, GPTimer, and LEDC opportunities where they improve deterministic measurement/control.
- Hardware AES/SHA acceleration for authenticated/encrypted logs or future secure communications.
- Secure Boot and flash encryption once field-deployment/recovery policy is mature.
- Preserve a documented JTAG/debug path.

### Security/communications experiments

- Evaluate authenticated/encrypted data exchange using ESP32 hardware crypto.
- Explore phone-paired encrypted audio/data relay concepts only after the basic RFScout platform and audio service are stable.
- Keep key management, threat model, interoperability, and user-controlled recovery explicit.

### Presentation ideas

- Automatic Indoor/Outdoor/Night selection from ambient light or explicit schedule, with manual override and hysteresis.
- Additional compact data visualizations that preserve readability on the 320x170 display.
- Theme-specific status/icon variants where useful outdoors/night.
- Optional onboarding/help hints that do not permanently consume data-display area.

## Roadmap maintenance rules

- `ROADMAP.md` is the single canonical planning location, including active release milestones.
- When a planned feature becomes implemented, move factual behavior into the appropriate production documentation and mark the roadmap item complete here.
- Do not create parallel release-specific roadmap/status documents unless the material genuinely cannot be maintained coherently here.
- Do not leave future-tense design promises in production README/Architecture/Capabilities/Configuration/Release docs.
- Keep speculative/vision material here unless a separate technical research document is genuinely needed.
- A merged planning-only PR does not mean its feature is implemented; status must distinguish design capture from production code.
