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

## Implementation and acceptance plan

1. Prove Wi-Fi raw frame tap and BLE raw advertising event preservation without breaking existing apps.
2. Implement binary PCAPNG writer with separate Wi-Fi/BLE interfaces, confirmed Wireshark link-type decoding, metadata, and valid GPS association.
3. Build an app descriptor, lifecycle hooks, SD capture UI, channel-hopping policy and loss statistics.
4. Add tests for PCAPNG block length/alignment, multi-interface IDs, packet truncation, GPS option encoding, no-GPS capture, and write failures.
5. Validate on hardware at different traffic levels; confirm files open in Wireshark and GPS location is recoverable with compatible tooling.

**This document is not the implementation.** Resolve BLE packet framing and Kismet GPS interoperability with actual fixture tests before declaring capture compatibility.
