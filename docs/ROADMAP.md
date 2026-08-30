# RFScout Roadmap

This is the canonical planning document for RFScout. Production documentation describes implemented behavior; future design, priorities, follow-up work, and brainstorming belong here so intent is not scattered across operational docs.

Statuses describe implementation state, not release state.

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
