# nRF24

nRF24 is RFScout's receive-only 2.4 GHz activity application. Shared SPI/service/lifecycle rules are defined in [Platform architecture](../ARCHITECTURE.md); planned passive packet-discovery/history work belongs in [ROADMAP.md](../ROADMAP.md).

## Availability and lifecycle

The application uses the platform's **fixed-hardware** probe policy. It is probed during initial discovery; if that probe misses, availability is retried when navigation checks the app rather than remaining permanently unavailable for the boot. The nRF24 driver keeps failed initialization retryable and only caches initialized state after a successful identity probe. The current T-Embed profile advertises the provider.

Activation starts the session-managed scanner worker. The worker must quiesce before app memory can be reclaimed.

## RPD scanning

The production scanner sweeps configurable channel bounds with configurable dwell time and uses the nRF24 **Received Power Detector (RPD)** result as channel activity evidence.

RPD is threshold energy evidence. It is **not RSSI** and it is **not proof of a decoded packet**.

The scanner maintains a fast activity histogram across the 126 nRF24 channel positions and applies conservative activity classification.

### Spectrum-shape policy

The fast histogram is intentionally coarse. A channel is considered busy when its accumulated activity reaches **72**. The current spectrum classifier calls activity **Wi-Fi-like** when it contains a contiguous run of at least **12** busy channels. Otherwise, it calls activity **BLE-like** when it contains at least **7** separate busy-channel islands while fewer than **28** channels are busy overall. Anything else remains unclassified by the broad spectrum-shape heuristic.

These results describe the shape of energy occupancy seen by the nRF24 RPD scanner. They do not assert that an IEEE 802.11 or BLE packet was decoded. Broad spectrum-shape candidates are debounced across consecutive sweeps before being latched for presentation.

## Current classification

Current category labels include:

- Enhanced ShockBurst (ESB);
- input-device-like;
- sensor-like;
- Wi-Fi-like;
- BLE-like; and
- Logitech-like.

These are conservative observations, not guaranteed product identity.

## Validated Enhanced ShockBurst observations

The application also has a separate ingestion/presentation path for validated packet observations. A packet must be supplied with valid CRC before it is admitted to the recent-packet ring/classifier.

Retained metadata can include channel, air rate, address, payload length/content, PID, NO_ACK state, CRC state, and timestamp. The current recent-packet ring is bounded to 16 entries and the packet browser holds a stable displayed snapshot while a row is selected.

The production RPD scanner does **not** currently decode ESB packets itself. Validated observations must come from a separately validated receive-only ingestion path.

### Validated-packet classification policy

A validated packet observation is admitted only when CRC is valid, the ESB address length is 3–5 bytes, and payload length is at most 32 bytes. New packet identities begin as **ESB device** with heuristic confidence 55. A known Logitech pairing address is labeled **Logitech?** with confidence 98. Other identities need at least six validated packets before timing/payload heuristics refine the label:

- average inter-packet interval at or below **25 ms** with maximum payload length at or below **20 bytes** becomes **Input?**; confidence is 70 and rises to 82 after at least 20 packets;
- average interval at or above **100 ms** with payload-size span no greater than one byte becomes **Sensor?**; confidence is 66 and rises to 78 after at least 12 packets; and
- otherwise the observation remains **ESB device**, with confidence rising to 60 or 68 as additional validated packets accumulate.

These confidence values are deterministic heuristic strengths, not measured probabilities of product identity. Packet-derived classifications and RPD spectrum-shape classifications are separate evidence paths and should not be interpreted as interchangeable.

### SD telemetry example

The app emits one-second operational summaries while active when SD logging is available. A captured hardware record:

```json
{"type":"telemetry","time":"2026-09-23T22:38:44Z","app":"NRF24","low_channel":0,"high_channel":125,"dwell_us":250,"likely":6,"detected":true}
```

`likely` is the number of fixed detection-category rows copied by the current telemetry path. It is currently **6** because nRF24 exposes six category rows; it is not a count of active detections, validated ESB devices, or identified products.

## Passive-only boundary

RFScout does not enable auto-ACK, inject, replay, pair, probe, or actively discover nRF24 devices. That passive boundary is intentional.

## Current limitations

The current scanner does not yet provide the stronger passive ESB candidate discovery/validation or growable PSRAM-backed validated packet history planned for v0.4.0. Packet/history surfaces must not be populated from unvalidated RPD activity alone.
