# GP-20U7 receiver discovery and rate policy

RFScout targets the [GP-20U7 hardware](https://cdn.sparkfun.com/datasheets/GPS/GP-20U7.pdf), which advertises NMEA 0183 and u-blox binary protocol. Its published default is 9600 baud and **1 Hz** navigation output; higher update rates are not guaranteed by that module datasheet.

Once checksum-valid NMEA has established UART ownership, the provider transmits one read-only UBX MON-VER (0A/04) poll. It parses checksum-validated UBX responses separately from NMEA. The software/hardware identifiers are logged. Only firmware identifying as u-blox generation 7 is allowed to progress to UBX CFG-RATE (06/08) query and experimental rate control. Other devices, unsupported versions, invalid messages and timeouts retain generic NMEA behavior.

On positive identification and CFG-RATE response, the first pass logs the current measurement interval and navigation divider. Speed above 3 mph sustained for 5 seconds requests 2 Hz (500 ms); below 3 mph sustained for 20 seconds returns to 1 Hz. At least 30 seconds elapses between successful requests. Rejected or unanswered commands disable further changes during that UART activation. The 2 Hz ceiling deliberately respects the module's 9600-baud default; 5/10 Hz and changes to NMEA sentence structure are deferred until actual GP-20U7 hardware response, UART bandwidth and output are validated.

No PMTK commands, persistent config-save, baud-rate changes, receiver resets or time/position assistance are sent. Neither generic UART arbitration nor receiver-independent location snapshots change.

## Serial log checkpoints

- `GPS NMEA detected at 9600 baud`
- `GPS GP-20U7 probe: UBX MON-VER at 9600 baud`
- `GPS UBX TX class=0A id=04 ...`
- `GPS UBX MON-VER software=... hardware=...`
- `GPS UBX CFG-RATE measurement=... navigation=...`
- `GPS GP-20U7 policy ready: current interval ...`
- `GPS GP-20U7 requested ...`
- `GPS UBX CFG-RATE accepted...`, rejected or timeout

These diagnostics currently go to serial/ESP-IDF logs; this PR does not yet add structured SD logging. Test candidate firmware with serial capture, both at rest and in motion. Before enabling more aggressive rates, verify receiver capability rather than assuming all u-blox implementations are equivalent.
