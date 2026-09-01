# RFScout Roadmap

This is the canonical planning document for RFScout. Production documentation describes implemented behavior; future design, priorities, follow-up work, and brainstorming belong here so intent is not scattered across operational docs.

Statuses describe implementation state, not release state.

## v0.4.0 milestone plan

**Status:** Planned

v0.4.0 is intended to be a resource-management, lifecycle-stability, and application-quality release. The release should make application switching deterministic, allow data-heavy applications to use PSRAM opportunistically rather than being constrained by conservative fixed-size buffers, and establish a concrete improvement plan for every current application.

The work in this section is planning only. It does not describe behavior implemented on current `main` until the corresponding implementation work is merged and the production documentation is updated.

### Milestone 1: application-scoped PSRAM memory manager

Introduce a shared memory manager for application-owned dynamic storage.

Design goals:

- Treat PSRAM as a reusable application resource rather than permanently partitioning large fixed buffers among applications that are not active at the same time.
- Give the active application access to essentially all currently available application PSRAM, excluding memory reserved by the platform, long-lived services, the RTOS, display infrastructure, drivers, and other resources that must remain resident across application transitions.
- Prefer PSRAM for large or growable application datasets while keeping latency-sensitive or DMA-constrained allocations in the memory class required by the underlying peripheral/API.
- Allow application-owned collections to grow dynamically as observations, packets, captures, frames, history, or processing requirements increase instead of imposing arbitrary static limits solely to protect memory for inactive applications.
- Track allocations by owning application so the application manager can reclaim an application's entire dynamic allocation set as one lifecycle operation.
- Support normal allocation, capacity growth, and explicit release without requiring each application to invent its own allocator or growth policy.
- Make allocation failure explicit and recoverable. Applications should preserve a usable bounded state, evict according to documented policy where appropriate, or report resource pressure rather than corrupting state or crashing.
- Expose diagnostics including app-owned bytes, allocation count, peak app usage, free PSRAM, largest free block, and failed allocations where practical.
- Treat PSRAM as a required platform capability for supported RFScout targets. Internal SRAM is reserved for system, stack, latency-sensitive, and hardware-constrained allocations rather than serving as a substitute for large application storage.

The memory manager is not intended to hide ownership. Persistent device configuration, platform services, caches that intentionally span applications, and peripheral-driver allocations remain owned by their respective subsystems rather than being silently attached to the active app. A target without usable PSRAM is outside the supported RFScout platform contract rather than a reduced-capacity fallback configuration.

### Milestone 2: deterministic app-exit reclamation

Application switching must become a hard memory-ownership boundary.

Required lifecycle ordering:

1. Begin a reversible transition-preparation phase by quiescing the current application's workers and peripheral activity using the existing bounded lifecycle contract.
2. Cancel or drain callbacks/timers that can access app-owned dynamic memory, retaining enough state to restore them if the transition aborts.
3. Perform only reversible app-specific preparation that leaves user-visible/runtime state recoverable; destructive cleanup is deferred until commit.
4. Verify that no worker, callback, timer, DMA completion path, or other execution context can still reference app-owned state.
5. If the reachability/safety gate fails, abort the transition, keep the application's memory intact, roll back the reversible preparation, and reactivate the source app in its prior operational state.
6. After the final fallible safety gate succeeds, commit the transition by running destructive app-specific exit cleanup while its data is still valid.
7. Reclaim all memory owned by that application through the memory manager.
8. Verify allocator/accounting state reports no remaining allocations for the exited application.
9. Enter/settle/activate the destination application.

Release criteria:

- Repeated rapid switching across every enabled application must not produce monotonic PSRAM/heap loss.
- Leaving an application must free all memory owned by that app, including dynamically grown collections and scratch/work buffers.
- A failed quiesce or failed pre-reclamation reachability/safety check must abort the transition before destructive cleanup or memory reclamation and restore the current application's operational state.
- No destination application may activate while the previous app can still access reclaimed memory.
- Stress diagnostics should record source/destination app, quiesce duration, safety-gate result, rollback/commit result, reclaimed bytes, remaining app-owned allocations, free heap/PSRAM, minimum-ever free heap, and largest free block where practical.

