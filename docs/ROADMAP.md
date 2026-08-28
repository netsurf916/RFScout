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

**Status:** Done — PR #21, notification refinement PR #23, volume/registry PR #24

- Add real onboard-speaker output through a dedicated asynchronous audio-output service.
- Keep RF observation callbacks nonblocking by enqueueing bounded audio commands.
- Serialize alert policy changes with request enqueueing without blocking RF callbacks; selecting `OFF` clears pending sounds.
- Support device-wide `OFF`, `TONE`, and `MORSE` notification modes.
- Keep each Morse notification exactly one character long and approximately **20 WPM**.
- Map distinct Morse characters to detection categories, including surveillance, CC1101, and nRF24 categories.
- Keep the category enumeration and compiled default characters centralized in the alert layer.
- Persist notification mode and every per-category character in `/sdcard/rfscout.json`; a valid saved configuration overrides compiled defaults on boot.
- Add persistent global notification volume from `0-100%`, with a compiled fallback of **50%** and restoration from saved configuration when present.
- Capture volume in each queued speaker command so already-queued sounds retain the level that was active when requested.
- Maintain a canonical 1:1 notification descriptor registry containing application ownership, stable JSON key, display label, and default character for every alert item.
- Make Device and SD persistence iterate the canonical registry rather than maintaining duplicate category/key lists.
- Document producer-side maintenance points: WiFi/BT classification mapping in `rfscout.c`, and CC1101/nRF24 category mappings in `app_manager.c`.
- Enumerate notification categories in Device so each Morse character can be changed manually.
- Centralize the printable International Morse patterns used by notifications for reuse by the Audio application.
- Keep speaker output on I2S1 and microphone/PDM acquisition on I2S0.
- Target ESP-IDF 6.x only and run CI against ESP-IDF 6.1.
- Replace the removed built-in `json` component with managed `espressif/cjson`.

Remaining hardware validation includes physical speaker channel, perceived loudness across the new volume range, and playback behavior on the target device.

## 4. Clean GitHub release packaging

**Status:** Done — PR #22

- Produce clean firmware artifacts suitable for a GitHub Release.
- Keep pull-request and main-branch builds as short-lived candidate artifacts rather than creating releases automatically.
- Require a deliberate manual workflow run to prepare a release from a version and source ref.
- Reject duplicate versions, source commits outside main, and source commits with no changes after the latest published release.
- Create draft releases only; keep publication and target-hardware approval as explicit maintainer actions.
- Enable ESP-IDF reproducible builds and produce deterministic release metadata, checksums, and archives.
- Include a combined ESP32-S3 image, the component images and offsets, and a staged ESP Web Tools manifest for the future browser installer.
- Package release artifacts sensibly rather than publishing an entire ESP-IDF build tree.
- Include straightforward flashing and installation instructions.
- Automate release artifact generation so releases are reproducible.

## 5. Navigation, menu consistency, and detection UX

**Status:** Done — PR #23

- Change Device/global settings to a pass-through landing screen by default rather than immediately taking ownership of the encoder.
- Require a deliberate center-button press to enter Device settings/edit mode.
- While Device is not entered, keep encoder rotation as normal application navigation.
- Make the Device landing screen clearly indicate that center enters configuration while showing useful read-only status.
- Standardize WiFi/BT, CC1101, nRF24, Audio, and Device menus so **Back** is the first and initially selected entry.
- Make accidental menu entry easy to undo by opening a menu and pressing center again without rotating.
- Add a WiFi/BT `RESET DETECTIONS` action that clears the rolling device/OUI census and retained surveillance detections without restarting the collectors.
- Rename the CC1101 and nRF24 right-side category panel to **DETECTIONS**.
- Preserve app-specific lifecycle behavior so inactive hardware is stopped and re-entered cleanly.

## 6. MAC provenance, expanded IEEE identity, and surveillance prefix metadata

**Status:** In progress

