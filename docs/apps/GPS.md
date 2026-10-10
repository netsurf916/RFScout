# GPS diagnostics

The GPS application is a read-only presentation of the shared GPS location service.
It neither opens a UART nor acquires the shared GPS/nRF24 pins and does not write
position history to SD. Its descriptor has no exclusive service dependencies.

## Current display

- Receiver status distinguishes an inactive UART from a receiver without valid NMEA and a receiver with/without a fresh position fix.
- A fresh fix displays latitude, longitude, altitude (meters), speed (km/h), and fix age.
- The existing GGA satellite field is the number of satellites **used in the fix**, not all satellites in view.
- When the fix becomes stale, the app hides position and motion values rather than displaying them as live.
- The display refreshes twice per second. Navigation remains under normal framework control.

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
- Add fix type, GPS UTC, and per-satellite details as supported by receiver messages.

Hardware validation and ESP-IDF CI are required before treating this application as complete.
