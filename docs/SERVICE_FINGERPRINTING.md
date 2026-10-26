# Hotspots service fingerprinting (experimental design)

This page describes an **unwired prototype and proposed integration**, not an implemented
Hotspots feature. Current connection, UI, settings, journals and automatic checking
remain as documented in [Hotspots](apps/HOTSPOTS.md).

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
  No unauthenticated discovery response should be treated as authorization.

The current recognition literals are intentionally tiny illustrative heuristics,
**not** a complete vendor fingerprint database. Do not translate their presence
into a surveillance alert. Host regression coverage is in
`tests/test_service_fingerprint.py`.

## Proposed integrations (not implemented)

1. Define a separate opt-in, explicitly authorized network/device scope.
   Do not add passive observation results to that scope automatically.
   Preserve existing Hotspots automatic connectivity checks unchanged.
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
