# Release hardware acceptance checklist

This is a repeatable **validation procedure for implemented behavior**, not a feature plan. For v0.4.0 the release scope is stabilization/cleanup of existing capabilities, not completion of the deferred v0.5.0 implementation backlog. Tests that need unavailable optional nRF24 or other accessories may be recorded explicitly **Untested (hardware unavailable)**, never silently marked passed; software lifecycle safety remains mandatory. Record device revision, firmware commit/tag, SD card, GPS/nRF24 accessories, tester, date, and pass/fail notes in the release review. Any improvement proposals found during testing belong only in [Roadmap](ROADMAP.md); code defects belong in GitHub issues or corrective PRs.

| Area | Acceptance check | Result |
| --- | --- | --- |
| Startup | Cold boot, detect supported board, render display, navigate Home | Untested |
| Per-device I2C (#146) | Confirm BQ25896/BQ27220 100-kHz transfers for boot, provisioning, charging and recovery; with optional MLX90640 hardware test 400-kHz frame reads, sticky fallback to 100 kHz, pull-ups/rise time, interleaved battery telemetry and hot-plug recovery | Untested |
| App lifecycle | Switch repeatedly among Home, WiFi/BLE, Wardriving, GPS, CC1101, nRF24 (if present), Recorder; watch for hangs or retained workers | Untested |
| Radio sources | Toggle Wi-Fi/BLE independently; confirm disabled sources stop and stale observations disappear | Untested |
| Wi-Fi/BLE memory pressure | Verify independent source expiry, geometric PSRAM growth, clean capacity-drop reporting without evicting existing observations, and full reclamation on exit; record free/largest-free-block measurements | Untested |
| Wi-Fi channel policy | Auto/Manual, individual mask, sequential/shuffled sweep, empty-mask disable and reenable; verify after restart | Untested |
| BLE discovery | Passive/Active, duplicate filter, duty presets; validate observed scan responses and Wi-Fi coexistence | Untested |
| Wardriving | Start/stop PCAPNG capture, verify packet timestamps/Wireshark decoding and SD filenames | Untested |
| Wardriving policy | Confirm radio settings cannot change during recording and resume correctly after stop | Untested |
| GPS | Detect NMEA without fix, acquire valid fix, invalidate stale fix, inspect trip distance/reset and position metadata | Untested |
| GPS/nRF24 | Verify UART priority with GPS connected and nRF24 present; verify absent GPS fallback | Untested |
| GPS LED ring | Leave GPS for Home and other apps repeatedly with guidance on and off: the ring goes dark at every exit and no app inherits a pixel. While moving with known course, record the lit index for N/NE/E/SE/S/SW/W/NW (ground track is direction of motion, not enclosure facing); stationary/no fix blanks the ring; check 0%/100% intensity and every color preset | Untested |
| GPS Timepulse | Toggle in Home and GPS Settings; both show the same saved value; the receiver indicator follows without interrupting NMEA or restarting rate probing | Untested |
| GPS settings | With no receiver attached, breadcrumbs, retained files, Movement trigger, and Timepulse rows are shown unavailable and refuse changes in Home and GPS Settings; they become available once NMEA is detected | Untested |
| Breadcrumbs (#139) | With fresh position/UTC, stationary for several minutes: approximately one heartbeat per 60 seconds; verify no writes from stale fixes. Walk/drive at multiple speeds and movement-trigger settings, enforce speed-scaled minimum intervals and one record per UTC second, confirm UTC-day file names and 1-day/1-week/1-month/Forever retention including legacy-file safety; remove/reinsert SD without torn journal lines | Untested |
| Surveillance journal | Stationary receiver: repeated packets from one identity add no records; moving beyond the trigger adds one record per identity; live distances still update on every packet | Untested |
| SD handling | Remove/reinsert outside and during active logging/capture; verify errors and recovery without corruption | Untested |
| Audio | Recorder start/stop/playback and Audio Spectrum transition; verify no resource leakage | Untested |
| System UI | Input lock, application lock, status icon changes, display idle/wake and power button | Untested |
| Rotary input (#145/#149; hardware acceptance open) | Verify independent 1-ms ESP timer sampling and TWO03 latch behavior, physical direction, one intended action per detent, no jitter/backlog, slow/fast turns, reversals, bounce and interrupted wake/lock detents; complete the display-off/locked-input wake matrix during idle and scan/HTTPS load | Untested |
| Event-driven presentation (#149) | Exercise all 13 app runtimes, Home's Device/Network/OTA menus, shared modals, System Events uptime, Recorder, Thermal, radio/audio/GPS/Hotspots live data, and App Lock rows. Input/menu changes redraw immediately; meaningful data updates coalesce; static screens avoid redundant frames | Untested |
| Visual themes | Select Pixel Quest and each legacy theme; visit every app, nested settings, channel/source menus, and dialogs; verify title/status separation, editing cursors, battery/charging/locks, dense rows, thermal colors, idle/wake, and persisted selection after reboot | Untested |
| Networking | Home Wi-Fi connection, randomized station MAC, NTP/HTTPS probe and Hotspots exit cleanup; verify responsiveness around TLS cancellation and station teardown. Full-frame LCD SPI and some network teardown operations remain synchronous after #149 | Untested |
| Hotspots (#140/#143/#144) | Check Open/OWE/WPA2/WPA3 and Known/Inferred/manual profile picks, positive-only credential learning (no new DENY), 4-dB signal-improvement retry after failed automatic checks, tentative gateway-only CAM metadata, Live/History/Saved selection identity during reorder/removal, Home credential round trip, history replay/compaction/SD loss, evidence-based grouping splits, and fresh/stale/no-GPS behavior | Untested |
| OTA | Candidate update, reboot confirmation and rollback behavior; verify release identity and SHA | Untested |
| Power | Screen-off, deep sleep, physical power-off and return where supported | Untested |

**v0.4.0 candidate record:** SHA/tag: _not prepared_; board revision: _not recorded_; SD card/configuration: _not recorded_; GPS/nRF24/MLX90640 accessories, I2C wiring/pull-ups: _not recorded_; tester/date: _not recorded_. Replace `Untested` only with evidence; unavailable optional equipment is `Untested (hardware unavailable)`, never Pass.

Passing CI does **not** mark hardware passed. Run formatting, packaging tests and ESP-IDF build for the exact release commit. A previous PR candidate cannot validate the later release SHA. Report hardware discrepancies with logs and reproduction conditions.

## Lifecycle failure-injection acceptance

- With an active resource session that owns no managed allocations, request `app_session_ensure()` for another application: verify rejection without changing the active session, consumers, view, or services.
- Inject an initialization failure after one or more selected services initialize: verify reverse-order `stop()` calls, no ENTER into the failed destination, and successful subsequent session creation.
- Inject managed-view initialization failure after successful service initialization: verify initialized services are stopped and the manager takes the documented Home recovery path.
- Inject an unsuccessful initialization rollback stop: verify diagnostics expose the cleanup failure, the degraded session remains owned, and Home fallback cannot acquire another session. Restart is required before further navigation.
- Inject an aborted close where a persistent service fails to restart: verify the original app state remains allocated and failure is surfaced rather than silently implying a clean restoration.
- Check that committed memory/media-handle invariant failures remain distinct in transition diagnostics even when navigation has already committed.
