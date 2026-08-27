# RFScout Roadmap

This document is the canonical working roadmap for RFScout. The ordering reflects the current development priority, not a permanent commitment. Update statuses, scope, and ordering here as work is completed or priorities change so future development sessions have a single source of truth.

## 1. Full-codebase linting and formatting cleanup

**Status:** Done — PR #19

- Canonically format the complete hand-maintained firmware tree with clang-format 18.
- Enforce full-tree formatting in pull-request CI.
- Keep local formatting checks integrated with the normal development build workflow.

## 2. Device and Settings rework

**Status:** Done — PR #20

- Add SD-backed `/sdcard/rfscout.json` configuration with schema validation.
- Keep compiled sane defaults so RFScout remains usable without an SD card.
- Add the Device settings application.
- Add application enable/disable controls.
- Add display brightness and 12/24-hour clock preferences.
- Add configurable timezone definitions and selection.
- Make the ESP32 system wall clock authoritative for runtime display, log timestamps, and normal FAT metadata.
- Show the local clock in the global status bar.
- Support manual date/time setting.
- Use UTC ISO-8601 timestamps for logs when wall time is valid, with an explicit monotonic fallback before time is established.
- Save shutdown time/source metadata for cold-boot restoration.
- Preserve a future authority model capable of representing manual, GPS, NTP, and specific RTC sources.

Future RTC/GPS/NTP integrations are tracked separately below. In particular, detecting a supported external RTC must not silently choose which clock wins; Device should explicitly offer **Read RTC** or **Set RTC**.

## 3. Configurable asynchronous audio alerts and ESP-IDF 6.x migration

**Status:** Done — PR #21

- Add real onboard-speaker output through a dedicated asynchronous audio-output service.
- Keep RF observation callbacks nonblocking by enqueueing bounded audio commands.
- Serialize alert policy changes with request enqueueing without blocking RF callbacks; selecting `OFF` must clear pending sounds and prevent a concurrent stale request from enqueueing afterward.
- Add WiFi / BT alert modes: `OFF`, `TONE`, and `MORSE`.
- Default audible alerts to `OFF`.
- Keep each Morse alert exactly one character long.
- Play surveillance-alert Morse at approximately **20 WPM** so category cues are concise while remaining recognizable by ear.
- As a follow-up refinement, map a distinct Morse character to each detected surveillance category so the user can identify the likely category by ear without looking at the display. Categories such as general camera, body-worn camera, Flock-style camera, drone, and other surveillance classes should have intentionally distinct one-character patterns.
- Define the category-to-Morse mapping centrally alongside the surveillance classifier rather than scattering hard-coded characters through alert call sites.
- Centralize the printable International Morse patterns used by alerts for reuse by the Audio application.
- Keep speaker output on I2S1 and microphone/PDM acquisition on I2S0.
- Target ESP-IDF 6.x only.
- Run CI against the ESP-IDF 6.1 development line.
- Replace the removed built-in `json` component with managed `espressif/cjson`.
- Complete required ESP-IDF 6 driver/API migration work discovered by CI.

Remaining hardware validation includes physical speaker channel, loudness, and playback behavior on the target device.

## 4. Clean GitHub release packaging

**Status:** In progress — PR #22

- Produce clean firmware artifacts suitable for a GitHub Release.
- Keep pull-request and main-branch builds as short-lived candidate artifacts rather than creating releases automatically.
- Require a deliberate manual workflow run to prepare a release from a version and source ref.
- Reject duplicate versions, source commits outside main, and source commits with no changes after the latest published release.
- Create draft releases only; keep publication and target-hardware approval as explicit maintainer actions.
- Enable ESP-IDF reproducible builds and produce deterministic release metadata, checksums, and archives.
- Include a combined ESP32-S3 image, the component images and offsets, and a staged ESP Web Tools manifest for the future browser installer.
- Determine exactly which binaries, partition data, and configuration/manifest files are required for installation.
- Package release artifacts sensibly rather than publishing an entire ESP-IDF build tree.
- Include straightforward flashing and installation instructions.
- Automate release artifact generation so releases are reproducible.
- Prefer a release workflow that can validate the same ESP-IDF 6.x build before publishing artifacts.

## 5. Navigation and menu consistency

**Status:** Planned — near term

