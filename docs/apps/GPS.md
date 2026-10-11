# GPS diagnostics

The GPS application is a read-only presentation of receiver telemetry from the shared
GPS location service. Its only state-changing action resets the service's trip odometer.
It neither opens a UART nor acquires the shared GPS/nRF24 pins and does not write
position history to SD. Its descriptor has no exclusive service dependencies.

## Current display

- Receiver status distinguishes an inactive UART from a receiver without valid NMEA and a receiver with/without a fresh position fix.
- A fresh fix displays latitude, longitude and fix age; altitude (meters) is shown only with recent GGA data, and speed (km/h) only with recent RMC data. Missing or stale source fields display `--` rather than a fabricated zero or an old value.
- The existing GGA satellite field is the number of satellites **used in the fix**, not all satellites in view. Its value also expires independently when GGA data stops arriving.
- When the fix becomes stale, the app hides position and motion values rather than displaying them as live.
- The GPS service maintains a resettable trip odometer from plausible, moving RMC fixes while the receiver is active, even when the GPS application is not open.
- The trip counter ignores stationary jitter, implausible position jumps, and gaps in fix reception. It cannot reconstruct distance traveled without valid GPS fixes.
- Select opens a trip-reset confirmation. Rotate to YES and select to reset; long-press cancels. A reset is written immediately to internal flash.
- The distance is checkpointed to internal flash once at least 250 m of new movement **and** five minutes since the previous checkpoint are reached, including while stationary or when nRF24 has temporarily suspended the GPS UART. A sudden power loss can discard the most recent uncheckpointed distance; SD media is never used.
- The display refreshes twice per second. Normal global navigation applies outside the reset confirmation.
- GPS app periodic SD telemetry includes receiver/fix state, trip distance, and fresh speed/altitude/satellites-used values when available. The normal passive-app audio feedback policy applies.

## Receiver and radio interaction

GPS detection and valid GPS position are distinct. A receiver may be present while
searching for a fix. The platform GPS UART uses pins shared with nRF24; this application
does not override the platform's established arbitration rules. With the GPS provider
inactive, the UI reports that state instead of attempting to take over the pins.

## Receiver rate management

The shared GPS service detects NMEA at the receiver's existing UART baud (9600 on the tested HT1818Z3G5L). It probes CASIC read-only commands; this receiver NAKs MON-VER/MON-HW but answers CFG-PRT and CFG-RATE. CASIC commands use a four-second response deadline. Timed-out discovery probes and the initial read-only CFG-RATE query are retried twice before giving up, because the receiver may already be producing higher-rate NMEA traffic on a 9600-baud link. Retries are recorded in the main SD log. A valid CFG-RATE interval enables adaptive updates; otherwise generic NMEA operation continues.

| Speed | Requested update interval |
| --- | --- |
| Under 3 mph | 1000 ms (1 Hz) |
| 3–7 mph | 500 ms (2 Hz) |
| 7–12 mph | 333 ms (~3 Hz) |
| 12+ mph | 250 ms (4 Hz maximum) |

Rate increases require 5 seconds in a higher tier, decreases require 20 seconds, and at least 30 seconds separates changes. The rate policy uses a separate exponentially smoothed GPS speed estimate (90% previous/10% new at 1 Hz, adjusted for each sample interval), without changing displayed raw speed or trip calculations. A CFG-RATE write must be acknowledged and verified by readback; rejection, mismatch, or timeout stops automatic adjustments for that GPS session. After confirming CASIC support and the initial rate, firmware attempts a session-only transition to 115200 baud when a matching CFG-PRT UART profile is available. It preserves the profile's protocol/mode fields, keeps ownership of the shared UART pins while waiting for fresh checksum-valid NMEA at the new baud, and resumes baud discovery only if the dedicated eight-second verification deadline expires. No configuration-save command, sentence-output change, or persistent receiver configuration is sent.

Serial logs summarize CASIC discovery, initial configuration, rate changes, and errors without routine raw packet dumps. The normal SD logs record `gps_policy` JSON events for receiver detection, protocol confirmation, initial rate, rate requests, acknowledgments, verified changes, and failures. Records include baud, requested/actual interval, and filtered speed when available, independent of the active app. The optional SD breadcrumb recorder now includes latitude/longitude, raw speed, filtered speed (mph), and current/target update intervals in its five-second CSV samples. This replaces the redundant periodic `gps_position` records in the main log. New-schema breadcrumb files use a `2.csv` suffix to preserve older three-column CSV files. Allow time to flush SD logs before power-off. Higher-rate behavior still requires moving hardware validation.

## Planned diagnostics

- Parse NMEA GSV to expose satellites in view (separately from GGA satellites used).
- Parse and expose course over ground from RMC as a bearing in degrees.
- Capture HDOP from GGA and, where emitted, additional DOP data from GSA.
- Report horizontal accuracy only if the receiver provides a defensible accuracy
  measurement; DOP alone is **not** an accuracy radius in meters.
- Add fix type and GPS UTC as supported by receiver messages. Per-satellite information is intentionally out of scope.

Hardware validation and ESP-IDF CI are required before treating this application as complete.