This makes app entry/exit analogous to crossing a transactional resource-ownership boundary: a transition may take slightly longer when cleanup is required, but the next application starts from a predictable memory baseline. The pre-reclamation safety gate is mandatory; irreversible cleanup and reclamation happen only after that gate succeeds, and post-reclamation checks are accounting assertions rather than a substitute for proving that execution contexts have stopped.

### Milestone 3: dynamic WiFi/BLE retention and observation model

Rename the user-facing **WiFi / BT** application to **WiFi / BLE** and replace fixed-capacity/MAC-address-centric thinking with dynamically growable app-owned observation, identity, and relationship state backed by the memory manager.

Retention controls should support three semantic classes:

- **Instant** — no intentional retention grace period. An entry is eligible for removal on the next normal housekeeping pass once it is no longer current according to the collector's observation semantics.
- **Timed** — retain configurable finite timeout behavior for users who want a rolling recent-history window.
- **Never** — do not age the entry out while WiFi/BLE remains the active application. `Never` is session-scoped, not permanent storage: switching away still frees app-owned observation, identity, relationship, and history state under the app-exit contract.

Timeouts define whether evidence is current rather than requiring an object under active inspection to disappear immediately. A selected/deep-dive object may become stale while temporarily retained for UI stability; once unreferenced, normal retention and memory-pressure reclamation apply.

Identity and analysis goals:

- Stop equating every observed MAC address with a physical device. Preserve raw observed addresses while introducing stable app-internal entity IDs, observed-identity sets, and confidence/provenance for any correlation.
- Preserve independent Wi-Fi and BLE source aging where a higher-level entity contains evidence from both radios.
- Treat randomized-address correlation as evidence rather than proof. Never merge stable/associated identities solely from similar fingerprints, and do not claim persistent physical identity across independently randomized associations/private-address rotations without adequate passive evidence.
- Replace the Wi-Fi collector's MAC-only reduction with richer semantic 802.11 observations where practical, preserving address roles/BSSID, frame type/subtype, sequence information, channel/RSSI, SSID labels, relevant capability information, and association evidence.
- Use authentication/association/reassociation exchanges and ordinary associated data traffic as useful client/AP topology evidence; EAPOL/handshake traffic strengthens confidence but is not required to discover an associated client.
- Keep randomized Probe Requests as useful transient evidence while preventing each rotating probe MAC from inflating the primary device count one-for-one. Conservative short-lived clustering may use compatible timing, sequence, capability, channel/RSSI, and other evidence while retaining uncertain observations separately.
- Make radio type a first-class view dimension: **Combined / Wi-Fi / BLE**.
- Wi-Fi views should include **SSIDs, APs, Clients, Unassociated, and Probes**. SSID grouping is label-based analysis only: an SSID is non-unique, so same-named APs must remain distinct BSSIDs and must not be presented as one logical network/topology unless separate observed evidence supports a stronger relationship.
- BLE views should use BLE-native semantics such as **Devices, Advertisers, Beacons, and Services/Capabilities** rather than inventing AP/probe terminology or claiming unobserved central/peripheral connections.
- Use the main screen for actively updated analytical projections and context-sensitive menus for relationship drill-down. Internal IDs plus active/stale state should keep selections stable while observations continue to update.
- Grow retained storage on demand until memory pressure requires a documented degradation/eviction policy, preferentially discarding stale/low-confidence transient evidence before current/high-confidence entities when semantically safe.
- Preserve detection notification behavior independently from whether an observation remains visible in retained history.
- Keep all WiFi/BLE observation, identity, relationship, and retained-history memory app-owned so leaving the app returns it to the common pool.

