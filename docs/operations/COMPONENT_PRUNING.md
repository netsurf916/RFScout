# ESP-IDF component pruning experiment

This optimization is intentionally scoped to RFScout's application build; it does not change
application behavior or patch third-party source. The initial baseline is main commit
`db4e35782b8ab507f77ebccf18f3b561a0cac1a3`.

## What changes

- Enable ESP-IDF's minimal component build after `project.cmake` is included and before
  `project()`, so unrelated components do not compile solely because they are installed.
- Declare `esp_psram` explicitly: external PSRAM is a hard requirement for RFScout.
- Declare managed LittleFS explicitly: internal persistent storage must remain available.
- Disable NimBLE security manager, while retaining BLE observer and active advertisement scans.
- Disable SoftAP and enterprise Wi-Fi support, while keeping Wi-Fi station, monitor,
  WPA2/WPA3-Personal, and OWE station features.
- Build mbedTLS in client-only TLS mode. Preserve certificate bundles, hostname verification,
  and the cryptographic services used by persistent storage and OTA.

## Validation before merge

The CI build and formatting jobs must pass. The following device-level behavior must
also be checked before release:

1. Boot/board discovery, display, encoder, buttons, and PSRAM allocation.
2. Passive and active BLE advertisement collection, UUID/name parsing, start/stop/restart.
3. Wi-Fi passive/active scan, station joining, WPA2/WPA3-Personal, OWE, DHCP/DNS and NTP.
4. Hotspots HTTP camera fingerprinting and certificate-validated HTTPS connectivity probe.
5. OTA catalog download, firmware acquisition, SHA-256 checking, rollback/reboot.
6. Internal LittleFS save/restart/read, removable SD/FatFS mount and journaling.
7. USB debug and recovery (no deliberate JTAG/panic-handler changes in this PR).

## Evidence and measurement

Compare the baseline and this branch using *the same* ESP-IDF 6.1 toolchain:

- `idf.py size`, `idf.py size-components`, and the generated application ELF/map file.
- `build/project_description.json` component list (compiled components, not just linked code).
- Full clean-build wall time on the same CI runner; remote CI noise means one build is not
  a reliable performance measurement.
- A repeat Coverity capture, looking at compiled third-party sources and reachable
  findings. A lower Coverity count is not by itself proof that defects were fixed.

### Follow-up candidates, not disabled yet

Inspect whether `console`, `spiffs`, `protocomm`, `esp_hid`, `esp_gdbstub`,
`wear_levelling`, and Bluedroid are still compiled or linked. Do not forcibly exclude
transitive dependencies. Treat Bluetooth, TLS, and WPA pruning as configuration trials
until actual builds and device smoke tests confirm them.

**Scope:** This optimization PR is independent of PR #152, which handles the 11
RFScout-owned Coverity findings. No vendor code is modified.
