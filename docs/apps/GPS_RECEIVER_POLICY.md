# HT1818Z3G5L CASIC receiver discovery and adaptive rate policy

The receiver is identified physically as HT1818Z3G5L (HTZN TTL). It provides NMEA data at 9600 baud; hardware logs confirm bidirectional CASIC binary communication.

On first valid NMEA traffic, RFScout probes the receiver with read-only CASIC MON-VER, MON-HW, and CFG-PRT commands. Matching NAKs immediately advance discovery. On the tested module MON-VER and MON-HW return NAK, whereas CFG-PRT responds successfully. A valid CFG-PRT response enables a read-only CFG-RATE poll. Only a valid, plausible CFG-RATE reply enables adaptive update-rate control.

## Adaptive rate

- Below 3 mph: 1000 ms (1 Hz).
- 3 to below 7 mph: 500 ms (2 Hz).
- 7 to below 12 mph: 333 ms (~3 Hz).
- At or above 12 mph: 250 ms (4 Hz). This is the temporary upper bound at 9600-baud UART.
- Increasing the rate requires 5 seconds at the new target speed tier; decreasing it requires 20 seconds. Intermediate tiers can be skipped when speed rises quickly.
- Allow at least 30 seconds between attempted changes.
- Require a successful CFG-RATE write ACK, then query CFG-RATE again to verify the receiver's effective interval. A rejection, readback mismatch, or timeout disables further automatic writes until GPS service reactivation.
- Do not persist configuration, switch UART baud rates, reset the receiver, or change NMEA sentence selection.

## Hardware diagnostics

Initial discovery is deliberately verbose because it occurs once per GPS activation. Serial logs include bounded hexadecimal CASIC TX/RX frames, calculated/received checksums, UART TX drain results, NAK command identifiers, CFG-RATE detection, rate-change requests, write ACKs, and rate readback verification.

Hardware confirmed responses: CFG-PRT (06/00) and CFG-RATE (06/04), with initial interval 1000 ms. For a scooter test, hold above 3, 7, and 12 mph long enough to trigger 500, 333, and 250 ms requests, respectively. Each requires a write ACK followed by matching CFG-RATE readback. Lower speed for 20 seconds to test downward changes; the 30-second cooldown may delay or skip intermediate steps. Review `gps_policy` SD records rather than depending on a live serial connection.

Generic NMEA position/time reception remains available when CASIC discovery or rate adjustment is unsupported. The UART remains at its detected baud (9600 on the tested module).

One-off discovery and rate-change transitions are also recorded as JSON lines with `type=gps_policy` in the normal SD log (`logs/rfscout-*.log`), regardless of the active app. Events include `transport_detected`, `rate_detected`, `rate_requested`, `rate_write_ack`, `rate_verified`, `rate_verify_mismatch`, `rate_write_rejected`, `rate_timeout`, and discovery/query failures; fields include `requested_ms`, `actual_ms`, and `baud`. Packet hex dumps remain serial-only. SD logging is asynchronous and best-effort; allow time for log flushing before removing power or the SD card.

## References

- CASIC multi-mode GNSS binary protocol specification (CFG-PRT, CFG-RATE, ACK-ACK/ACK-NAK).
- HT1818Z3G5L usage example: https://www.cnblogs.com/excellentHellen/articles/17176131.html