### Milestone 4: application-by-application improvement pass

Every current application should receive an explicit 0.4.0 review rather than treating the memory manager as an isolated infrastructure change.

#### Home / Device Settings

- Add memory/lifecycle diagnostics sufficient to verify app-owned PSRAM behavior without a debugger, either directly in Device Settings or through a compact diagnostics surface reachable from Home.
- Show total/free PSRAM, largest free block, current app-owned bytes, peak app-owned bytes, and the most recent transition/reclamation result where practical.
- Audit settings interaction against the shared semantic menu policy so toggles and small choices use direct center-button cycling while genuine values retain enter/edit/exit behavior.
- Keep Home lightweight and free of large app-owned allocations so it remains a useful neutral baseline when measuring whether another app released its resources.

#### WiFi/BLE

- Rename the app from WiFi / BT to WiFi / BLE when the implementation lands and update production documentation accordingly.
- Move retained observation/source/surveillance plus identity/relationship state to dynamically growable app-owned storage.
- Add Instant and Never retention choices alongside finite timeout values, with active/stale semantics that keep deep-dive navigation stable.
- Replace MAC-equals-device counting with semantic observations, stable internal entity IDs, observed-identity sets, and confidence/provenance.
- Parse enough 802.11 roles/subtypes/metadata to distinguish APs, associated clients, unassociated/probe activity, and defensible topology evidence.
- Use ordinary associated traffic and management exchanges for associated-client discovery; treat handshake/EAPOL observations as stronger confirmation rather than a requirement.
- Cluster randomized probe identities conservatively into ephemeral scan sessions only where evidence supports it, leaving uncertain observations distinct.
- Add Combined/Wi-Fi/BLE radio selection and radio-specific views: Wi-Fi SSIDs/APs/Clients/Unassociated/Probes and BLE Devices/Advertisers/Beacons/Services.
- Treat SSID views as non-unique label groupings, never as automatic logical-network identity or shared topology.
- Add actively updated relationship drill-down without cursor/selection instability as entries become stale.
- Preserve source-specific aging inside higher-level entities and keep unassociated/probe-only evidence explicitly lower-confidence than associated-client evidence.
- Add memory-pressure handling and diagnostics without dropping passive collection stability.
- Ensure both collectors are quiesced before app-owned observation/identity state is reclaimed on exit.

#### CC1101

- Move large sweep-history, detection-history, and future structured-capture storage to app-owned dynamic PSRAM where suitable.
- Add burst duration and inter-burst timing enrichment.
- Add conservative modulation/behavior inference without presenting heuristics as decoded product identity.
- Add structured capture/history that can grow while the app remains active, subject to explicit memory-pressure policy.
- Improve drill-down/history presentation where accumulated observations support it.
- Verify that persistent worker shutdown completes before all CC1101 app-owned dynamic state is reclaimed.

#### nRF24

- Move packet/history/classifier working sets that do not require internal/DMA memory to app-owned dynamic PSRAM.
- Strengthen passive Enhanced ShockBurst candidate discovery and validation so packet/history surfaces can be populated from defensible receive-only observations.
- Grow validated packet/capture history dynamically during the active session rather than reserving a large permanent buffer.
- Preserve strict receive-only behavior: no ACK generation, injection, replay, or active discovery.
- Improve structured packet drill-down and classifier confidence using real hardware captures.
- Verify that the radio worker is fully quiesced before packet/history memory is reclaimed.

#### Recorder

- Implement the shared calibrated Audio Input service already described in this roadmap so microphone ownership and measured-rate/gain telemetry are no longer Recorder-specific.
- Use app-owned PSRAM for large Recorder work buffers, waveform/FFT history, catalog scratch storage, and asynchronous processing buffers where the ESP-IDF API does not require another memory class.
- Keep DMA/I2S buffers in the memory class required by the hardware/API rather than forcing all audio allocation into PSRAM.
- Make monitor/record/playback transitions explicitly release mode-specific scratch buffers when they are no longer useful.
- Ensure leaving Recorder synchronously stops all audio activity before its app-owned memory is reclaimed.
- Preserve completed recordings on SD; app-exit reclamation applies to runtime memory, not user data on storage.

