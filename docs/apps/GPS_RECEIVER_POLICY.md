# HT1818Z3G5L CASIC receiver probe and rate policy

The physical module marking is HT1818Z3G5L (HTZN TTL), rather than the previously assumed GP-20U7 or MTK receiver. A published example using the exact module reports 9600 baud and one NMEA position cycle per second. The CASIC protocol specification documents binary MON-VER (class 0A, ID 04, 64-byte software/hardware response), CFG-RATE (class 06, ID 04, four-byte interval response/set), and ACK/NACK (class 05). **Module-specific binary protocol support and maximum rate remain to be confirmed on hardware.**

After checksum-valid NMEA locks the UART, the provider sends exactly one read-only CASIC MON-VER query. CASIC frames use header BA CE, a little-endian payload length, and a little-endian additive 32-bit checksum over class, ID, length and 32-bit payload words. Responses are parsed independently of NMEA. On valid MON-VER, the provider logs 32-byte firmware/hardware identifiers and queries CFG-RATE. Only successful replies enable adaptive control. An unidentified receiver keeps generic NMEA operation.

To minimize risk while awaiting validation, 1 Hz below 3 mph and at most 2 Hz above 3 mph are attempted after 20-second/5-second stable-speed hysteresis, respectively. At least 30 seconds separates configuration attempts. Rejected or unanswered commands disable further adjustment until UART service reactivation. Changes affect the running session only: no persistent configuration save, UART baud change, sentence-output change, assistance, or reset is sent.

## Hardware test checkpoints

Capture serial ESP-IDF logs from startup:
- `GPS NMEA detected at 9600 baud`
- `GPS HT1818Z3G5L probe: CASIC MON-VER at 9600 baud`
- `GPS CASIC TX class=0A id=04 ...`
- `GPS CASIC MON-VER software=... hardware=...` (if recognized)
- `GPS CASIC CFG-RATE interval=... ms` (if supported)
- `GPS HT1818Z3G5L requested ...` and `GPS CASIC CFG-RATE accepted...` (only after movement and successful probing)
- `GPS CASIC policy timeout...` / checksum warnings / rejection as applicable

This PR does not yet write these protocol diagnostics to the SD log. Firmware and protocol support remain hypotheses until the module responds.

## References
- CASIC multi-mode GNSS protocol specification, sections 2.2, 2.14.1, 2.11.5
- HT1818Z3G5L UART/NMEA usage example: https://www.cnblogs.com/excellentHellen/articles/17176131.html
