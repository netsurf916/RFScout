# Hotspots

Hotspots finds nearby Wi-Fi access points, checks whether each one provides Internet connectivity, and keeps a durable history of the ones that worked. It is RFScout's port of the standalone Hotspotter project, adapted to a 320x170 rotary interface. Shared Wi-Fi station, network-session, credential, journal, GPS, lifecycle, and memory contracts are defined in [Platform architecture](../ARCHITECTURE.md); future work belongs in [ROADMAP.md](../ROADMAP.md).

> **Active behavior:** unlike most RFScout applications, Hotspots **associates with third-party networks**. With automatic checking on (the default), it joins eligible access points by itself while the app is active. Leaving the app stops all scanning, association, and probing immediately.

## Scope

- **Live** results for the current visit live in app-session PSRAM and are released when the app is left.
- **History** of every access point that passed a green or yellow check is kept on SD and replayed on each visit, including with no radio activity and no GPS.
- **Saved networks** are the same profiles Home uses; there is no separate Hotspots credential store.
- Eligible security: **Open**, **OWE** (Enhanced Open), and **WPA2/WPA3 personal** (WPA2, WPA2/WPA3 transition, WPA3). WEP, WPA1 and 802.1X networks are listed as unsupported and never joined. Hidden networks are not reported by the passive scan.
- GPS is optional. Without a fix, everything works except location capture, distance/direction, and nearest-first sorting.

## Services and lifecycle

### Shared discovery, memory and history migration (PR #136)

**Target behavior; app migration pending.** Hotspots will subscribe to the same framework-owned Wi-Fi monitor and normalized access-point observations used by WiFi/BLE, with the shared beacon/probe-response parser supplying BSSID, SSID, channel, RSSI and carefully validated security metadata. Framework-owned monitor quiescence and station transitions will suspend discovery before association, DHCP/NTP and TLS, and restore monitoring after release; Hotspots will not maintain a parallel scan-result collection or its current 32-BSSID retrieval limit.

Hotspots retains its app-owned **128-BSSID Live budget** and its OPEN/OWE-over-secured admission/eviction preference, with active checks and manual requests protected. Replace **Missed scans** with a **last-observed elapsed-time timeout**, accounting for deliberate monitor interruptions, while keeping GPS-distance eviction (500 m default). Under capacity pressure, expire time/distance-stale entries first, then iteratively evict safe candidates by app-specific category, age and optionally RSSI; if no safe victim exists, reject admission rather than oversubscribe memory. Capacity is the budget to satisfy, not the eviction algorithm.

History and UI will use bounded SD-backed nearest-first queries and detail caching rather than retaining every characterized BSSID and history-sized scratch arrays. History remains authoritative on SD and must still support credential inference, epoch handling, grouping, compaction and missing-card recovery. Shared, versioned observation JSONL fields will be emitted through the existing SD abstraction; Hotspots-specific characterization records stay distinct and credentials never enter the journal. Until migration, the implementation, settings, and history behavior described below remain authoritative.

The descriptor declares the framework network-session and connectivity-probe services and requires delayed activation, so skipping through the app does not start the radio. The network session owns association, DHCP, NTP, MAC policy, timeout, and teardown; the app owns only candidate selection, credential resolution, and result interpretation. Both services are stopped by the app session before app memory is reclaimed. History opens on entry, replays in the background, and is flushed and closed when the app is left.

## Scanning and observation sessions

Hotspots alternates bounded **scan** and **check** phases, following Hotspotter's characterization policy:

1. A passive all-channel scan (no probe requests, about 200 ms per channel) refreshes every visible named BSSID with its SSID, channel, RSSI, and exact auth mode.
2. A check phase of up to **2.8 seconds** may start checks on eligible access points. The window only limits when new checks start; a check already running finishes or reaches its own timeouts.
3. The next passive scan begins.

Each BSSID has an independent observation session. A BSSID missing from **complete** passive scan snapshots stays listed as not currently visible until the configured **Missed scans** limit (default 3, range 1–10); counting complete scans rather than seconds prevents the check-window duration from producing spurious expiry. The station snapshot can contain at most 32 BSSIDs. When it returns all 32, that snapshot may be truncated, so any unlisted BSSID is not counted as missed and is not distance-expired. The Live view still marks unlisted BSSIDs as not currently visible; bounded-capacity eviction may still reclaim them under pressure. A BSSID whose SSID, security or auth mode changes starts a fresh observation session.