#### Thermal

- Move interpolation/work/history buffers that can safely reside in PSRAM into app-owned dynamic storage.
- Avoid permanently reserving thermal work memory while the optional sensor/app is inactive.
- Add a modest history/inspection improvement, such as recent min/max/center-point trend or frame statistics, using dynamically sized storage rather than a global fixed allocation.
- Review scale/palette/range controls for consistency with the shared menu interaction policy while keeping literal thermal pixel colors outside theme translation.
- Stop hot-plug acquisition and all frame callbacks before thermal memory reclamation.

### Milestone 5: independent visual theme and audio policy

Separate visual presentation from device sound policy so choosing a display theme never implicitly changes audio behavior.

Planned visual themes:

- **Indoor** — normal RFScout presentation.
- **Outdoor** — daylight/high-contrast presentation.
- **Night** — red/orange low-light presentation.

Planned audio policies:

- **Normal** — incidental UI feedback and enabled detection notifications behave normally, subject to existing per-app notification controls, global notification mode/volume, and application-specific audio rules.
- **Quiet** — suppress incidental interaction/event sounds and detection-notification presentation while preserving explicitly requested application content audio such as Recorder playback.

Design goals:

- Treat visual theme and audio policy as independent persistent configuration fields.
- Keep theme resolution strictly visual; themes must not participate in deciding whether a sound is emitted.
- Keep application content audio, detection notifications, and incidental UI feedback as distinct policy classes.
- Quiet suppresses incidental UI feedback and audible detection-notification presentation without mutating the user's saved notification enablement, mappings, mode, or volume.
- Quiet does not suppress explicitly requested content audio such as Recorder playback or future CW output.
- Permit every useful combination, including Outdoor + Quiet, Night + Normal, and Night + Quiet.
- Retire the standalone **Discipline** theme rather than preserving a permanently coupled visual/audio mode.
- Migrate legacy Discipline configuration to **Night + Quiet** and materialize the Night environmental gain profile (`20%` interaction / `60%` notification). Quiet preserves the immediate silent behavior after migration; if the user later selects Normal, the migrated gains provide the expected Night audio profile rather than carrying forward Discipline's legacy `0%` / `0%` suppression.
- Existing Indoor, Outdoor, and Night selections retain their visual choice and default to the **Normal** audio policy unless an explicit migrated audio-policy value exists.
- Migration must also materialize the current theme-owned interaction and notification gains into independent audio state so Indoor preserves 35%/100%, Outdoor preserves 55%/100%, and Night preserves 20%/60%. These environmental gains remain separate from the existing global notification-volume setting.
- Saving after migration should emit the independent theme, audio-policy, and migrated audio-gain fields and should not recreate obsolete Discipline state.
- Add Audio Policy to Device Settings as a small direct-cycle choice consistent with the semantic menu policy.

Validation should cover all six theme/policy combinations, verify that Quiet suppresses incidental and notification sound presentation but not explicit application content audio, verify that migrated Discipline uses Night's 20%/60% environmental gains and that migrated Indoor/Outdoor/Night configurations retain their pre-migration effective sound levels, and verify that changing either axis never changes the other.

### Milestone 6: cross-application consistency and cleanup

Use the 0.4.0 app review to remove behavioral inconsistencies that increase maintenance cost or make lifecycle bugs harder to reason about.

Goals:

