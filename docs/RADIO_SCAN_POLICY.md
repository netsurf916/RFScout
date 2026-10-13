# Radio scanning policy and settings alignment

Status: implementation in draft PR #111. Compact Sources, Wi-Fi settings, individual channel toggles, sequential/shuffled complete sweeps, dwell presets, and BLE discovery/duplicate-filter/duty presets are implemented for initial review. Firmware build and physical verification are outstanding. Active Wi-Fi wildcard probing is **not** implemented and is deliberately not exposed.

## Shared UI, app-local state

WiFi/BLE and Wardriving present the same Sources, Wi-Fi settings, and BLE settings navigation. Each app independently owns its persisted settings, migration, defaults, and runtime policy request. No global persisted scan configuration. Radio services enforce the applied policy and report failures.

## Wi-Fi settings

- Discovery: Passive (default) or Active (planned; not yet exposed for Wi-Fi). Passive uses the current `WIFI_MODE_NULL` plus promiscuous receive callback and framework-owned channel hopping. Active must use the ESP-IDF station scan API (`esp_wifi_scan_start()` with `WIFI_SCAN_TYPE_ACTIVE`, with an empty SSID/BSSID filter) in `WIFI_MODE_STA`, allowing the driver to transmit wildcard Probe Requests; do not probe stored SSIDs, associate, or handcraft frames. Active scans yield AP discovery records rather than equivalent raw 802.11 packets. ESP-IDF permits promiscuous capture in station mode, but the interaction with driver-owned scan channel hopping must be validated before attempting to combine them; RFScout should switch scan/receive policies explicitly and avoid concurrent competing channel schedulers. Handle `WIFI_EVENT_SCAN_DONE`, retrieve scan results, preserve per-app ownership and source cleanup, and ensure cancellation/app switching does not leave the station scanner running. Apply radio country/regulatory settings rather than assuming channels 1-14 are eligible for active transmission.
- Channel policy: Auto or Manual. Auto means RFScout-managed hopping, not driver-selected channels and not synonymous with passive reception.
- Manual: individual channel enable/disable controls, Sequential or Shuffled sweep order, dwell in milliseconds.
- Auto: grey out channel mask, order, and dwell controls. Preserve WiFi/BLE adaptive observation behavior; preserve Wardriving's existing uniform-coverage behavior until explicitly migrated to a shared policy.
- A shuffled sweep visits every selected channel once before reshuffling. Never repeatedly sample channels at random with replacement.
- Respect hardware and country-specific allowed channels.

### Empty-channel semantics

When the user disables the final enabled channel in Manual:
1. Persist the empty mask for **that app**.
2. Disable that app's Wi-Fi source, stop its Wi-Fi collector through the existing lifecycle/error-checked service path, and leave the Wi-Fi settings submenu.
3. If hardware stop fails, do not silently claim Wi-Fi is disabled; show the existing transition/error notice and keep state consistent with hardware.
4. When Wi-Fi is enabled again with an empty mask, restore the full allowed-channel mask before starting the collector. Do not alter the other app's settings.
5. If BLE is also disabled, show a clear disabled/no-sources state rather than silently re-enabling BLE.

## BLE settings

- Discovery: Passive (default) or Active (scan requests to scannable advertisers).
- Controller-managed advertising channels; no fake per-channel selector or shuffled BLE hopping.
- Expose scan duty presets (scan interval/window) and optional duplicate filtering with app-local persistence.
- Active discovery changes transmit behavior and should be clearly indicated in the UI.
- Validate window <= interval and propagate controller start/stop errors.

## Lifecycle and tests

- Changing policy while running must use service ownership and error paths, not a parallel radio-control paradigm.
- Verify Wi-Fi and BLE coexistence, SD recording boundaries in Wardriving, and that source disable/reenable does not restart disabled collectors.
- Unit tests: channel-mask transitions including last channel; empty-mask reenable; shuffled sweep coverage; independent app persistence; Auto disabled controls; regulatory mask; failed radio stop; BLE scan interval/window validation.
- Hardware validation: wildcard probing with promiscuous capture, passive/active BLE scan responses, capture data integrity and coexistence.

## Compact menu implementation (PR #111)

Both applications expose `Sources` with independent Wi-Fi and BLE on/off choices, and Wi-Fi/BLE setting submenus only when the corresponding source is enabled. The Wi-Fi settings submenu contains Channel policy (Auto/Manual), Channels (individual on/off rows), Scan order (Sequential/Shuffled) and Dwell time (100/200/250/500/1000 ms presets). BLE settings contain Discovery (Passive/Active), Duplicate filter (Off/On) and Scan duty (High/Medium/Low, fixed valid interval/window pairs).

The BLE menu configures NimBLE's scan request behavior; this remains subject to hardware verification, and Wardriving forbids changing sources or scan parameters while actively recording. An empty manual channel mask disables that application's Wi-Fi source after a successful stop. Re-enabling Wi-Fi restores the full configured 14-channel mask; regulatory/country-specific filtering needs additional validation. The scan-service policy is applied at source activation; each app keeps its own persisted record.

The original Wardriving top-level fixed-channel and dwell controls were removed to avoid conflicting settings. Full CI, menu navigation regression tests, active BLE scan behavior and on-hardware mixed-source tests are still required. No claims of production-readiness are implied.
