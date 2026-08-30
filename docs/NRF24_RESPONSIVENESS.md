# nRF24 responsiveness and packet browser

This document describes the responsiveness and decoded-packet presentation work implemented by the nRF24 optimization branch.

## Responsiveness problem

The nRF24 scanner already runs in its own FreeRTOS task, so the observed Center-button latency was not caused by the entire RF sweep executing directly inside the application manager. Two other effects were more important:

- every completed sweep could trigger a full nRF24 spectrum redraw even when the display could not usefully present updates that quickly;
- the ST7789 display flush is synchronous, so the application loop cannot consume input while a full framebuffer transfer is in progress. The LCD and nRF24 also share SPI2 on the current hardware.

The encoder button was also sampled and debounced only from `platform_poll()`. A sufficiently short press/release that occurred while the application loop was delayed could therefore be missed even though the physical button was functioning correctly.

## Implemented responsiveness changes

RF acquisition and human-facing presentation now have deliberately different cadences:

- RF sweeps continue in the nRF24 worker at the configured dwell/channel cadence.
- Every completed sweep still contributes to the adaptive activity histogram and spectrum classification.
- Completed display snapshots are coalesced, and the application consumes at most one nRF24 presentation frame per 125 ms.
- The scan worker runs cooperatively at `tskIDLE_PRIORITY + 1` instead of priority 4 so it does not unnecessarily dominate application work.
- Entering nRF24 settings or packet subviews suppresses background spectrum rendering immediately.
- Encoder rotation remains interrupt-driven, and the Center button now also records press/release edges in an ISR. `platform_poll()` converts those captured durations into semantic input after the physical gesture completes. A complete click therefore survives a temporary synchronous LCD/main-loop stall.
- Center-button holds from 0.8 to under 1.5 seconds remain `LONG_SELECT`, holds from 1.5 to under 2 seconds produce the generic `BACK` input event, and the existing two-second hold toggles the input lock. This gives one-button targets an explicit Back gesture without taking long-select away from applications such as Recorder.

The presentation throttle does not reduce the number of RF sweeps used by the activity model; it only reduces redundant LCD work.

## Decoded packet presentation

Decoded packets are treated as a first-class nRF24 data product rather than extra rows squeezed underneath the spectrum graph.

The nRF24 settings menu now contains a **Packets** subview. It provides:

- a full-screen newest-first packet list;
- up to 16 bounded recent packet records;
- per-row channel, data rate, address, and payload length;
- a detail screen with channel, data rate, address, PID, ACK/NO_ACK state, CRC state, timestamp, and the complete payload;
- generic Back navigation from detail to list, then from list to nRF24 settings.

The packet list keeps the exact bounded snapshot that was rendered. Opening a detail reads the selected record from that displayed snapshot rather than taking a second live snapshot, so a packet arriving between highlighting a row and pressing Center cannot silently change the identity of the packet shown.

The recent-packet model preserves all currently decoded metadata: channel, bitrate, address, PID, NO_ACK, CRC state, payload length, full payload, and timestamp. Rendering reads bounded copies only; it does not perform RF decoding or classification.

Only CRC-validated observations may enter the recent-packet ring. The current production scanner is still an RPD energy scanner and does not yet feed decoded ESB observations into `nrf24_activity_observe_packet()`. Until a receive-only decoder producer is implemented, the packet browser explicitly displays that it is waiting for decoder input instead of presenting spectrum heuristics as decoded messages.

## Long-term boundary

Receive/decode work should remain separate from presentation. Future ESB candidate discovery, CRC validation, hopping follow, or protocol-specific interpretation should publish validated `nrf24_packet_observation_t` records into the existing activity/capture API. The packet browser should not need to know how those records were acquired.
