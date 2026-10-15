# GPS LED ring and breadcrumb modernization (work in progress)

## Hardware boundary
The T-Embed CC1101 Plus exposes eight WS2812 LEDs on GPIO14. Implement a board-owned
ESP-IDF RMT driver and an app-facing API for indexed RGB color, intensity, clear and
atomic frame updates. Keep GPIO/RMT specifics out of GPS and other applications.
GPS owns the foreground LED lease. Its `prepare_leave` hook clears and releases
the ring before app navigation commits. If release fails (after one bounded retry
for an RMT timeout), the app transition is vetoed instead of allowing another
app to inherit a latched GPS navigation pixel. No extra LED ownership state
machine is needed; the common app lifecycle already supplies this boundary. Do not confuse course-over-
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

## Hardware test procedure (PR #126)

Preconditions: use the supported T-Embed CC1101 Plus with the tested CASIC
GPS module and an SD card if checking breadcrumbs. Flash a CI-built image of
this PR and retain a serial log. **Do not merge on CI alone.**

1. **Startup / ownership:** Boot with GPS attached; confirm valid NMEA and
   GPS display. Open GPS, then leave for Home and another app repeatedly.
   The eight-LED ring must go dark at each exit and other apps must not
   inherit a GPS direction pixel. Repeat with GPS LED guidance disabled.
2. **LED mapping:** Enable LED guidance in GPS Settings, set brightness low,
   and change the six color presets. With safe, known motion/course, record
   which physical index lights for N/NE/E/SE/S/SW/W/NW. Ground track is
   **direction of motion**, not where the enclosure faces; do not use a static
   hand-rotation test to infer heading. Verify stationary/no-fix blanks the
   ring. Check 0% and 100% intensity and change settings while active.
3. **Timepulse:** Toggle **GPS Timepulse** in Home and GPS Settings. Confirm
   both menus share the saved preference. Verify the receiver's own flashing
   indicator follows the command without interrupting NMEA or retriggering
   the full rate-probing scan. This is best-effort: the menu shows the saved
   requested state, **not** independent evidence of the receiver state.
4. **Background service:** Disable or leave the GPS app. Verify receiver
   detection and (when enabled) breadcrumb collection continue; verify
   nRF24 shared-pin ownership rules remain unchanged.
5. **Breadcrumb cadence:** Collect a log while stationary and while moving
   through adaptive receiver-rate tiers. With rate unavailable/unverified,
   expect at most one record per 5 seconds; with a verified fast rate,
   no more than one record per second. Check fresh fix/UTC validity,
   duplicate-second suppression, UTC bucket boundaries, CSV column schema,
   SD removal, and file-count retention. A faster receiver rate does
   **not** imply subsecond breadcrumb writes.
6. **GPS settings and trip:** Test Reset trip only from GPS Settings; verify
   the dashboard contains no reset helper text and the settings footer
   reports failed actions. Test keep-awake and inactivity timeout.
7. **Failures:** Disconnect the module, repeat app transitions, and inspect
   logs for bounded CFG-TP errors, graceful NMEA behavior, and safe LED
   cleanup. Record any LED/RMT timeout or persistent illuminated pixel.
   A persistent hardware transport failure must block an ordinary app switch
   until the ring can be safely blanked/released; power failure and physical
   wiring defects cannot be solved in software.

Capture: board revision, module model/firmware, flash build SHA, selected
color/intensity, observed LED mapping, receiver rate, serial log excerpts,
sample CSV, expected/actual behavior, and any failures.