- Apply the semantic menu policy consistently across all applications.
- Keep visual theme logic independent from audio-policy resolution.
- Standardize app entry/exit diagnostics and memory-manager instrumentation.
- Document which allocations are app-scoped, service-scoped, DMA-constrained, or intentionally persistent.
- Prefer shared growable containers/helpers over one-off realloc/capacity schemes in individual apps.
- Remove obsolete planning text from production documents when 0.4.0 implementations supersede it, moving factual behavior into the appropriate production documentation at merge time.

### v0.4.0 release gate

v0.4.0 should not be considered complete until:

- the shared app-scoped memory manager is implemented and used by data-heavy current applications where appropriate;
- supported RFScout hardware targets provide usable PSRAM as a platform requirement;
- application exit passes the pre-reclamation safety gate and reliably reclaims all app-owned dynamic memory only after workers/callbacks/timers can no longer reference it;
- WiFi/BLE exposes Instant, finite timed, and Never retention semantics backed by growable app-owned storage;
- WiFi/BLE no longer treats every observed MAC as a physical device: randomized probe/private-address activity is represented conservatively with raw-identity provenance and confidence, and rotating probe MACs do not inflate the primary device count one-for-one;
- Wi-Fi exposes defensible AP/client/probe relationships from appropriate 802.11 evidence, with handshake/EAPOL observation optional for ordinary associated-client discovery and SSID groupings clearly treated as non-unique labels rather than automatic network identity;
- the app provides Combined/Wi-Fi/BLE radio selection, Wi-Fi SSIDs/APs/Clients/Unassociated/Probes views, BLE Devices/Advertisers/Beacons/Services views, and live deep-dive navigation that remains stable as observations age from active to stale;
- every current application has completed its documented improvement review or has an explicit, documented reason for deferral;
- visual theme and Normal/Quiet audio policy are independent, with legacy Discipline configuration migrated to Night + Quiet;
- Quiet suppresses incidental and detection-notification sound presentation without suppressing explicitly requested application content audio;
- rapid-switch stress testing shows no monotonic heap/PSRAM loss, stale-worker access, or cross-app resource ownership violation;
- memory pressure and allocation failure are handled without crashes or silent data corruption;
- production documentation is updated to describe only the portions that are actually implemented; and
- release notes summarize the memory/lifecycle contract and any user-visible app changes without implying unsupported behavior.

## Completed foundation

### Display, board support, and application lifecycle

**Status:** Done — PRs #1-#7

- Native ESP-IDF ST7789 display/framebuffer support for the T-Embed CC1101 Plus.
- Shared board pin/SPI ownership.
- 16 MB flash layout with a larger factory application partition.
- WiFi/BLE situational-awareness UI and surveillance classification groundwork.
- Bounded asynchronous SD logging and shared status presentation.
- Multi-application lifecycle/navigation framework.

### CC1101 and nRF24 analyzers

**Status:** Done baseline — PRs #8-#11

- Receive-only CC1101 RSSI spectrum scanning with bounded sweeps, peak hold, conservative detection categories, and reset controls.
- Receive-only nRF24 2.4 GHz activity scanning with RPD history, peak hold, passive ESB classification groundwork, address deduplication, detection counts, and reset controls.

Further classifier/protocol enrichment is tracked below.

### Audio analyzer experiments and codebase cleanup

**Status:** Superseded/Done — PRs #12-#19

- Initial PDM/FFT experiments established microphone hardware behavior but were later replaced by Recorder.
- Shared application status presentation and global power-off overlay were established.
- Application logging was unified.
- Historical review findings were closed.
- The hand-maintained firmware tree was canonicalized with clang-format 18 and full-tree CI enforcement.

The removed standalone Audio analyzer is not a current application. Only the compatibility ABI remains until a later naming cleanup.

### Device configuration, time, notifications, and release tooling

**Status:** Done baseline — PRs #20-#24

