# nRF24

nRF24 is RFScout's receive-only 2.4 GHz activity application. Shared SPI/service/lifecycle rules are defined in [Platform architecture](../ARCHITECTURE.md); planned passive packet-discovery/history work belongs in [ROADMAP.md](../ROADMAP.md).

## Availability and lifecycle

The application performs a startup hardware probe and is available only when the selected runtime profile legally provides a working nRF24 service. The current T-Embed profile advertises that provider.

Activation starts the session-managed scanner worker. The worker must quiesce before app memory can be reclaimed.

## RPD scanning

The production scanner sweeps configurable channel bounds with configurable dwell time and uses the nRF24 **Received Power Detector (RPD)** result as channel activity evidence.

RPD is threshold energy evidence. It is **not RSSI** and it is **not proof of a decoded packet**.

The scanner maintains a fast activity histogram across the 126 nRF24 channel positions and applies conservative activity classification.

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

## Passive-only boundary

RFScout does not enable auto-ACK, inject, replay, pair, probe, or actively discover nRF24 devices. That passive boundary is intentional.

## Current limitations

The current scanner does not yet provide the stronger passive ESB candidate discovery/validation or growable PSRAM-backed validated packet history planned for v0.4.0. Packet/history surfaces must not be populated from unvalidated RPD activity alone.
