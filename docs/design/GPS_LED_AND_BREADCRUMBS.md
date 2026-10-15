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
LED color, Stay awake, breadcrumbs enabled/file retention, and GPS inactivity timeout).
Retain system-owned storage/retention and background breadcrumb capture while moving
their controls to GPS. The GPS app is a *view of system services*, not their owner.

## Breadcrumb capture
The current recorder is fixed at five seconds and suppresses duplicate Unix-second
timestamps. Its source includes *verified* GPS current update interval, distinct from
target policy. Derive a bounded recorder interval from verified input cadence, avoid
duplicate fixes, and use subsecond monotonic acquisition timestamps when output rate
exceeds 1 Hz. The existing hourly/ten-minute file buckets and file-count retention
are not a byte cap; monitor SD write amplification and queue depth. Never raise output
rate solely because a faster rate was requested but not ACK/readback verified.

## Receiver controls
Baud probing, NMEA output and CASIC rate auto-negotiation remain strictly platform-owned
and are **not** configurable from the GPS application. The only proposed user-facing
receiver-hardware setting is its onboard LED flash/output pulse enablement, and that
must remain unavailable unless the exact receiver command and electrical signal are
confirmed. That setting is distinct from the RFScout eight-LED direction ring.
Do not send speculative receiver configuration packets.

## Acceptance
Host tests cover sector math, no-fix LED blanking, RMT frame construction,
setting persistence, rate clamp/freshness/dedup, and legacy CSV compatibility;
ESP-IDF and formatting CI green. Hardware checks validate LED index order, color
channel order, intensity, GNSS hotplug, screen sleep, and SD sampling under motion.
