# Radio scanning policy and settings alignment

Status: implementation under review in PR #111. Compact Sources, Wi-Fi settings, individual channel toggles, sequential/shuffled complete sweeps, dwell presets, and BLE discovery/duplicate-filter/duty presets are implemented for initial review. Firmware build and physical verification are outstanding. Active Wi-Fi wildcard probing is **not** implemented and is deliberately not exposed.

## Shared UI, app-local state

WiFi/BLE and Wardriving present the same Sources, Wi-Fi settings, and BLE settings navigation. Each app independently owns its persisted settings, migration, defaults, and runtime policy request. No global persisted scan configuration. Radio services enforce the applied policy and report failures.

## Wi-Fi settings

- Discovery: Passive packet capture remains the baseline; opportunistic Active discovery is planned with the conditional scheduler below (not yet exposed in firmware). Passive uses the current `WIFI_MODE_NULL` plus promiscuous receive callback and framework-owned channel hopping. Active must use the ESP-IDF station scan API (`esp_wifi_scan_start()` with `WIFI_SCAN_TYPE_ACTIVE`, with an empty SSID/BSSID filter) in `WIFI_MODE_STA`, allowing the driver to transmit wildcard Probe Requests; do not probe stored SSIDs, associate, or handcraft frames. Active scans yield AP discovery records rather than equivalent raw 802.11 packets. ESP-IDF permits promiscuous capture in station mode, but the interaction with driver-owned scan channel hopping must be validated before attempting to combine them; RFScout should switch scan/receive policies explicitly and avoid concurrent competing channel schedulers. Handle `WIFI_EVENT_SCAN_DONE`, retrieve scan results, preserve per-app ownership and source cleanup, and ensure cancellation/app switching does not leave the station scanner running. Apply radio country/regulatory settings rather than assuming channels 1-14 are eligible for active transmission.
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

## Planned active Wi-Fi discovery policy (design only; not implemented)

Active Wi-Fi discovery is an **opportunistic supplement** to continuous passive promiscuous packet capture, never a replacement for raw 802.11 capture or a mutually exclusive Passive/Active operating mode. The proposed policy is **enabled** in this design profile, but active Wi-Fi firmware, menu controls, persistence and scheduler do **not** yet exist in PR #111. Existing firmware continues passive Wi-Fi capture only. When implementing, retain a passive-only default for existing installations unless users explicitly opt into transmitting Probe Requests; the On value below is the requested design configuration, not a deployed default.

### Conditional Wi-Fi menu

```text
Wi-Fi settings
  Channel policy            Auto / Manual
  Channels                  [Manual only]
  Scan order                [Manual only]
  Dwell time                [Manual only]
  Active discovery          On                 [Off / On]
  Active scanning policy    >                  [visible when On]
    Use GPS                 On                 [Off / On]
    Minimum distance        50 m               [GPS On]
    Stationary duration     10 sec             [GPS On]
    GPS unavailable         Timed fallback     [GPS On; Timed / Skip]
    Minimum interval        120 sec            [GPS Off OR Timed fallback]
    Active duty ceiling     10%                [always shown]
    Back
```

Hide inapplicable controls rather than leaving selectable controls that have no effect. The timed interval setting remains saved when GPS is enabled, even if normally hidden: it governs the configured GPS-unavailable fallback. Keep a bounded number of visible rows and scroll to the selection on the RFScout screen. Source gating continues to apply: these menus are unavailable while that app's Wi-Fi source is disabled. Wardriving must also prevent changes during a recording.

### Policy inputs and defaults

| Setting | Requested design profile | Behavior |
| --- | --- | --- |
| Active discovery | **On** | Authorize occasional wildcard active AP discovery; passive capture remains primary |
| Use GPS | **On** | Prefer location-triggered active discovery with valid, accurate fixes |
| Minimum displacement | **50 m** | Distance from the last completed active scan's accepted location, not consecutive fixes |
| Stationary duration | **10 sec** | Stable stationary interval after sufficient movement |
| GPS unavailable | **Timed fallback** | Use the timed eligibility rule if reliable GPS data is unavailable |
| Minimum timed interval | **120 sec** | Earliest possible subsequent active burst under the timed policy; not an obligation to scan |
| Active duty ceiling | **10%** | Hard upper bound on the fraction of elapsed wall-clock time occupied by active scans, including startup/transition cost |

The duty ceiling applies with **or without GPS**; GPS does not bypass it. Limit each burst's duration and channel coverage, and consider smaller channel batches so passive receive gaps remain short. Enforce the radio's configured regulatory domain for active transmissions. A change of scan policy never resets or expands Wardriving's retained PCAPNG packet count; active discovery results are marked separately from raw captured frames.

### Scheduler state machine

```text
DISABLED
  Active discovery Off -> remain passive; no active scans
  Active discovery On  -> PASSIVE_CAPTURE

PASSIVE_CAPTURE
  Continue normal channel hopping, observation, and raw packet capture
  At a safe passive sweep boundary -> CHECK_ELIGIBILITY

CHECK_ELIGIBILITY
  Wi-Fi source disabled / app owns no radio / recording policy disallows -> PASSIVE_CAPTURE
  GPS On and reliable fix:
    Require displacement >= 50 m from the last *completed* active scan location
    AND stationary for >= 10 sec
  GPS On but unusable fix:
    Timed fallback -> require >= 120 sec since last completed active burst
    Skip -> PASSIVE_CAPTURE
  GPS Off:
    Require >= 120 sec since last completed active burst
  In every case: enforce <= 10% rolling active duty budget,
    minimum cooldown, permitted channels, and capture priority
  Not eligible -> PASSIVE_CAPTURE
  Eligible -> PREPARE_ACTIVE_BURST

PREPARE_ACTIVE_BURST
  Pause the framework's passive channel-hop scheduler at a safe boundary
  Coordinate sole Wi-Fi scan ownership with connection/OTA services
  Configure native ESP-IDF STA active scan without SSID/BSSID targeting
  Setup/ownership failure -> RECOVER_PASSIVE

ACTIVE_BURST
  Native ESP-IDF scan transmits wildcard Probe Requests on allowed channels
  Respect bounded channel batch, duration, timeout and cancellation
  Collect AP records separately; do not label scan records as captured frames
  Completion / timeout / error / app exit -> RECOVER_PASSIVE

RECOVER_PASSIVE
  Stop/cancel scan as needed; restore promiscuous reception and hop ownership
  Reconcile scan-discovered AP observations with source provenance
  Update cooldown/budget, and store last scan location only after success
  If recovery fails, surface radio failure rather than claiming capture resumed
  Successful restoration -> PASSIVE_CAPTURE
  App exit or source disabled -> DISABLED
```

The first eligible burst may occur only after a meaningful passive capture window, never immediately on menu activation. Prevent concurrent ESP-IDF scans, avoid hand-crafted directed probes, and do not invoke active scanning in the promiscuous RX callback. Stationary detection must use multiple GPS samples and position-accuracy uncertainty, not only a speed field, to prevent GPS jitter from generating repeated scans. If a previously scanned location cannot be established, require an initial stationary confirmation plus cooldown before the first GPS-triggered burst. If the system is receiving substantial raw packet traffic, defer active discovery when practical while retaining a bounded retry/deferral policy. An active scan interrupts full-fidelity passive coverage even when promiscuous mode remains technically available during STA scanning; record these gaps if capture-session metadata permits.

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
