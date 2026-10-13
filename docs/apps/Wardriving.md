# Wardriving packet capture

Wardriving is an implemented, independent passive Wi-Fi/BLE PCAPNG capture application. Its dashboard and capture lifecycle are available on `main`. ESP-IDF CI and the basic capture/start/stop/navigation paths have been exercised; Wireshark packet decoding, sustained capture quality, and removable-card failure recovery still need dedicated validation.

## Output format

Use **PCAPNG** for one mixed-source capture file on the SD card:
- Section Header Block identifies the capture session.
- Separate Interface Description Blocks (IDBs) distinguish Wi-Fi and BLE. Never reinterpret one protocol as another.
- Enhanced Packet Blocks (EPBs) store timestamps, captured/original lengths, raw bytes, and interface IDs.
- For Wi-Fi, record unmodified 802.11 bytes exposed by `wifi_promiscuous_pkt_t.payload`, with a standards-compliant Radiotap header synthesized from available RX metadata (channel/frequency, signal in dBm, and only fields actually known). Do not invent FCS or claim visibility into unsupported packets.
- For BLE, preserve the raw advertisement bytes, advertiser address/type, reported RSSI, event type, and available PHY/channel metadata. Select an existing libpcap Bluetooth LE link type matching the actual representation (for example, Bluetooth LE LL with PHDR only if the required pseudoheader can be correctly filled); otherwise choose a suitable supported representation and document precisely which controller-level bytes are missing. Do not label an advertising report as a complete on-air LL frame.
- When a **valid GPS position fix** is fresh, include a per-packet location annotation. The implementation writes a binary PCAPNG custom option (code 2989, private enterprise number 55922) carrying Kismet-compatible GPS v1 longitude/latitude values encoded in fixed-point units of 1e-7 degrees with a 180-degree offset. It does not write a text packet comment or a GPS fix-age field; interoperability with external tools still requires validation. When GPS is missing or stale, omit packet GPS options (never emit 0,0 as an invented position).
- Capture startup refuses to proceed until RFScout's system clock is valid (GPS, NTP, retained or manually set). This avoids falsely interpreting boot-relative times as Unix epochs. Relative-only captures could be implemented later with explicitly defined metadata.
- Store loss counters, capture totals, supported interface/link type information and channel-hop decisions in an inspectable session manifest or PCAPNG custom metadata; count RX enqueue drops, writer failures, and channel coverage separately.

References:
- https://www.kismetwireless.net/docs/readme/logging/pcap/
- https://www.kismetwireless.net/docs/dev/pcapng_gps/
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/wifi-driver/wifi-modes.html

## RFScout integration

The `radio_monitor` raw-frame subscriber forwards driver-delivered bytes to Wardriving without involving the Wi-Fi/BLE discovery application's derived observations.

The `ble_monitor` raw-advertisement subscriber copies event bytes within their valid lifetime; it must not block the NimBLE host.

Wardriving owns a single capture session through the existing app lifecycle, with a bounded PSRAM-backed buffer and an SD writer task. Neither radio driver callback may perform filesystem I/O, malloc-heavy work, or PCAPNG serialization. Every packet is copied or dropped atomically before the callback returns; on pressure, drop new packets and increment counters rather than blocking radio operations.

Use `media_store` ownership/collection access; card-loss recovery remains a validation area rather than a guaranteed automatic pause/resume. Flush/close on app stop, size-bound segments, and avoid reusing the JSON log writer for raw binary packet data.

## Capture policy and limitations

- Passive reception only. No association, injection, or packet transmission.
- Wi-Fi: scan board-supported 2.4 GHz channels 1-14, with configurable fixed or uniform channel dwell. **No simultaneous capture of all channels**. The existing adaptive AP-focused hopping policy is unsuitable for broad packet sampling; Wardriving must choose an explicit independent hop policy.
- BLE: passive scanning, no duplicate suppression; the controller delivers advertising reports rather than arbitrary Bluetooth traffic. Packet completeness, PHY/channel information, and scan cadence are hardware/host limited and must be disclosed.
- ESP32-S3 cannot receive 5 GHz Wi-Fi. Wi-Fi/BLE coexistence and SD bandwidth impose unavoidable packet loss under load. The integrated UI shows GPS fix, channel, capture state, received packet activity (instantaneous and rolling average), saved/dropped counts, and a rolling 74-second per-channel histogram.
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

Changes to capture source, channel mode, channel set or dwell while recording currently close the active segment, apply the revised radio policy, and start a new capture segment. Explicit policy-change metadata within the file remains a follow-up. Use a separate raw Wi-Fi capture policy rather than altering the existing observation app's scan behavior.

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

