# Hotspots

Hotspots finds nearby Wi-Fi access points, checks whether each one provides Internet connectivity, and keeps a durable history of the ones that worked. It is RFScout's port of the standalone Hotspotter project, adapted to a 320x170 rotary interface. Shared Wi-Fi station, network-session, credential, journal, GPS, lifecycle, and memory contracts are defined in [Platform architecture](../ARCHITECTURE.md); future work belongs in [ROADMAP.md](../ROADMAP.md).

> **Active behavior:** unlike most RFScout applications, Hotspots **associates with third-party networks**. With automatic checking on (the default), it joins eligible access points by itself while the app is active. Leaving the app stops all scanning, association, and probing immediately.

## Scope

- **Live** results for the current visit live in app-session PSRAM and are released when the app is left.
- **History** of every access point that passed a green or yellow check is kept on SD and replayed on each visit, including with no radio activity and no GPS.
- **Saved networks** are the same profiles Home uses; there is no separate Hotspots credential store.
- Eligible security: **Open**, **OWE** (Enhanced Open), and **WPA2/WPA3 personal** (WPA2, WPA2/WPA3 transition, WPA3). WEP, WPA1 and 802.1X networks are listed as unsupported and never joined. Hidden networks without an SSID are not entered in the Live list.
- GPS is optional. Without a fix, everything works except location capture, distance/direction, and nearest-first sorting.

## Services and lifecycle

### Shared discovery migration (PR #136)

Hotspots now uses the same framework-owned `radio_monitor` and channel scheduler as WiFi/BLE. The shared beacon/probe-response decoder provides BSSID, SSID, receiver channel, RSSI and conservatively validated authentication evidence. A **64-item bounded callback queue** coalesces AP identities on the Wi-Fi callback task; the app task drains it into the separate **128-BSSID Live table**, with no station scan-result array or 32-result upstream ceiling. Queue overflow is counted and surfaced as telemetry (`ap_queue_drops`); admission failures are counted (`ap_rejected`). This queue is not a population ceiling: it is drained repeatedly during a sweep. The monitor is stopped and its callbacks quiesced **before** station association, DHCP/NTP and TLS. After each check, Hotspots stops the connectivity probe, releases the network session, and retries failed teardown before allowing another monitor sweep. A failed driver stop, unfinished callback, TLS worker, or station event drain never counts as a successful handoff.

Hotspots now uses **Expire after** (30–300 s, default 60 s) measured on a logical clock that only advances while the monitor is receiving, so connection/TLS interruptions do not falsely age nearby BSSIDs. The location eviction rule remains optional, defaulting to 500 m beyond observed coverage bounds. Normal expiration removes overdue, unqueued entries not heard during the current sweep; on capacity pressure, guest-capable BSSIDs preferentially reclaim not-yet-heard secured records, then older/weak-RSSI eligible records. Already-observed-in-sweep and queued-manual records are never displaced, and admission is rejected when no eligible victim exists. The app settings format version changes intentionally; old missed-scan settings are rejected and return to defaults.

**Still planned elsewhere in #136:** bounded SD-backed History projections, common observation journal serialization, and WiFi/BLE's own memory-budget/GPS policy. Hotspots-specific characterization remains journaled through `media_journal`, and Live eviction never deletes History or saved credentials.

The descriptor declares the shared Wi-Fi monitor, framework network-session and connectivity-probe services and requires delayed activation, so skipping through the app does not start the radio. The network session owns association, DHCP, NTP, MAC policy, timeout, and teardown; the app owns only candidate selection, credential resolution, and result interpretation. Both services are stopped by the app session before app memory is reclaimed. History opens on entry, replays in the background, and is flushed and closed when the app is left.

## Scanning and observation sessions

Hotspots alternates bounded **scan** and **check** phases, following Hotspotter's characterization policy:

1. A receive-only 3.2-second monitor sweep hops across the standard 14 Wi-Fi channels (nominal dwell ~120 ms per channel), collecting named beacon/probe-response APs asynchronously. The app drains the bounded callback queue throughout the sweep and coalesces each BSSID into its Live record.
2. The app updates last-seen timestamps during the sweep without clearing existing visibility. After the monitor has stopped and callbacks have quiesced, it expires stale/distant records, resolves saved profiles and may perform bounded Open/OWE/credentialed checks in its check window. The radio is never in monitor mode during association or HTTPS/TLS.
3. The next monitor sweep begins after the check window and successful connectivity-probe/station cleanup plus settling. Teardown failure enters a retry phase; result/history are recorded only once.

Each BSSID has an independent Live observation session. **Visibility is timestamp-driven, never based on a missed-sweep count:** an AP is displayed as recently visible when last observed within 8 seconds of active monitor receive time, spanning more than two full 3.2-second sweeps without blinking at a sweep boundary. After that it is shown as absent/dimmed, but its Live record is retained until **Expire after** (default 60 seconds of monitor receive time) or geographic eviction removes it. Association/TLS downtime does not advance either clock. A BSSID whose SSID, security or auth mode changes starts a fresh Live session. No **Missed scans** setting or scan-count expiration policy remains.

**Live retention.** A second, independent eviction rule removes an *absent* BSSID when the receiver is more than the configured **Evict beyond** distance (default 500 m, Off or 100–2,000 m in 100 m increments) beyond the BSSID's estimated observed coverage. For that Live observation session, fresh GPS at an observed BSSID expands a latitude/longitude bounding box. The location estimate is the box center; its conservative coverage radius is half the larger great-circle diagonal measured in metres. Eviction occurs when the distance from the current fix to the center, minus the coverage radius, exceeds the configured distance. A BSSID seen in the current monitor sweep is never distance-evicted, and without a fresh GPS fix or a valid Live location box the distance rule is skipped entirely. The Live box is independent of durable History location bounds.

**Capacity protection.** The Live table has a provisional hard maximum of 128 allocated BSSID entries, independent of the bounded AP callback queue. Each monitor sweep tags fresh sightings only for admission protection and GPS distance eviction; the marker never controls UI visibility or time expiration. Only entries not heard this sweep and not queued for manual work can yield to a new AP when full. Admission prefers evicting absent **secured** BSSIDs before absent **OPEN/OWE** BSSIDs and otherwise prioritizes oldest observations, using weaker RSSI as a tie-breaker. A secured newcomer cannot displace an Open/OWE entry. When all entries are protected, the new candidate is rejected rather than growing memory. Order of receipt is not a complete scan ranking; some co-visible APs may be unrepresented under saturated capacity. The cap is subject to measured PSRAM/internal RAM and HTTPS/TLS peak headroom; neither the Live ceiling nor the distance policy limits the SD History index.

Eviction of Live state never deletes saved network profiles or persisted History.

### Planned: Hotspots-only admission priorities (follow-up PR)

> **Design specification, not current behavior.** The following hierarchy is intended for a
> follow-up PR based on #136. Until implemented, the capacity policy in the preceding
> paragraph remains authoritative. This policy belongs to the Hotspots/Hotspotter
> application model (`main/apps/hotspots/`), **not** to the shared wireless monitor,
> shared observation-retention arithmetic, WiFi/BLE, Wardriving, or a generic eviction engine.

**Purpose.** The 128-BSSID Live ceiling is a memory budget, not a scan population
limit. At capacity, keep making room for newly discovered *useful* BSSIDs without
letting a newcomer displace an incumbent in a higher admission tier. Capacity
pressure is intentionally allowed to evict an otherwise-unexpired record; normal
time/GPS expiration remains independent of admission priority.