- Change the Device/global-settings application to behave as a pass-through landing screen by default rather than immediately taking ownership of the encoder.
- Require a deliberate center-button press to enter Device settings/edit mode.
- While the Device landing screen is not entered, encoder rotation should continue normal application navigation so simply arriving at Device does not trap the user in settings.
- Make the Device landing screen clearly indicate that a center press enters configuration while still showing useful read-only status where appropriate.
- Standardize all settings/action menus so **Back** is the first entry and the initial selection when a menu opens.
- Make an accidental menu entry easy to undo: opening a menu and pressing the center button again without rotating should safely return to the live/pass-through view without changing settings.
- Audit existing WiFi / BT, CC1101, nRF24, Audio, Device, and future application menus for the same Back-first navigation convention.
- Keep setting changes deliberate; entering or exiting a menu should not itself mutate configuration.

## 6. Log format and automated parsing

**Status:** Planned — near term

- Make RFScout log records reliably machine-parseable without depending on fixed word positions or assumptions about field length.
- Prefer stable `key=value` fields for structured metadata, especially values such as vendor/brand information whose length and word count vary between observations.
- Quote and escape free-form values when they may contain spaces, for example `brand="Axis Communications"`.
- Where a record still contains an intentionally free-form tail, place it at the end of the line so a parser can safely treat the remainder as that field rather than guessing token boundaries.
- Define a stable field order and naming convention for each log record type while preserving reasonable human readability.
- Add a maintained reference parser/tool that consumes RFScout logs and emits structured output suitable for downstream automation, such as JSON or CSV.
- Add automated parser/regression coverage with representative single-word, multi-word, missing, unknown, and unusually long brand/vendor values.
- Consider a lightweight record/schema version so future format changes can be handled explicitly rather than silently breaking analysis tools.
- Keep UTC ISO-8601 timestamps and other already-structured fields directly consumable by external scripts.

## 7. Audio application expansion

**Status:** Planned

- Preserve the useful spectrum analyzer while expanding Audio into multiple tools.
- Add microphone recording using the existing I2S0/PDM capture path rather than introducing a competing microphone reader.
- Write standard PCM WAV recordings to SD through a buffered/asynchronous path suitable for continuous audio data.
- Add an interactive International Morse browser/player using the shared Morse definitions and speaker service established by PR #21.
- Make the Morse tool able to browse and play the standard character patterns; keep each surveillance-alert Morse notification limited to one character even when the character is selected by surveillance category.
- Validate the microphone/PDM/FFT path on hardware as part of the expansion.
- After the recorder and Morse player are stable, consider tuner and DTMF-analysis features.

## 8. External time-authority integrations

**Status:** Planned

- Add supported external RTC discovery and integration.
- When an RTC is detected, explicitly prompt:
  - **Read RTC** — set RFScout's authoritative time from the RTC.
  - **Set RTC** — write RFScout's current authoritative time to the RTC.
- Add GPS as a time authority where supported.
- Add NTP as a time authority when networking policy permits it.
- Preserve and display the specific authority source through the model established in PR #20.
- Define explicit authority-transition rules so a newly available source never silently overwrites another valid clock.

## 9. Passive nRF24 Enhanced ShockBurst packet discovery and validation

**Status:** Planned

- Extend the current receive-only nRF24 activity scanner beyond RPD energy sweeps.
- Discover defensible Enhanced ShockBurst packet candidates without introducing active transmission, ACK generation, injection, or replay behavior.
- Validate framing/CRC sufficiently before treating observations as packets.
- Feed validated observations into the existing nRF24 packet-classifier infrastructure.
- Preserve watchdog-safe scanning and existing application lifecycle/resource ownership.

## 10. nRF24 classifier refinement and time-windowed radio counts

**Status:** Planned

- Refine nRF24 fingerprints using real hardware captures and validated packet observations.
- Improve protocol/device confidence without overstating product identity.
- Replace or supplement session-only category counts with useful time-windowed activity counts.
- Apply the same time-window model to other radio views where it improves situational awareness.

## 11. CC1101 burst timing and modulation analysis

**Status:** Planned

- Add burst-duration and inter-burst timing analysis.
- Explore conservative modulation/behavior inference from receive-only measurements.
- Keep classifications evidence-based and avoid presenting heuristic categories as decoded product identities.

## 12. Drill-down views, classifier controls, and structured captures

**Status:** Planned

- Add per-category drill-down views where the underlying observations support useful detail.
- Add configurable classifier thresholds where tuning is useful and understandable.
- Add structured SD exports/captures for analysis outside RFScout.
- Keep high-rate capture paths asynchronous so storage latency does not block radio acquisition.

## 13. Additional optional hardware and applications

**Status:** Longer term

- NFC/PN532 tools.
- IR analyzer/transmit tools, with transmit behavior kept explicit and separate from passive monitoring.
- GPS user interface and location features, distinct from GPS time-authority support.
- Additional optional hardware applications that follow the existing lifecycle, shared-resource, settings, and status-UI conventions.
