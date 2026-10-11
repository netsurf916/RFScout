# HT1818Z3G5L CASIC receiver discovery and adaptive rate policy

The receiver is identified physically as HT1818Z3G5L (HTZN TTL). It provides NMEA data at 9600 baud; hardware logs confirm bidirectional CASIC binary communication.

On first valid NMEA traffic, RFScout probes the receiver with read-only CASIC MON-VER, MON-HW, and CFG-PRT commands. Matching NAKs immediately advance discovery. On the tested module MON-VER and MON-HW return NAK, whereas CFG-PRT responds successfully. A valid CFG-PRT response enables a read-only CFG-RATE poll. Only a valid, plausible CFG-RATE reply enables adaptive update-rate control.

## Adaptive rate

- Below 3 mph, target 1000 ms (1 Hz), after 20 seconds of stable lower speed.
- At or above 3 mph, target 500 ms (2 Hz), after 5 seconds of stable higher speed.
- Allow at least 30 seconds between attempted changes.
- Require a successful CFG-RATE write ACK, then query CFG-RATE again to verify the receiver's effective interval. A rejection, readback mismatch, or timeout disables further automatic writes until GPS service reactivation.
- Do not persist configuration, switch UART baud rates, reset the receiver, or change NMEA sentence selection.

## Hardware diagnostics

Initial discovery is deliberately verbose because it occurs once per GPS activation. Serial logs include bounded hexadecimal CASIC TX/RX frames, calculated/received checksums, UART TX drain results, NAK command identifiers, CFG-RATE detection, rate-change requests, write ACKs, and rate readback verification.

Hardware confirmed responses: CFG-PRT (06/00) and CFG-RATE (06/04), with initial interval 1000 ms. To test the switch to 2 Hz, sustain speed above 3 mph for at least five seconds and look for a requested interval of 500 ms, ACK, and verified readback of 500 ms. Reduce speed below 3 mph for at least 20 seconds to test return to 1000 ms. The 30-second cooldown may extend either transition.

Generic NMEA position/time reception remains available when CASIC discovery or rate adjustment is unsupported. The UART remains at its detected baud (9600 on the tested module).

One-off discovery and rate-change transitions are also recorded as JSON lines with `type=gps_policy` in the normal SD log (`logs/rfscout-*.log`), regardless of the active app. Events include `transport_detected`, `rate_detected`, `rate_requested`, `rate_write_ack`, `rate_verified`, `rate_verify_mismatch`, `rate_write_rejected`, `rate_timeout`, and discovery/query failures; fields include `requested_ms`, `actual_ms`, and `baud`. Packet hex dumps remain serial-only. SD logging is asynchronous and best-effort; allow time for log flushing before removing power or the SD card.

## References

- CASIC multi-mode GNSS binary protocol specification (CFG-PRT, CFG-RATE, ACK-ACK/ACK-NAK).
- HT1818Z3G5L usage example: https://www.cnblogs.com/excellentHellen/articles/17176131.html