| Priority (highest first) | Incoming BSSID | Entry it may replace at capacity |
| --- | --- | --- |
| 1 | Open or OWE (both guest-accessible) | Eligible entries in tiers 1–4 |
| 2 | Supported WPA2/WPA3 personal with an unambiguous, usable **Known** or **Inferred** saved profile | Eligible entries in tiers 2–4 |
| 3 | Supported WPA2/WPA3 personal without an automatically usable profile (None, Ambiguous, Unmatched or Rejected) | Eligible entries in tiers 3–4 |
| 4 | Unsupported or conservatively unknown security/authentication (including WEP, WPA1 and 802.1X) | Eligible entries in tier 4 only |

An incoming entry **must never evict a higher-priority tier**. Open and OWE
share the top tier for *guest accessibility*, not because Open offers the same
privacy as OWE. At less than 128 entries, an eligible named BSSID can be
recorded regardless of tier; these tiers govern **capacity admission only**.
No user-selectable eviction strategy or per-tier capacity quotas are planned.

**Full-table admission algorithm (Hotspots-owned):**

1. First apply existing timeout and optional GPS coverage-distance expiration
   to eligible, unprotected records. The configured *Expire after* (default 60 s)
   and *Evict beyond* (default 500 m) semantics do not change; GPS is optional.
   The logical receive clock still excludes association/TLS downtime.
2. If room remains, admit the new BSSID. Otherwise, classify the newcomer
   using the security evidence and credential state actually applicable to
   **that BSSID and authentication mode**, then consider only incumbent tiers
   of equal or lower priority. An incumbent with an active connectivity attempt
   or a queued manual check is never a victim.
3. Prefer reclaiming the **lowest-priority eligible tier**. Within that tier,
   evict the **least recently observed** BSSID (using `last_seen_ms` on the
   logical monitor clock); break equal-age ties with weaker RSSI and then a
   deterministic BSSID order. Distance and signal are **not** alternative
   user-selectable capacity rankings.
4. A current-sweep observation should ordinarily be favored over an otherwise
   equivalent stale candidate, but **current-sweep membership must not be an
   absolute capacity-admission barrier**. At saturation, allow the oldest
   eligible same-tier (or lower-tier) entry to yield to a new BSSID, even if
   that incumbent was observed earlier in the same sweep. Preserve absolute
   active-check/manual-work protection. Avoid pathological churn among equally
   fresh BSSIDs without imposing a new 32/128-BSSID discovery ceiling.
5. If no lawful victim exists, reject the newcomer within the 128-entry bound
   and count it in the existing admission-rejection telemetry; never allocate
   past the cap or evict a higher tier to make space.

**Admission-time credential resolution.** Today Hotspots resolves saved profiles
after the sweep, whereas full-table admission occurs as AP callbacks are drained.
The implementation must therefore determine a candidate's tier *before* choosing
a victim, using an app-owned, bounded/read-only credential lookup compatible with
the existing `hotspot_profiles_resolve` and history/match-distance evidence.
Do not infer tier 2 merely from an SSID match, a stored password, an ambiguous
choice or previously rejected credentials. If current evidence cannot establish
a Known/Inferred usable profile, classify conservatively; never promote a
candidate based on unverified security evidence. Re-evaluate affected incumbent
tiers when saved credentials change. Do not copy passwords into Live entries,
logs or telemetry, or perform station association in the admission path.

**Implementation and regression scope.** Keep tier classification and victim
selection inside `main/apps/hotspots/` with app-local tests. Cover all four
incoming tiers against each incumbent tier, top-tier-to-top-tier replacement
under saturation, last-seen/RSSI ordering, full-table admission with all 128
incumbents seen during the current sweep, manual and active-check protection,
conservative/changed credential resolution, normal time/distance expiry,
128-entry allocation safety, and stable behavior under repeated observations.
The existing shared `radio_monitor`, `observation_retention`, WiFi/BLE and
Wardriving contracts are deliberately unchanged.

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

Each view's menu starts with **Back**, **App lock**, **Auto check**, and **Settings** (**Match distance**, **Expire after**, **Evict beyond**, and **Compact history**), followed by the view's rows. Selecting a Live or History network opens its BSSID list; selecting a BSSID opens its detail.

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