**Live retention.** A second, independent eviction rule removes an *absent* BSSID when the receiver is more than the configured **Evict beyond** distance (default 500 m, Off or 100–2,000 m in 100 m increments) beyond the BSSID's estimated observed coverage. For that Live observation session, a fresh GPS fix on each visible completed scan expands a latitude/longitude bounding box. The location estimate is the box center; its conservative coverage radius is half the larger great-circle diagonal measured in metres. Eviction occurs when the distance from the current fix to the center, minus the coverage radius, exceeds the configured distance. A BSSID seen in the current scan is never distance-evicted, and without a fresh GPS fix or a valid Live location box the distance rule is skipped entirely. The Live box is independent of durable History location bounds.

**Capacity protection.** The Live table has a provisional hard maximum of 128 allocated BSSID entries (not simply 128 active entries). It refreshes all existing sightings and expires stale/out-of-range entries only after complete snapshots before admitting new ones. New OPEN/OWE candidates are admitted ahead of new secured candidates, regardless of driver scan order. At capacity, admission prioritizes evicting absent **secured** BSSIDs before absent **OPEN/OWE** BSSIDs, and uses least-recently-observed order within each class (insertion-order ties). A new secured BSSID does not displace an existing OPEN/OWE BSSID; current-scan sightings and queued manual checks are protected regardless of security. If every record is protected, new observations are skipped rather than exceeding the allocated ceiling. RSSI/network grouping presentation order is independent of eviction order. The cap is subject to validation against measured PSRAM, internal RAM and HTTPS/TLS peak headroom; neither the Live ceiling nor the distance policy limits the SD History index, whose memory usage must be assessed separately. Eviction of Live state never deletes saved network profiles or persisted History.

## Credentials

