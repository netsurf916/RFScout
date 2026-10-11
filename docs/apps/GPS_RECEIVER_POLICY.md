# Receiver identification and automatic policy

The shared GPS provider retains generic NMEA operation until checksum-valid RMC/GGA traffic locks a baud rate and its shared UART pins. It then sends a single read-only PMTK605 firmware query. Nonresponsive/unknown receivers continue generic NMEA without further configuration.

For a receiver replying with PMTK705, it queries PMTK414 (NMEA sentence periods) and PMTK400 (fix interval), logging PMTK514 and PMTK500 responses. It then requests an MT3337-style 19-field PMTK314 sentence profile: RMC and GGA each fix, GSA each fix, GSV every fifth fix, redundant sentence types disabled. Only an accepted PMTK001,314,3 permits adaptive PMTK220 updates.

Nominal adaptive position intervals use speed from fresh RMC:
- Under 3 mph: 1000 ms (1 Hz).
- 3 to 10 mph: 500 ms (2 Hz).
- 10 to 35 mph: 200 ms (5 Hz) if baud >=19200; otherwise 500 ms.
- 35+ mph: 100 ms (10 Hz) if baud >=38400; otherwise capped at the baud-limited rate.

Upshift after 5 seconds of stable reported speed and downshift after 20 seconds, with 30 seconds between rate changes. A rejected or missing rate acknowledgment disables subsequent adjustments for that UART activation. No baud-rate changes, receiver resets, time assistance, position assistance, or user-exposed controls are sent. The exact maximum usable rate is receiver-firmware dependent and requires hardware verification.

### Log validation
Filter logs for `gps:` / `GPS policy`:
- `GPS NMEA detected at ... baud`
- `GPS policy probing receiver ...` / `GPS policy TX PMTK605`
- `GPS receiver PMTK firmware: ...`
- `GPS existing NMEA configuration: ...`
- `GPS existing fix interval: ...`
- `GPS PMTK acknowledgment command=...`
- `GPS policy requested ... ms fix interval ...`
- `GPS policy update interval result=...`
- Warnings on timeouts and unsupported/rejected requests

Device-dependent hardware validation: inspect PMTK version/product to confirm the intended module; verify baud and output sentence compatibility, actual fix cadence at different speeds, and unchanged GPS/nRF24 arbitration. The identification is protocol-based; a PMTK705 alone is not independent proof of a particular chip model.