- Separate locally administered/private addresses from genuinely unresolved globally administered addresses in the WiFi/BT vendor summary.
- Keep `RANDOM` and `UNKNOWN` as fixed rows below the ranked vendor list.
- Expand IEEE vendor attribution beyond MA-L/OUI to MA-M, MA-S/OUI-36, and legacy IAB assignments.
- Use longest-prefix matching: 36-bit, then 28-bit, then 24-bit.
- Keep CID out of ordinary vendor attribution because CID is not a globally unique MAC-address allocation.
- Keep IEEE ownership data authoritative and generated directly from the IEEE Registration Authority feeds.
- Retain human-curated surveillance prefixes as separate source data, but expose them through the common prefix-identity lookup API instead of a classifier-owned duplicate OUI table.
- Preserve multi-signal surveillance classification: prefix metadata remains one evidence source alongside BLE names, company IDs, services, Remote ID, and future protocol/payload evidence.
- Permit curated field-verified locally administered prefixes to contribute surveillance evidence without treating them as IEEE vendor assignments.
- Add deterministic generator tests and keep the scheduled registry-refresh workflow synchronized with all generated outputs.
- Document registry sources, precedence, classifier boundaries, and maintenance touch points.

## 7. Log format and automated parsing

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

## 8. Recorder application replacement

**Status:** In progress — PR #27

- Replace the former Audio spectrum analyzer rather than preserving the FFT/DSP path.
- Reuse the existing I2S0/PDM microphone hardware for recording only.
- Calibrate the PDM receive path against measured GPIO clock and delivered PCM rate rather than trusting nominal divider assumptions; derive runtime-safe settings that produce a consistent recording format across supported hardware/IDF behavior.
- Record standard 32 kHz, 16-bit mono PCM WAV files under `/sdcard/audio/`.
- Treat SD availability as a first-class application state; recording and playback are unavailable without mounted storage.
- Create `/sdcard/audio/` automatically when storage is available.
- Enumerate only supported, correctly formatted PCM WAV files and expose duration/size metadata.
- Play selected recordings asynchronously over the onboard I2S1 speaker.
- Finalize WAV headers and stop active capture/playback when leaving Recorder.
- Keep capture and playback off the UI loop and preserve app lifecycle/resource ownership.
- Validate microphone capture, storage-loss behavior, speaker playback, and perceived volume on target hardware.

A separate **CW** application is planned after Recorder. CW will own editable/persistent Morse strings and send complete strings through the shared Morse/speaker service.

## 9. External time-authority integrations

**Status:** Planned

- Add supported external RTC discovery and integration.
- When an RTC is detected, explicitly prompt:
  - **Read RTC** — set RFScout's authoritative time from the RTC.
  - **Set RTC** — write RFScout's current authoritative time to the RTC.
- Add GPS as a time authority where supported.
- Add NTP as a time authority when networking policy permits it.
- Preserve and display the specific authority source through the model established in PR #20.
- Define explicit authority-transition rules so a newly available source never silently overwrites another valid clock.

## 10. Passive nRF24 Enhanced ShockBurst packet discovery and validation

**Status:** Planned

- Extend the current receive-only nRF24 activity scanner beyond RPD energy sweeps.
- Discover defensible Enhanced ShockBurst packet candidates without introducing active transmission, ACK generation, injection, or replay behavior.
- Validate framing/CRC sufficiently before treating observations as packets.
- Feed validated observations into the existing nRF24 packet-classifier infrastructure.
- Preserve watchdog-safe scanning and existing application lifecycle/resource ownership.

## 11. nRF24 classifier refinement and time-windowed radio counts

**Status:** Planned

- Refine nRF24 fingerprints using real hardware captures and validated packet observations.
- Improve protocol/device confidence without overstating product identity.
- Replace or supplement session-only category counts with useful time-windowed activity counts.
- Apply the same time-window model to other radio views where it improves situational awareness.

## 12. CC1101 burst timing and modulation analysis

**Status:** Planned

- Add burst-duration and inter-burst timing analysis.
- Explore conservative modulation/behavior inference from receive-only measurements.
- Keep classifications evidence-based and avoid presenting heuristic categories as decoded product identities.

## 13. Drill-down views, classifier controls, and structured captures

**Status:** Planned

- Add per-category drill-down views where the underlying observations support useful detail.
- Add configurable classifier thresholds where tuning is useful and understandable.
- Add structured SD exports/captures for analysis outside RFScout.
- Keep high-rate capture paths asynchronous so storage latency does not block radio acquisition.

## 14. Additional optional hardware and applications

**Status:** Longer term

- NFC/PN532 tools.
- IR analyzer/transmit tools, with transmit behavior kept explicit and separate from passive monitoring.
- GPS user interface and location features, distinct from GPS time-authority support.
- Additional optional hardware applications that follow the existing lifecycle, shared-resource, settings, status-UI, and notification conventions.
