# Release hardware acceptance checklist

This is a repeatable **validation procedure for implemented behavior**, not a feature plan. Record device revision, firmware commit/tag, SD card, GPS/nRF24 accessories, tester, date, and pass/fail notes in the release review. Any improvement proposals found during testing belong only in [Roadmap](ROADMAP.md); code defects belong in GitHub issues or corrective PRs.

| Area | Acceptance check | Result |
| --- | --- | --- |
| Startup | Cold boot, detect supported board, render display, navigate Home | Untested |
| App lifecycle | Switch repeatedly among Home, WiFi/BLE, Wardriving, GPS, CC1101, nRF24 (if present), Recorder; watch for hangs or retained workers | Untested |
| Radio sources | Toggle Wi-Fi/BLE independently; confirm disabled sources stop and stale observations disappear | Untested |
| Wi-Fi channel policy | Auto/Manual, individual mask, sequential/shuffled sweep, empty-mask disable and reenable; verify after restart | Untested |
| BLE discovery | Passive/Active, duplicate filter, duty presets; validate observed scan responses and Wi-Fi coexistence | Untested |
| Wardriving | Start/stop PCAPNG capture, verify packet timestamps/Wireshark decoding and SD filenames | Untested |
| Wardriving policy | Confirm radio settings cannot change during recording and resume correctly after stop | Untested |
| GPS | Detect NMEA without fix, acquire valid fix, invalidate stale fix, inspect trip distance/reset and position metadata | Untested |
| GPS/nRF24 | Verify UART priority with GPS connected and nRF24 present; verify absent GPS fallback | Untested |
| SD handling | Remove/reinsert outside and during active logging/capture; verify errors and recovery without corruption | Untested |
| Audio | Recorder start/stop/playback and Audio Spectrum transition; verify no resource leakage | Untested |
| System UI | Input lock, application lock, status icon changes, display idle/wake and power button | Untested |
| Visual themes | Select Pixel Quest and each legacy theme; visit every app, nested settings, channel/source menus, and dialogs; verify title/status separation, editing cursors, battery/charging/locks, dense rows, thermal colors, idle/wake, and persisted selection after reboot | Untested |
| Networking | Home Wi-Fi connection, randomized station MAC, NTP/HTTPS probe, Hotspots exit cleanup | Untested |
| OTA | Candidate update, reboot confirmation and rollback behavior; verify release identity and SHA | Untested |
| Power | Screen-off, deep sleep, physical power-off and return where supported | Untested |

Passing CI does **not** mark any hardware item passed. Run formatting, packaging tests, and ESP-IDF build for the exact release commit independently. Hardware discrepancies should be reported with logs, steps to reproduce, and whether a cold reboot is required.