- SD-backed `/sdcard/rfscout.json` with compiled fallbacks and recovery-safe replacement.
- Device pass-through landing screen and center-entered settings.
- App enablement, brightness, timezone/manual time, SD logging preference, and wall-clock provenance.
- Asynchronous I2S1 speaker service.
- Device-wide Off/Tone/Morse notifications with per-category characters.
- Independent persistent global notification volume with perceptual output scaling.
- Canonical alert descriptor registry.
- Reproducible firmware packaging and manual draft-release workflow.

### MAC provenance and prefix identity

**Status:** Done baseline — PRs #25-#26

- Separate `RANDOM` private/local addresses from unresolved public `UNKNOWN` addresses.
- IEEE MA-L, MA-M, MA-S/OUI-36, and IAB longest-prefix attribution.
- Curated surveillance-prefix metadata integrated through the common identity lookup while remaining separate from authoritative IEEE ownership.
- Deterministic generator coverage and scheduled registry refresh.

### Recorder replacement

**Status:** Done baseline — PR #27

- Replace the former standalone Audio spectrum analyzer with SD-backed Recorder.
- Record 16-bit mono PCM WAV under `/sdcard/audio/`.
- Measure PDM clock and delivered PCM rate at runtime instead of trusting nominal divider/sample-rate assumptions.
- Write measured effective rate into WAV metadata.
- Add asynchronous save processing with 300 ms head trim, 600 ms tail trim, DC removal, and bounded normalization.
- Enumerate/refresh validated recordings, play through I2S1, and delete from the browser.
- Add independent persistent Recorder playback volume.
- Add Recorder `MONITOR` mode with live FFT spectrum and persistent 0..+24 dB fixed digital input gain.

### Audio-input design capture

**Status:** Design captured, implementation pending — PR #28

PR #28 intentionally recorded the architecture goal but did **not** implement a new shared Audio Input service. The detailed implementation plan is consolidated below under Near-term work.

### Shared UI layout/chrome consistency

**Status:** Done baseline — PR #29

- Shared title/layout metrics.
- `ui_chrome_draw()` owns common title/status/battery presentation.
- Long titles are constrained away from the right-side status region.
- Device, Recorder, WiFi/BT, CC1101, nRF24, and Thermal use consistent top chrome/layout conventions.
- Footer placement remains optional; data-heavy views may use the bottom of the display.

### Browser flasher

**Status:** Done baseline — PR #30

- GitHub Pages + ESP Web Tools browser flasher.
- Published release assets mirrored into the Pages deployment.
- Candidate from the most recent triggering eligible build exposed ahead of releases when present.
- Same-origin firmware/manifests avoid browser authentication/CORS problems.
- Hardware flashing validated with a Chromium-based browser/Web Serial.

### Home and configurable startup

**Status:** Implemented — PR #32

- Turn the Device pass-through landing state into branded RFScout **Home** while preserving center-entered Device Settings.
- Keep Home/Device non-disableable and in normal top-level navigation.
- Default startup to Home instead of WiFi/BT.
- Persist a preferred startup target as backward-compatible schema-v1 `apps.startup` configuration.
- Allow Home, WiFi/BT, CC1101, nRF24, Recorder, or Thermal as the preferred startup target.
- Fall back to Home for the current boot when the configured target is disabled or unavailable without erasing the saved preference.
- Generate compact indexed LCD Home branding from canonical `assets/logo.png` artwork at build time.
- Reuse `assets/logo.png` and the RFScout orange/white identity in the GitHub Pages browser flasher.
- Preserve established per-application lifecycle and initialization behavior while adding startup selection.

### Global display themes

**Status:** Implemented — PR #33

- Centralize semantic UI color roles rather than allowing each application to independently define presentation colors.
- Add persistent Indoor, Outdoor, and Night themes.
- Keep Indoor aligned with the orange/white RFScout identity.
- Prioritize high daylight contrast in Outdoor instead of carrying forward the earlier subdued-secondary-color direction.
- Shift Night toward red/orange presentation while preserving state distinctions.
- Add Theme beside Brightness in Device Settings with immediate application and NVS-backed persistence independent of SD availability.
- Route historical application UI colors through the common semantic palette as a compatibility bridge.
- Keep literal sensor/image data outside theme translation through raw framebuffer paths.
- Apply theme logo roles to the Home bitmap and control affordances.

