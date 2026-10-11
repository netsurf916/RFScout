# Wardriving packet capture (design)

Status: design proposal; implementation pending. This app is separate from Wi-Fi/BLE observation and classification.

## Output format

Use **PCAPNG** for one mixed-source capture file on the SD card:
- Section Header Block identifies the capture session.
- Separate Interface Description Blocks (IDBs) distinguish Wi-Fi and BLE. Never reinterpret one protocol as another.
- Enhanced Packet Blocks (EPBs) store timestamps, captured/original lengths, raw bytes, and interface IDs.
- For Wi-Fi, record unmodified 802.11 bytes exposed by `wifi_promiscuous_pkt_t.payload`, with a standards-compliant Radiotap header synthesized from available RX metadata (channel/frequency, signal in dBm, and only fields actually known). Do not invent FCS or claim visibility into unsupported packets.
- For BLE, preserve the raw advertisement bytes, advertiser address/type, reported RSSI, event type, and available PHY/channel metadata. Select an existing libpcap Bluetooth LE link type matching the actual representation (for example, Bluetooth LE LL with PHDR only if the required pseudoheader can be correctly filled); otherwise choose a suitable supported representation and document precisely which controller-level bytes are missing. Do not label an advertising report as a complete on-air LL frame.
- When a **valid GPS position fix** is fresh, encode per-packet location as a compatible PCAPNG GPS custom option; refer to Kismet's documented approach, but use Kismet's enterprise number only if implementing its format exactly. When GPS is missing or stale, omit packet GPS options (never emit 0,0 as an invented position).
- Clock-invalid captures must use monotonic timestamps with an explicitly documented capture-local convention, or defer creation until wall time is valid. Do not silently present boot-relative time as Unix epoch.
- Store loss counters, capture totals, supported interface/link type information and channel-hop decisions in an inspectable session manifest or PCAPNG custom metadata; count RX enqueue drops, writer failures, and channel coverage separately.

References:
- https://www.kismetwireless.net/docs/readme/logging/pcap/
- https://www.kismetwireless.net/docs/dev/pcapng_gps/
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/wifi-driver/wifi-modes.html

## RFScout integration

The existing `radio_monitor` decodes Wi-Fi frames and delivers *derived MAC observations*; it currently does not expose raw frame bytes to applications. Add a low-overhead raw-frame subscriber path at the platform/service boundary, keeping Wi-Fi/BLE discovery applications unchanged.

The existing `ble_monitor` likewise parses NimBLE discovery events into `rf_device_t` and drops raw advertisement bytes. Add a raw-advertisement subscriber path using a **copy** of the event data while valid, without blocking the NimBLE host.

The Wardriving app should explicitly own a single capture session via app lifecycle start/stop, with a bounded PSRAM-backed buffer and a dedicated SD writer task. Neither radio driver callback may perform filesystem I/O, malloc-heavy work, or PCAPNG serialization. Every packet is copied or dropped atomically before the callback returns; on pressure, drop new packets and increment counters rather than blocking radio operations.

Use existing `media_store` ownership/collection access and card insertion/removal handling; a card loss ends or pauses capture cleanly and starts a new PCAPNG segment when media returns. Flush/close on app stop, size-bound segments, and avoid reusing the JSON log writer for raw binary packet data.

## Capture policy and limitations

- Passive reception only. No association, injection, or packet transmission.
- Wi-Fi: scan board-supported 2.4 GHz channels 1-14, with configurable fixed or uniform channel dwell. **No simultaneous capture of all channels**. The existing adaptive AP-focused hopping policy is unsuitable for broad packet sampling; Wardriving must choose an explicit independent hop policy.
- BLE: passive scanning, no duplicate suppression; the controller delivers advertising reports rather than arbitrary Bluetooth traffic. Packet completeness, PHY/channel information, and scan cadence are hardware/host limited and must be disclosed.
- ESP32-S3 cannot receive 5 GHz Wi-Fi. Wi-Fi/BLE coexistence and SD bandwidth impose unavoidable packet loss under load. The UI should display elapsed time, channel, GPS fix, written bytes, observed packets, dropped packets, and SD status.
- Do not decode or filter application payloads in the hot path; preserve captured bytes verbatim insofar as delivered by the hardware driver. Captures may contain sensitive traffic: document retention and handling.

## App menu and configurable policies

The app provides an on-device Settings menu using RFScout's standard trackball navigation and edit semantics. The capture overview remains accessible while recording and shows active protocol(s), Wi-Fi channel/hop mode, GPS status, queued/written bytes, packet counts, drops, and remaining SD space. Settings are persisted as app-scoped preferences (no global radio-policy changes).

### Radio

