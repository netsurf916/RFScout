# nRF24 research and direction

RFScout keeps this application passive. Spectrum energy, candidate framing, and
CRC-validated packets are deliberately separate evidence levels in both the UI
and future capture formats.

## What the radio can tell us

The nRF24L01+ `RPD` bit is a threshold observation, not RSSI. It reports whether
received power exceeded roughly -64 dBm during the receive interval. Repeated
samples can therefore build occupancy and peak-history views, but cannot support
precise signal strength or packet claims.

Enhanced ShockBurst packet evidence is richer: channel, air rate, address width,
packet-control fields, payload length, CRC result, and arrival timing can support
conservative device-behavior groupings. Dynamic payload length is bounded to 32
bytes. A packet must pass framing bounds and CRC before RFScout adds it to the
recent-packet view or classifier.

Primary references:

- Nordic Semiconductor, *nRF24L01+ Product Specification*, sections 6-7:
  <https://docs.nordicsemi.com/bundle/nRF24L01P_PS_v1.0/resource/nRF24L01P_PS_v1.0.pdf>
- Bastille Research's receive-oriented RFStorm firmware and research tooling:
  <https://github.com/BastilleResearch/nrf-research-firmware>
- Great Scott Gadgets' Ubertooth project, a useful comparison for generic 2.4 GHz
  capture and spectrum tooling: <https://github.com/greatscottgadgets/ubertooth>

## Recommended sequence

1. **Keep acquisition independent of rendering.** The radio worker should own
   settle/dwell timing and publish completed snapshots. UI refresh, input, and
   notifications must never wait for a sweep.
2. **Implement receive-only ESB candidate discovery.** Search address widths and
   supported air rates, validate packet-control bounds and CRC, and do not enable
   auto-ack. Feed only validated observations into the existing packet API.
3. **Add a bounded capture ring.** The live UI intentionally shows only a short,
   newest-first, width-truncated hex preview. A later SD capture format should
   retain full payload, address, channel, air rate, CRC status, and monotonic time.
4. **Expose scan modes.** A fast occupancy sweep, focused packet watch, and
   hopping follow mode have different tradeoffs; labeling these modes is clearer
   than making one dwell control imply all three.
5. **Measure useful statistics.** Per-channel hit rate, packets/second, duplicate
   rate, inter-arrival distribution, and channel-transition history are more
   defensible than guessing a product identity from energy alone.
6. **Make classification explainable.** Drill-down should show which evidence
   produced a tentative label and should age stale observations out of the live
   window while preserving explicit session totals.

## Guardrails

- Never transmit acknowledgements, pairing frames, replayed payloads, or probes.
- Treat broad Wi-Fi-like or hopping activity as spectrum heuristics, not decoded
  identities.
- Make truncation visual-only; structured captures must not silently discard the
  bytes hidden by the display width.
- Do not store packet payloads persistently unless capture is explicitly enabled.