Automatic theme selection from ambient light or time-of-day remains a possible future presentation enhancement rather than current behavior.

## Near-term work

### 1. Shared calibrated Audio Input service

**Priority:** High

The microphone is a board resource and should not remain Recorder-specific. Implement a shared service that Recorder and live analysis consume.

Goals:

- Move I2S0/PDM channel ownership out of `recorder.c` into a single service.
- Keep the board microphone on its deterministic LEFT slot.
- Configure toward a desired physical PDM outcome rather than assuming an ESP-IDF request produces the requested clock/rate.
- Measure the physical PDM clock with PCNT after configuration.
- Compare measured clock to the target and use bounded `i2s_channel_reconfig_pdm_rx_clock()` correction iterations.
- Clamp requests to known-safe limits and reject measured microphone clocks outside the supported range.
- Measure delivered PCM throughput and expose an effective sample rate.
- Expose requested rate, measured PDM clock, effective PCM rate, calibration result, level, peak, and clipping telemetry.
- Centralize fixed digital input gain (0..+24 dB in 3 dB steps) so Recorder and Monitor consume the same post-gain stream.
- Serialize ownership so recording and monitoring cannot independently open I2S0.
- Keep whole-recording operations such as DC correction, trim, and normalization in Recorder rather than the hardware service.
- Make FFT frequency interpretation use the measured effective PCM rate.

Validation should include quiet room, speech, whistle, transient/clap, monitor-to-record transitions, repeated app entry/exit, and consistency across cold boots/ESP-IDF updates.

### 2. CW/Morse sender application

**Priority:** Near term

Add a separate **CW** application rather than overloading Recorder.

Goals:

- Compose strings with the rotary encoder and center button.
- Persist user-defined strings in `/sdcard/rfscout.json`.
- Add newly created strings to configuration and remove deleted strings from configuration.
- Browse/select saved messages.
- Send complete messages through the shared Morse/speaker service.
- Keep detection-notification one-character Morse mappings separate from CW messages.

### 3. Log format and maintained parser

**Priority:** Near term

Make logs reliably machine-parseable without depending on fixed word positions.

Goals:

- Stable `key=value` fields.
- Quote/escape free-form values that can contain spaces.
- Stable field naming/order by record type.
- Explicit schema/version marker if needed.
- Maintained reference parser that emits JSON and/or CSV.
- Regression tests for missing, unknown, long, and multi-word values.
- Preserve UTC ISO-8601 timestamps and explicit uptime fallback.

### 4. CC1101 enrichment

**Priority:** Near/medium term

- Burst duration and inter-burst timing.
- Conservative modulation/behavior inference.
- Structured captures for offline analysis.
- Per-category drill-down where observations support it.
- Time-windowed activity/count models where more useful than session-only counts.
- Avoid presenting heuristic classifications as decoded product identities.

### 5. nRF24 enrichment

**Priority:** Near/medium term

- Strengthen passive Enhanced ShockBurst candidate discovery/validation.
- Validate framing/CRC sufficiently before treating observations as packets.
- Improve classifier confidence from real hardware captures.
- Preserve receive-only behavior: no ACK generation, injection, replay, or active discovery.
- Add structured captures/drill-down and time-windowed activity where useful.
- Follow the evidence levels, scan modes, and passive guardrails documented in
  [nRF24 research and direction](NRF24_RESEARCH.md).

## Medium-term platform work

### Location and external time authorities

