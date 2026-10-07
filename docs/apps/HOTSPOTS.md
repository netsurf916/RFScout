# Hotspots

Hotspots is RFScout's realtime view of nearby **open** Wi-Fi access points and whether each one currently provides Internet connectivity. It is a lightweight derivative of the standalone Hotspotter project. Shared Wi-Fi station, connectivity-probe, lifecycle, and memory contracts are defined in [Platform architecture](../ARCHITECTURE.md); future work belongs in [ROADMAP.md](../ROADMAP.md).

> **Active behavior:** unlike every other RFScout application, Hotspots **associates with third-party networks**. With automatic checking on (the default), it joins eligible open access points by itself while the app is active. Leaving the app stops all scanning, association, and probing immediately.

## Scope

- Realtime only. Results live in app-session PSRAM and are released when the app is left; re-entering starts with an empty view.
- No GPS, location, distance, or history.
- No persistent storage of any kind: no SD media collections and no app configuration records. The automatic-check toggle resets to **On** each time the app is entered.
- No credentials or profiles. Only plain **Open** and **OWE** (Enhanced Open) networks are eligible. Secured networks are counted but never joined; hidden networks are not reported by the passive scan.

## Services and lifecycle

The descriptor declares the framework network-session and connectivity-probe services and requires delayed activation, so skipping through the app does not start the radio. The network session owns association, DHCP, NTP, MAC policy, timeout, and teardown; the app owns only candidate selection and result interpretation. Both services are stopped by the app session before app memory is reclaimed.

## Scanning and observation sessions

Hotspots alternates bounded **scan** and **check** phases, following Hotspotter's characterization policy:

1. A passive all-channel scan (no probe requests, about 200 ms per channel) refreshes every visible BSSID with its SSID, channel, RSSI, and security.
2. A check phase of up to **2.8 seconds** may start checks on eligible access points. The window only limits when new checks start; a check already running finishes or reaches its own timeouts.
3. The next passive scan begins.

Each BSSID has an independent observation session. A BSSID missing from one or two consecutive completed scans stays listed as not currently visible; the third consecutive miss ends its session and removes it from the view together with its result. A later reappearance starts a fresh session.

## Connectivity check

One check for one open or OWE BSSID performs:

1. **Network** — associate pinned to that BSSID with no credentials (OWE networks require OWE), then complete DHCP under the common framework policy. Association/DHCP failure marks the AP **red**.
2. **Time** — once DHCP succeeds, the framework captures LAN attributes and attempts NTP. NTP failure leaves the session LIMITED, marks the AP **yellow**, retains the LAN attributes, and **skips HTTPS**. A successful sync promotes the session to READY and sets the device clock.
3. **HTTPS** — only for READY sessions, perform one certificate-validated request to Cloudflare's `/cdn-cgi/trace`, bounded to **10 seconds**. HTTP 200 marks the AP **green** and records the public egress IP and Cloudflare colo; any other result marks it **yellow**.
4. **Leave** — release the network session and return to scanning.

Each check inherits the platform-wide default of a fresh random, locally administered station MAC. This is the same policy used by Home/OTA connections. The factory MAC is restored when the network session stops; if the factory MAC could not be captured, the framework continues with the current station MAC rather than failing the check.

A failed recheck does not erase an earlier green or yellow result for the same observation session; the latest attempt's failure is shown in the detail view.

### Automatic checking

Automatic checking is **on by default** each time the app is entered:

- an eligible BSSID is checked automatically once it is visible at **-80 dBm or stronger**;
- after a failed automatic attempt, the same BSSID is retried in the same observation session only when its RSSI improves by at least **4 dB** over the RSSI of the previous failed attempt;
- a green or yellow result ends automatic checks for that BSSID for its observation session; and
- among candidates, the strongest currently visible BSSID is checked first.

A manual **Recheck** from the detail view bypasses the RSSI gates (not eligibility) and runs before the next automatic check. Turning automatic checking off stops new automatic checks; manual rechecks still work.

## Presentation and controls

The landing view lists the strongest visible eligible access points with their status and RSSI, plus a footer showing the current phase or check and counts of open, OWE, and ignored secured networks. Rotary navigation on the landing view changes applications as usual.

Pressing **Select** opens the browser:

- **Back** — return to the landing view;
- **Auto check** — toggle automatic checking for this visit; and
- one row per eligible BSSID, sorted strongest first, showing SSID, status, and RSSI. Selection follows the BSSID while the list re-sorts.

Selecting an access point opens its detail view: SSID, BSSID, channel, RSSI or not-visible state, security, status, current check stage, the last attempt's outcome, client IP, gateway, netmask, DHCP-provided DNS servers, public IP, and Cloudflare colo when known. LIMITED/NTP-failed results retain and display the LAN attributes even though HTTPS is skipped. **Recheck** requests a manual check while the BSSID is visible.

Status column values:

| Value | Meaning |
| --- | --- |
| `OK` (green) | Joined, NTP succeeded, verified HTTPS returned 200 |
| `LTD` (yellow) | Joined, but NTP failed or the HTTPS check did not pass |
| `FAIL` (red) | Association or DHCP failed and no earlier success exists in this session |
| `Q` | Queued for a manual recheck |
| `NETWORK` / `HTTPS` | Check in progress at that stage |
| `<-80` | Waiting for the automatic RSSI gate |
| `-` | Not yet checked |

## Logging

When SD logging is enabled, each finished check writes one `type:"hotspot"` record with the BSSID, channel, RSSI, security, outcome, failing stage, NTP result, and HTTP status. SSIDs, client addresses, and public IPs are not logged. The periodic telemetry record reports counts of visible, green, yellow, and red access points.

## Current limitations

- Yellow does not distinguish captive portals, DNS filtering, or blocked NTP from other restrictions.
- Results are not retained after leaving the app, by design.
- Same-SSID BSSIDs are listed individually; there is no network grouping.