Secured networks use the shared saved-network profiles (see [Home](HOME.md) and [Architecture](../ARCHITECTURE.md#shared-network-profiles)). Several profiles may share an SSID; each profile learns the BSSIDs that accepted its passphrase and the BSSIDs that refused it. After every completed scan, and immediately after any credential change, each secured BSSID is resolved to one of these states:

| State | Tag | Meaning |
| --- | --- | --- |
| Known | — | A profile already associated with this exact BSSID and auth mode. Automatic checks use it. |
| Inferred | — | Exactly one same-SSID, auth-compatible profile has evidence: one of its known BSSIDs is in the same scan, or has history within the **Match distance** of the current fix. Automatic checks use it. |
| Ambiguous | `PICK` | Several profiles have evidence. Only a manual **Join with profile** checks it. |
| Unmatched | `PICK` | Same-SSID profiles exist but none has evidence here. Only a manual join checks it. |
| Rejected | `DENY` | Every compatible same-SSID profile was refused by this BSSID. |
| None | `KEY?` | No saved password for this SSID. |
| Unsupported | `N/A` | WEP, WPA1, 802.1X or another mode the station cannot join. |

A profile is never used with a BSSID whose observed auth mode differs from the one it learned, and a WPA3-only BSSID is never joined under a WPA2 minimum. A successful secured association records the BSSID as associated with that profile, so later visits resolve it as Known. A join refused by the passphrase (authentication failure or handshake timeout) records the BSSID as rejected for that profile. Connectivity results are recorded separately in history; a yellow or red result does not change what a profile learned.

**Match distance** (Settings; Off, 50, 100, 200 or 500 m; default 100 m) controls how close the current fix must be to the history of a profile's known BSSID for it to count as evidence. It needs a GPS receiver and is shown unavailable without one. Profile resolution works without GPS through exact BSSIDs and same-scan overlap.

Passwords are copied out of the store only to start an association and are erased as soon as the attempt has started. They never appear in history, logs, telemetry, or the observation table.

## Connectivity check

One check for one BSSID performs:

1. **Network** — associate pinned to the selected BSSID and channel: without a password for Open/OWE (OWE remains required when advertised), or with the resolved shared profile for supported WPA2/WPA3 personal networks. Complete DHCP under the common framework policy. Association/DHCP failure marks the AP **red**.
2. **Time** — once DHCP succeeds, the framework captures LAN attributes and checks whether GPS or NTP has already provided trusted UTC within the last hour. If so, it skips a redundant NTP request and advances to READY; otherwise it attempts NTP (up to 15 seconds). Failure of a required NTP check leaves the session LIMITED, marks the AP **yellow**, retains LAN attributes, and **skips HTTPS**. Time readiness does not prove that this specific AP supports NTP.
3. **HTTPS** — only for READY sessions, perform one certificate-validated request to Cloudflare's `/cdn-cgi/trace`, bounded to **10 seconds**. HTTP 200 marks the AP **green** and records the public egress IP and Cloudflare colo; any other result marks it **yellow**.
4. **Leave** — read the gateway's MAC address from the station's ARP cache (joined checks only), release the network session, and return to scanning.

Each check inherits the platform-wide default of a fresh random, locally administered station MAC; the factory MAC is restored when the network session stops.

A failed recheck does not erase an earlier green or yellow result for the same observation session; the latest attempt's failure is shown separately in the detail view.

### Automatic checking

Automatic checking is **on by default** each time the app is entered, and starts only after history has finished loading:

- an eligible BSSID (Open, OWE, or secured with a Known or Inferred profile) is checked automatically once it is visible at **-80 dBm or stronger**;
- after a failed automatic attempt, the same BSSID is retried in the same observation session only when its RSSI improves by at least **4 dB** over the RSSI of the previous failed attempt;
- a green or yellow result ends automatic checks for that BSSID for its observation session; and
- among candidates, the strongest currently visible BSSID is checked first.

A manual **Recheck** or **Join with profile** bypasses the RSSI gates (not eligibility) and runs before the next automatic check. Turning automatic checking off stops new automatic checks; manual checks still work.

## History

Every green or yellow check is appended to the app's SD journal, `hotspots/history/hotspots-*.jsonl` (256 KiB segments, up to 32 retained), using the shared [SD journal](../ARCHITECTURE.md#sd-journals) format. Records hold the BSSID, SSID, auth mode, status, RSSI, gateway, netmask, DNS servers, gateway MAC, public IP and colo, and the receiver position when a fresh fix exists. Client IP addresses and credentials are never written.

- **Locations.** While a BSSID with history is visible, a `location` record is added each time the receiver has moved at least the system [movement trigger](../ARCHITECTURE.md#gps-movement-trigger) from that BSSID's previous logged point. A BSSID characterized without a fix therefore gains its location from the first later sighting with one. In memory each BSSID keeps the latitude/longitude range of its samples; its location is the center of that range, which can be recomputed from the journal.
- **Epochs.** A change of SSID, auth mode, gateway, netmask or DNS servers starts a new epoch for that BSSID: its counts and location range restart, so a re-addressed or repurposed access point is not merged with its past.
- **Loading.** Replay runs in the background, a bounded number of lines per poll, while Live scanning and drawing continue. The footer shows `LOAD n%`; checks start once replay completes. Malformed lines are skipped and counted (`n BAD` on the History footer); a torn final line is ignored.
- **Compaction.** When replay reads more than 1 MiB, or when **Compact history** is chosen in Settings, the journal is rewritten as one `summary` record per BSSID (latest details, success count, and location range) through the journal's atomic compaction. Records arriving meanwhile are kept and applied afterwards.
- **SD failures.** Unwritten records stay in memory and are retried; the footer shows `SD!` while the journal is unavailable.

## Network grouping

Live and History group BSSIDs into networks with Hotspotter's evidence rule. Only BSSIDs with the same SSID and auth mode are compared. Within that group they are one network unless their evidence conflicts: different gateway/netmask, or different gateway MAC, between two BSSIDs that both have it. Missing evidence never splits a network. A BSSID without evidence joins the network whose evidence-bearing members have the nearest location center, or the network with the most evidence-bearing members when it has no location. In Live, a BSSID that refused every same-SSID profile is kept apart from the networks those profiles reach.

When one SSID and auth mode splits into several networks, rows are labelled `SSID #1`, `SSID #2`, and so on, largest first. Individual BSSIDs always remain browsable under their network.

## Presentation and controls

Hotspots has three views. **App lock** off: landing-level rotary changes RFScout applications as usual. **App lock** on: landing-level rotary moves between Live, History and Saved. Long press on a landing view also moves to the next view. Select opens the current view's menu; rotary and select inside menus and details stay within Hotspots. Long press in a menu or detail returns one level.

- **Live** — networks currently observed this visit, ordered by their strongest visible BSSID. Each row shows the network label, a status tag summarizing its BSSIDs, the number of BSSIDs, and the strongest visible RSSI (`--` when none is visible). The footer shows the phase or running check and counts of open, OWE, and secured BSSIDs.
- **History** — networks from durable history, nearest first when GPS has a fresh fix (then green first, newest first). Rows show the label, `OK`/`LTD`, the number of BSSIDs, and the distance and compass direction to the nearest member (`--` without a fix). The footer shows the BSSID count, fix state, and any `BAD` or `SD!` indicators. With no history the view says **No known hotspots**, or **SD history unavailable** when the card cannot be read.
- **Saved** — the shared profiles, labelled `SSID #n` when several share an SSID, with the learned auth mode and the number of associated BSSIDs. Selecting one opens the shared profile screen: masked password with **Show**, **Edit password**, and **Forget**. Changes apply to Home immediately.

Each view's menu starts with **Back**, **App lock**, **Auto check**, and **Settings** (**Match distance**, **Missed scans**, **Evict beyond**, and **Compact history**), followed by the view's rows. Selecting a Live or History network opens its BSSID list; selecting a BSSID opens its detail.

**Live BSSID detail** shows the network label, BSSID and channel, RSSI or not-visible state with auth mode and check count, the credential state, status, the latest attempt's outcome (including a rejected password), client IP, gateway, netmask, gateway MAC, DNS servers, and public IP with Cloudflare colo. LIMITED results keep the LAN attributes even though HTTPS was skipped. Actions:

- **Recheck** — queue a manual check (visible, eligible BSSIDs only).
- **Password** — open the profile used for this BSSID.
- **Join with profile** — for Ambiguous, Unmatched or Rejected BSSIDs: choose an auth-compatible profile for one manual check.
- **Add password** — for secured BSSIDs without a usable profile: replace a same-SSID profile's password or create a new profile, then check this BSSID with it.

**History BSSID detail** is read-only: BSSID and auth mode, status, validation time (UTC) and success count, RSSI at the last success, gateway and netmask, gateway MAC, DNS servers, public IP and colo, and the location center with distance and direction.

Status tags carry their meaning as text, not only color:

| Tag | Meaning |
| --- | --- |
| `OK` (green) | Joined, trusted time was available or NTP succeeded, and verified HTTPS returned 200 |
| `LTD` (yellow) | Joined, but required time synchronization failed or the HTTPS check did not pass |
| `FAIL` (red) | Association or DHCP failed and no earlier success exists in this session |
| `Q` | Queued for a manual check |
| `JOIN` / `HTTPS` | Check in progress at that stage |
| `<-80` | Waiting for the automatic RSSI gate |
| `KEY?` / `PICK` / `DENY` / `N/A` | Secured BSSID that needs a password, a profile choice, was refused, or is unsupported |
| `-` | Not yet checked |

## Logging

**System event logs (when SD logging is enabled):** Each completed check emits a `type:"hotspot"` record with BSSID, channel, RSSI, security, outcome, failing stage, the `ntp` field (meaning the network session's time prerequisite was satisfied, **not** proof of an NTP response from that AP), HTTP status, and whether the check was manual. These event-log records omit SSIDs, client addresses, and public IPs. Periodic telemetry reports visible, green, yellow, red, and check counts.

**Hotspots history journal (separate from system event logging):** Successful green and yellow characterizations are retained in the `hotspots/history` SD collection as described in [History](#history). These records **do** include the SSID and public egress IP when known, but never credentials or the client IP. This journal is independent of the system's optional `type:"hotspot"` event logs.

## Current limitations

- Yellow does not distinguish captive portals, DNS filtering, or blocked NTP from other restrictions.
- Failed attempts are not kept in history.
- Grouping assumes same-SSID/auth BSSIDs are one network unless evidence conflicts, so unrelated networks can share a row until they are characterized. HESSID/802.11u and 802.11r mobility-domain evidence are not used.
- A profile is matched to the exact auth mode it learned, so a profile learned on a WPA2 BSSID is not offered for a WPA2/WPA3 transition BSSID of the same SSID; that BSSID needs its own profile (**Add password** → New profile).
