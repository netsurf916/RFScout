# GPS LED ring and breadcrumb modernization (work in progress)

## Hardware boundary
The T-Embed CC1101 Plus exposes eight WS2812 LEDs on GPIO14. Implement a board-owned
ESP-IDF RMT driver and an app-facing API for indexed RGB color, intensity, clear and
atomic frame updates. Keep GPIO/RMT specifics out of GPS and other applications.
GPS owns the foreground LED lease and clears it on leave; other applications must
not accidentally inherit stale navigation indications. Do not confuse course-over-
ground with device/compass heading.

## GPS behavior and settings
Map validated RMC ground track into eight azimuth sectors N, NE, E, SE, S, SW, W,
NW, only when a fresh fix, valid track, and sufficient speed are present. The eight
physical LED index-to-azimuth positions must be confirmed on hardware before relying
on directional mapping. Default LED guidance off. Persist enabled, 0-100% brightness,
RGB color, and GPS display keep-awake. Avoid illuminating on stale/no-fix readings.

Rework the GPS app's settings menu (Back, Reset trip, LED enable, LED intensity,
LED color, Stay awake, breadcrumbs enabled/file retention, GPS inactivity timeout,
and GPS Timepulse).
Retain system-owned storage/retention and background breadcrumb capture; expose
those same controls in both Home and GPS, backed by service-owned settings.
The GPS app is a *view of system services*, not their owner.

## Breadcrumb capture
Breadcrumb recording automatically adapts between **five seconds and one second**.
With an unavailable or unverified receiver update cadence, use the conservative
five-second interval. When a verified receiver rate is available, allow recording
up to once per second, never more frequently, regardless of how fast the receiver
itself reports fixes. Do not write duplicate GPS UTC seconds. A valid fresh position
and GPS UTC are required. This deliberately preserves the existing second-precision
CSV timestamp format and the existing file-count retention policy (which is not a
byte cap). Rate increases are automatic, not a user-configurable GPS setting.

## Receiver controls
Baud probing, NMEA output and CASIC rate auto-negotiation remain strictly platform-owned
and are **not** configurable from the GPS application. The shared Home/GPS
setting is **GPS Timepulse: On/Off**, controlling the receiver's timing-pulse output
rather than naming its onboard LED. The tested receiver's output drives its
flashing indicator; the GPS service owns its CASIC CFG-TP update and readback.
Changes are best-effort: command failures are logged, and a later receiver query
can reveal whether hardware matches the stored preference. The current menu
shows the persisted desired value, not an observed hardware state; displaying
readback status would require additional UI wiring. Do not add indefinite
retry loops or deferred-persistence recovery just for the timepulse.
It is distinct from the eight-LED direction ring, whose enable/color/brightness
controls belong only in the foreground GPS application.
Do not send speculative receiver configuration packets.

## Acceptance
Host tests cover sector math, no-fix LED blanking, RMT frame construction,
setting persistence, rate clamp/freshness/dedup, and legacy CSV compatibility;
ESP-IDF and formatting CI green. Hardware checks validate LED index order, color
channel order, intensity, GNSS hotplug, screen sleep, and SD sampling under motion.