If the card is unavailable, below required reserve, read-only, or write/close fails, surface the problem prominently and stop capture without an unbounded RAM backlog. Capture currently requires an explicit restart after media recovery; automatic recovery with fresh-segment resume is still pending. Settings reductions should not indiscriminately delete files while browsing the menu; apply retention pruning on capture start or rotation with the user-visible policy clearly displayed.

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

## Wardriving instrument dashboard

The primary Wardriving overview combines a compact GPS/capture instrument panel on the left with the rolling packet-rate and per-channel activity charts on the right. The global status bar above the app remains platform-owned, showing time, SD state, and other system indicators. The dashboard starts below that reserved region and never draws over it. A fresh GPS coordinate fix illuminates the theme-success `FIX` indicator; without a fix the indicator switches to theme-warning `NO FIX` and coordinates are hidden. Recording, storage errors, dropped frames, graph peaks, and current channel use the existing semantic theme colors. Thin theme-secondary dividers separate instruments. Center opens the existing capture settings; there is no separate channel activity page.

Packet-rate activity is a scrolling 74-sample window with one-second buckets. When UI polling is delayed, callback observations without timestamps are spread across the elapsed interval, so missed ticks do not create a false single-second spike; those rates are estimates rather than exact packet arrival times. The graph header displays the latest completed second's packet count and the arithmetic mean of completed samples within the window (using fewer than 74 seconds during initial warm-up). The upper graph stacks Wi-Fi (`info`) and BLE (`accent`) observation counts when both sources are enabled; with just one source enabled it reverts to the single-series peak-highlight color scheme. Both layers share one combined vertical scale, and the Wi-Fi channel histogram remains independent. The channel bars are 74-second rolling totals for channels 1–14, automatically scaled to their chart region. The bars represent driver-delivered frames, not airtime occupancy or traffic on channels while the radio was tuned elsewhere. Short control frames contribute. The activity callback only counts driver-delivered frames; AP/station identity estimation was removed because no dashboard consumer uses it. Comparisons between channels depend on comparable dwell times. Neither visualization changes raw PCAPNG capture.

## Runtime behavior and application lifecycle

The app is a passive capture utility, distinct from the Wi-Fi/BLE surveillance classifier. Its primary screen shows recording state (Stopped, Recording, Paused for media, or Error), current source selection, Wi-Fi channel and hop progress, fresh GPS fix status, packet and byte counters, drops, active PCAPNG filename, SD free space, and estimated retention ceiling. Pressing Start opens a new capture session; Stop drains the bounded writer queue, finalizes the current PCAPNG file, releases the radio service requests and returns to Stopped. Neither switching away from this app nor entering Settings should leave unmanaged radio or media resources behind.

Capture may start without GPS; the app records frames without location options until a fresh valid fix is available. Timestamp and GPS validity are separate: a GPS receiver may be present without a fix, and an NTP-synchronized clock does not imply location. System-time changes during capture must preserve a coherent timestamp convention and be recorded as session metadata. When the SD card disappears, the writer stops accepting packets, drops or drains pending frames according to a bounded policy, and reports media unavailable. Insertion may start a new segment after the storage service recovers; never append blindly to a damaged or partially written segment.

Radio settings are applied only to the Wardriving capture session, not to the existing RFScout Wi-Fi/BLE observation app. On start, enable only selected sources; on stop, quiesce callbacks and wait for in-flight deliveries before reclaiming queues. When capture settings change, update receiver policies atomically at a safe boundary and annotate the change. The UI can remain responsive while capture is active; UI drawing, menu navigation, and SD reads must not block radio callbacks.

## Capture pipeline and ownership

1. **Wi-Fi receive callback:** Copy the complete driver-exposed 802.11 payload and actual RX control metadata into a preallocated bounded queue. Retain frame type, channel/frequency where available, RSSI, reported length, and monotonic arrival timestamp. Do not perform filesystem I/O, PCAPNG encoding, or expensive parsing in this callback.
2. **BLE discovery callback:** Copy the advertising report data before the NimBLE event lifetime ends, including advertiser address/type, advertising-event type and RSSI. Record only controller-exposed metadata; do not synthesize absent PHY/channel data. Disable host duplicate filtering, though controller scheduling and reports remain hardware-dependent.
3. **Time/location snapshot:** Associate a contemporaneous system timestamp and GPS fix with each accepted queue entry; only include location when fix-valid and younger than a documented freshness threshold. Record which coordinate source and accuracy fields are truly available.
4. **Writer task:** Drain packets in bounded batches, serialize PCAPNG blocks with correct 32-bit alignment and interface IDs, and write through RFScout's media-store arbitration. On allocation or queue pressure, drop the new packet, increment counters, and keep receiver callbacks nonblocking.
5. **Rotation and finalization:** Flush blocks, close the file, and open a fully self-describing new section/interface set before the next packet. Enforce per-file and file-count policies while preserving the global SD reserve. Only delete segments owned by this app.
6. **Cleanup:** On stop, card failure, or app transition, prevent new queue entries, quiesce both radio callbacks, synchronize/close any active segment, free app-scoped PSRAM buffers, and report final loss/storage statistics. App lifecycle must not rely on cleanup by another application.