| Control | Values / semantics |
| --- | --- |
| Capture sources | **Both** (default), **Wi-Fi only**, **BLE only**. Stop inactive receiver and do not allocate its queue bandwidth. |
| Wi-Fi mode | **Scan all** (default), **Fixed channel**. Applicable only if Wi-Fi is enabled. |
| Scan channels | Enabled-channel set from the board-supported 2.4 GHz regulatory range; default all supported channels. Never assume 5 GHz is available on ESP32-S3. |
| Channel dwell | Adjustable dwell interval, initially 200 ms per channel; fixed-channel mode ignores dwell. Uniform round-robin hopping rather than existing AP-weighted adaptive hopping. |
| Fixed channel | Select one supported Wi-Fi channel; capture continuously there until changed or stopped. |
| BLE scan | Passive advertising with duplicate filtering disabled; coexistence scheduling remains controller-driven. BLE frequency/channel is not user-fixable through the current NimBLE scanning API. |

Changes to capture source, channel mode, channel set or dwell while recording should apply at a safe boundary and be recorded in capture/session metadata, including hop transitions. Use a separate raw Wi-Fi capture policy rather than altering the existing observation app's scan behavior.

### Capture content

| Control | Values / semantics |
| --- | --- |
| Packet types (Wi-Fi) | **All driver-delivered types** (default); optional management/data/control selection to reduce file size. These are raw 802.11 frames, not parsed device summaries. |
| BLE advertising | **All delivered reports** (default); optional disable BLE through Capture sources. Preserve payload bytes and available metadata. |
| GPS metadata | **Attach when valid** (default), **Off**. Never use stale/no-fix positions. Do not disable GPS provider globally. |
| Snap length | **Full exposed packet** (default), or a clearly labeled capture-length limit. Record original versus captured length in PCAPNG. |
| Metadata | Channel/frequency, RSSI, source/interface, time validity, capture/drop counters; include when available and distinguish unknown from zero. |

In particular, packet-type filters should be opt-in and **must not silently drop duplicate packets**. The app is intended to capture as much as hardware and throughput allow.

### SD capture and rotation

| Control | Values / semantics |
| --- | --- |
| Maximum file size | Selectable cap per PCAPNG segment; suggested default **16 MiB**. |
| Maximum files | Selectable count of retained capture segments; suggested default **16**. |
| Maximum total usage | **Computed hard ceiling**: maximum file size times maximum files, displayed immediately as settings change. For the suggested defaults this is **256 MiB**. |
| Free-space reserve | Maintain the platform's SD free-space reserve; never exhaust the card to fulfill a capture quota. |
| Storage status | Show configured ceiling, actual capture usage, free SD capacity, and estimated additional headroom before starting. |

These are **two primary rotation knobs**: per-file cap and retained-file count; the displayed total size is derived, not a third independently competing retention limit. The estimate is a **maximum allocation policy**, not predicted bytes recorded. Explain that real usage depends on radio traffic, capture duration, and losses; header/option overhead is included in the segment size limit.

Rotation is by completed segment order: finalize and close the current PCAPNG file before opening the next, then delete the oldest **Wardriving-owned** segment if retaining the new segment would exceed the configured count. Never rotate or delete RFScout JSON logs, other applications' files, or unrelated SD contents. A new segment always begins with its own Section Header and interface descriptions; do not split a PCAPNG block across segments. If one packet cannot fit inside an otherwise empty segment, report a record-too-large error and drop it rather than bypassing the cap.

If the card is unavailable, below required reserve, read-only, or write/close fails, surface the problem prominently and stop or pause capture without an unbounded RAM backlog. Resume in a fresh valid segment on media recovery. Settings reductions should not indiscriminately delete files while browsing the menu; apply retention pruning on capture start or rotation with the user-visible policy clearly displayed.

### Menu presentation

```text
Wardriving
  Start / Stop capture
  Sources           Both
  Wi-Fi mode        Scan all
  Channels          1-14
  Dwell             200 ms
  Wi-Fi packets     All
  GPS metadata      On when fixed
  Max file          16 MiB
  Max files         16
  Max SD usage      256 MiB  [read-only]
  Current usage     ... MiB  [read-only]
  SD free           ... MiB  [read-only]
  Back
```

Disable or dim Wi-Fi controls when Wi-Fi is off, and show the fixed-channel picker only in Fixed channel mode. Capacity and free-space values refresh when the menu opens or settings change, without rescanning the card on every UI frame. The on-device UI should state whether recording is currently active and whether policy changes take immediate effect.

## Implementation and acceptance plan

1. Prove Wi-Fi raw frame tap and BLE raw advertising event preservation without breaking existing apps.
2. Implement binary PCAPNG writer with separate Wi-Fi/BLE interfaces, confirmed Wireshark link-type decoding, metadata, and valid GPS association.
3. Build an app descriptor, lifecycle hooks, capture UI, radio/content menus, persisted app settings, channel policy, and loss statistics.
4. Add tests for PCAPNG block length/alignment, multi-interface IDs, packet truncation, GPS option encoding, no-GPS capture, rotation count/size limits, source selection, channel policy, space reserve, and write failures.
5. Validate on hardware at different traffic levels; confirm files open in Wireshark and GPS location is recoverable with compatible tooling.

**This document is not the implementation.** Resolve BLE packet framing and Kismet GPS interoperability with actual fixture tests before declaring capture compatibility.
