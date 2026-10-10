# GPS diagnostics

The GPS application is a read-only presentation of the shared GPS location service.
It neither opens a UART nor acquires the shared GPS/nRF24 pins and does not write
position history to SD. Its descriptor has no exclusive service dependencies.

## Current display

- Receiver status distinguishes an inactive UART from a receiver without valid NMEA and a receiver with/without a fresh position fix.
- A fresh fix displays latitude, longitude, altitude (meters), speed (km/h), and fix age.
- The existing GGA satellite field is the number of satellites **used in the fix**, not all satellites in view.
- When the fix becomes stale, the app hides position and motion values rather than displaying them as live.
- The GPS service maintains a resettable trip odometer from plausible, moving RMC fixes while the receiver is active, even when the GPS application is not open.
- The trip counter ignores stationary jitter, implausible position jumps, and gaps in fix reception. It cannot reconstruct distance traveled without valid GPS fixes.
- Select opens a trip-reset confirmation. Rotate to YES and select to reset; long-press cancels. A reset is written immediately to internal flash.
- The distance is periodically checkpointed to internal flash (after at least 250 m of new travel and five minutes since the previous checkpoint). A sudden power loss can discard the most recent uncheckpointed distance; SD media is never used.
- The display refreshes twice per second. Normal global navigation applies outside the reset confirmation.

## Receiver and radio interaction

GPS detection and valid GPS position are distinct. A receiver may be present while
searching for a fix. The platform GPS UART uses pins shared with nRF24; this application
does not override the platform's established arbitration rules. With the GPS provider
inactive, the UI reports that state instead of attempting to take over the pins.

## Planned diagnostics

- Parse NMEA GSV to expose satellites in view (separately from GGA satellites used).
- Parse and expose course over ground from RMC as a bearing in degrees.
- Capture HDOP from GGA and, where emitted, additional DOP data from GSA.
- Report horizontal accuracy only if the receiver provides a defensible accuracy
  measurement; DOP alone is **not** an accuracy radius in meters.
- Add fix type and GPS UTC as supported by receiver messages. Per-satellite information is intentionally out of scope.

Hardware validation and ESP-IDF CI are required before treating this application as complete.