- Discover supported external RTCs without silently changing clock authority.
- When an RTC is detected, explicitly offer **Read RTC** or **Set RTC**.
- Add GPS/GNSS as a shared Location & Time service rather than only a standalone UI.
- Use GNSS UTC as an explicit time authority.
- Expose location/fix/satellite/accuracy information.
- Optionally attach location metadata to RF observations/captures when enabled.
- Add NTP only under explicit networking policy.
- Define clear authority-transition rules among manual, saved, RTC, GNSS, and NTP sources.

### Diagnostics and self-measurement

Add a dedicated diagnostics surface that reports measured subsystem state rather than only initialization success.

Candidate diagnostics:

- ESP32/board identity and reset reason;
- flash/PSRAM/heap;
- SD capacity/state/errors;
- battery/power state;
- WiFi/BLE state;
- CC1101/nRF24 state;
- microphone requested rate, measured PDM clock, delivered PCM rate, gain, clipping;
- speaker state;
- GPS/RTC state when implemented.

Favor **desired physical outcome -> configure -> measure -> correct/validate** for clocked/timing-sensitive peripherals.

### Board/peripheral inventory

Systematically inventory the target-board schematic and code so every onboard peripheral, power rail, GPIO, bus device, interrupt, and expansion interface is used, intentionally reserved, or documented as conflicting/unavailable. Repeat the inventory for materially different supported board variants.

### Multi-target build, release, and browser flasher

**Priority:** Medium/platform prerequisite

Extend the release pipeline and browser flasher so one RFScout version or PR candidate can carry multiple hardware-target builds without the deployment layer maintaining a separate hard-coded target list.

Goals:

- Give every supported board a stable hardware target ID and user-facing display name independent of MCU family.
- Make board metadata the canonical source for build/package identity rather than duplicating board-name maps across CMake, CI, packaging, and the web UI.
- Build/package each supported hardware target into its own deterministic directory and self-describing RFScout manifest.
- Include hardware target ID/name, IDF target/chip family, flash assumptions, version, commit, and image metadata in the packaged manifest.
- Let candidate/release artifacts contain any number of successfully produced target directories.
- Have the web-flasher catalog discover the target manifests actually present in an artifact instead of assuming a fixed set of boards.
- Model the catalog as build/version -> hardware targets -> ESP Web Tools manifest.
- Present Hardware and Firmware as separate user choices, filtering firmware choices to versions that actually provide the selected board target.
- Keep chip-family compatibility distinct from board identity so boards sharing an ESP32 family cannot be mistaken for interchangeable firmware targets.
- Preserve an explicit manual hardware choice for clean-install/bootloader flashing; treat any future USB/runtime board auto-identification only as a convenience layer.
- Cover candidates, published releases, missing-target cases, and deterministic catalog generation with packaging tests.

This work should also revisit browser-flasher publication concurrency so PR candidates and `main` builds do not unnecessarily replace or cancel one another while publishing the shared Pages environment.

## Longer-term applications and capabilities

- NFC/PN532 tools.
- IR analyzer/transmit tools, with transmit behavior explicit and separate from passive monitoring.
- GPS map/location UI beyond the shared Location & Time service.
- Structured export/capture browsing.
- More detailed WiFi channel/utilization and management-frame statistics where ESP32-S3 APIs provide defensible data.
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

- Automatic Indoor/Outdoor/Night selection from an ambient-light signal or explicit schedule, with manual override and hysteresis.
- Additional compact data visualizations that preserve readability on the 320x170 display.
- Theme-specific status/icon variants where useful outdoors/night.
- Optional onboarding/help hints that do not permanently consume data-display area.

## Roadmap maintenance rules

- When a planned feature becomes implemented, move the factual behavior into the appropriate production documentation and mark the roadmap item complete.
- Do not leave future-tense design promises in README, Architecture, Capabilities, Configuration, SD-card, or Release docs.
- Keep speculative/vision material here unless it becomes a concrete implementation document needed for active development.
- A merged planning-only PR does not mean its feature is implemented; status must distinguish design capture from production code.