The capture metadata must distinguish **packets observed by a driver**, **packets accepted into the queue**, **packets committed to disk**, and **packets dropped**. Counts are estimates of delivered packets, never claims about all over-the-air traffic. Record failures separately for Wi-Fi queue overflow, BLE queue overflow, record-too-large, SD writer failures, and storage interruption.

## PCAPNG interoperability and file integrity

Use one Section Header Block plus distinct Interface Description Blocks for Wi-Fi and BLE in each segment. The Wi-Fi interface uses an 802.11 link type plus a valid Radiotap prefix; the BLE interface uses a verified libpcap link type consistent with the NimBLE report representation. Each Enhanced Packet Block references the corresponding interface ID and includes captured versus original lengths, timestamp, and applicable radio metadata. A segment is independently readable when copied off the SD card.

Per-packet GPS is optional and represented using an existing compatible PCAPNG option only after byte-for-byte interoperability has been tested. Do not use another project's enterprise number for a different binary layout. GPS position, speed, fix-validity, and precision/accuracy are separate from clock-sync validity. GPS-disabled or GPS-unavailable packets remain valid PCAPNG packets with no fabricated coordinates.

The writer never splits an individual PCAPNG block across segments. A segment cap accounts for headers and packet-block overhead, and oversized records are dropped with a counter. Write failures mark the segment suspect and notify the UI; the app must not silently report dropped data as captured. Files stay in a Wardriving-owned media collection and follow stable, monotonically incrementing segment naming, independent of the JSON system log sequence. Settings and retention operations must avoid traversing or modifying other apps' directories.

## Menu semantics and storage estimates

Use RFScout's ordinary trackball conventions: turn to select, press center to edit/confirm, and Back to return. Nonapplicable controls are dimmed rather than allowing changes with no effect. Source selection defaults to Both; Wi-Fi defaults to round-robin scan across the supported 2.4 GHz channels with 200 ms dwell. Fixed-channel mode stays on the selected channel. Wi-Fi packet filtering defaults to all available classes. BLE remains passive, no host-level duplicate suppression. GPS metadata defaults to attaching only when position is valid and fresh.

Maximum file size and maximum retained file count define a retention ceiling using checked integer arithmetic: `ceiling_bytes = per_file_cap_bytes * max_files`. Display the value as readable MiB/GiB next to **actual usage** and **currently available SD space**. The ceiling is not a forecast of data volume; real consumption depends on traffic and runtime. A configuration that cannot fit given current space and the reserved-free requirement should be flagged clearly, but should not cause other files to be deleted. Smaller retention settings take effect at the next documented pruning point, without surprise deletions just from browsing the menu.

## Design boundaries and test matrix

This app does not associate with access points, transmit discovery traffic, decrypt protected Wi-Fi traffic, reconstruct unavailable BLE link-layer packets, or claim lossless reception across multiple channels. ESP32-S3 is 2.4 GHz-only. Concurrent Wi-Fi, BLE and SD operations contend for resources and some missed traffic is unavoidable.

Acceptance requires host-side PCAPNG structure tests, Wireshark decode of both interface types, location-option fixture verification, boot-without-time handling, GPS fix acquisition/loss, fixed-channel and round-robin hopping, mixed and single-source capture, saturated queue/drop accounting, rotation exactly at size/count boundaries, card removal/reinsertion, SD-full handling, and application-switch shutdown without leaked handles. Record measured capture loss under high traffic and document any driver restrictions uncovered by hardware testing.

## Remaining validation

The raw capture path, app-scoped settings, rolling activity dashboard, and lifecycle integration are implemented on `main`. Successful firmware CI and basic hardware capture/navigation tests do not by themselves establish lossless capture or full file-format interoperability.

Before claiming production-quality capture, validate both PCAPNG interface types in Wireshark, GPS metadata interoperability, source/time behavior, saturated queue/drop accounting, retention boundaries, card removal/reinsertion, and SD-full handling. Record measured losses and any driver limitations rather than treating the old design checklist as implementation status.
