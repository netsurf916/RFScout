# Hotspots service fingerprinting (experimental design)

This page documents an **experimental, bounded local HTTP probe** plus the metadata
classifier and proposed later integration. On a Hotspots LIMITED connection,
the app now sends one HTTP HEAD request to the same-subnet DHCP gateway
during its normal station-session window. No wider network discovery exists yet.
See [Hotspots](apps/HOTSPOTS.md) for the live UI contract.

## Purpose and independent evidence

Hotspots' existing green/yellow/red outcome describes **Internet connectivity**.
In particular, yellow/LTD means association and DHCP succeeded but trusted time
or the external HTTPS check did not; it does not imply a camera. The DHCP gateway
may be a router rather than the device of interest.

Introduce general-purpose, uncertain **network service characterization** independently
of the surveillance detection database. `data/surveillance_signatures.csv` is curated
for the existing surveillance classification contract, and **must not** be used as
a generic camera fingerprint database. A generic HTTP 401, web server, or RTSP
advertisement is not proof of a camera.

## Initial committed slice

`services/wireless/service_fingerprint.[ch]` is a pure allocation-free classifier
for **supplied** bounded HTTP status/header, DNS-SD service-type and validated
WS-Discovery device-type metadata. It performs no Wi-Fi association, network
requests, XML parsing, credential handling, image fetching or persistent writes.
The caller supplies explicit field lengths, with 256-byte per-field limits.
Results are an evidence bitset, not a categorical camera determination or a
confidence percentage. In particular:

- HTTP status and basic authentication challenges indicate only web/auth services.
- A self-described camera/ONVIF HTTP banner is a **hint**, not a guarantee.
- `_rtsp._tcp` indicates a streaming-service advertisement, not a camera.
- `image/jpeg` and multipart responses indicate media response metadata only.
- `NetworkVideoTransmitter` is recorded only after a caller has validated
  WS-Discovery XML, namespace and QName; this module receives just the local name.
  **Compatibility exception:** the local-name comparison is ASCII case-insensitive
  even though XML QName local parts are formally case-sensitive. The owner explicitly
  prefers tolerance for nonstandard vendor implementations. Length and spelling
  remain exact apart from case; a match is only *tentative* service evidence,
  not proof of ONVIF conformance, surveillance, or authorization.
  No unauthenticated discovery response should be treated as authorization.

HTTP `Content-Type` identification is still restricted to the actual media-type
token before `;` parameters; case tolerance must not turn parameter text into
media evidence. These are separate interoperability and parsing decisions.

The current recognition literals are intentionally tiny illustrative heuristics,
**not** a complete vendor fingerprint database. Do not translate their presence
into a surveillance alert. Host regression coverage is in
`tests/test_service_fingerprint.py`.

## First gateway-only probe and CAM presentation (experimental)

After either NTP timeout or HTTPS failure has produced a LIMITED result, a
single asynchronous HTTP `HEAD /` request is attempted against the DHCP gateway
on port 80, with a 2.5-second request timeout and 3.5-second scheduler guard.
The gateway must be on the connected subnet. The probe reuses the existing
framework-managed connectivity-probe worker, cancels on app exit, never
follows redirects, submits credentials, downloads response bodies, attempts
additional endpoints, or scans other LAN hosts. It cannot establish that the
gateway is the observed BSSID/AP: gateway service identity is separate.

The existing bounded metadata classifier evaluates only the HTTP status and
response headers. An explicit camera/ONVIF server or authentication banner
produces tentative camera evidence. **Generic HTTP, HTTP 401, or JPEG alone does
not produce CAM.** Only a connection that already failed Internet verification
(LTD / internal YELLOW) may display the three-character **CAM** overlay. The
underlying connectivity result, reason, and diagnostics remain LIMITED, and
the detail view states `CAM - gateway camera hint (LTD)`. Camera metadata
is saved as optional numeric `camera_evidence` in history characterization
records; older history with no field remains compatible. CAM is a heuristic
indication, **not proof** of a camera, surveillance, ONVIF compliance, or AP identity.

A nonresponsive gateway, a camera on another LAN address, HTTPS-only camera,
or a camera that does not supply recognizable HEAD headers remains LTD. More
complete service discovery is deliberately deferred.

## Proposed integrations (not implemented)

1. Introduce a separate opt-in target selection and authorization mechanism
   for any expansion beyond the currently connected DHCP gateway; do not scan
   arbitrary subnet hosts or automatically expand passive observation targets.
2. Add a bounded, cancellable, framework-owned local discovery adapter,
   activated only during the existing station-session check window and stopped
   before a new monitor sweep. Avoid arbitrary subnet-wide scanning.
3. Prefer user-selected endpoints or explicitly advertised service targets.
   Do not assume the DHCP gateway is a camera. Bound host count, protocol
   exchanges, deadlines, response bytes, redirects, memory and concurrency.
   Reject redirects outside the authorized scope.
4. Introduce real DNS-SD/mDNS and ONVIF WS-Discovery parsers with strict
   response validation, independent of this classifier. Do not accept
   unvalidated local-name strings from raw XML.
5. Add a versioned, separate fingerprint-rule database and recorded evidence
   with provenance and confidence semantics. Preserve *unknown* and multiple
   possible categories; test ambiguous and spoofed metadata.
6. Only in a subsequent explicitly authorized flow, negotiate supported
   authenticated camera APIs such as ONVIF snapshot capabilities. No
   default-credential trials, authentication bypass, or automatic third-party
   image collection. Image retention and user-facing preview need their own
   storage/privacy limits and performance validation.
7. Keep results orthogonal to Hotspots' existing connectivity status,
   history epochs, saved credentials, and surveillance-case evidence.

## Validation gates

- Host: ordinary HTTP 401 remains non-camera, generic RTSP is non-camera,
  camera-like banner is only a weak hint, structured device type is distinguishable,
  missing/overlong fields do not crash or retain unbounded content.
- Integration: explicit authorization and manual opt-in, cancelled app exit,
  failed session teardown, SD unavailability, stale/invalid discovery data,
  redirect boundaries, no effect on automatic scan throughput or radio arbitration.
- Hardware: measure PSRAM/internal RAM headroom, task and packet budgets, and
  320x170 display usefulness before adding image decoding.

**Branching:** This PR starts from `main` to avoid coupling to the
unfinished priority-admission PR #137. Reconcile any future integration with
the branch containing the final Hotspots session/admission changes.
