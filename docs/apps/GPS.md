# GPS diagnostics

The GPS application presents receiver telemetry from the shared GPS location
service and provides a settings menu for trip reset and GPS-related preferences.
It neither opens a UART nor acquires the shared GPS/nRF24 pins and does not itself
write position history to SD. Its descriptor has no exclusive service dependencies.

## Current display

The 320×170 app surface now uses four semantic, theme-aware instruments beneath the global status strip: a **Fix** panel for latitude/longitude, altitude, fix freshness/age and receiver status; a **Motion** panel for cumulative trip distance and current velocity; a **Satellites** panel for GGA satellites used; and a larger **Ground Track** panel showing a prominent compass direction and numeric degrees.

Ground track comes directly from the RMC **course-over-ground** field, not inferred from screen updates or an electronic compass. It is shown only for a fresh position fix, fresh receiver motion and speed of at least 0.7 m/s, to avoid unstable headings while stationary. When unavailable, both indicators show placeholders. The heading is direction of travel, not the direction the physical device faces. In Pixel Quest style, panels use clipped-corner frames; Simple style uses rectangular semantic frames, and both use the active theme colors. The center/select button opens GPS Settings with trip reset, GPS-app-only directional LED options, stay-awake, shared breadcrumb enablement/archive status, Movement trigger and inactivity timeout, and receiver GPS Timepulse. Back returns to the dashboard. Trip reset is only available through GPS Settings;
there is no reset instruction or reset-failure helper text on the dashboard.
Menu action errors are shown in the settings footer instead.

## Telemetry validity and trip behavior

- The Fix panel distinguishes an inactive UART from a receiver with no fresh position fix; the satellite panel reports independently fresh GGA satellite information.
- A fresh fix displays latitude, longitude and fix age; altitude (meters) is shown only with recent GGA data, and speed (km/h) only with recent RMC data. Missing or stale source fields display `--` rather than fabricated zeros or old values.
- The existing GGA satellite field is the number of satellites **used in the fix**, not all satellites in view. Its value also expires independently when GGA data stops arriving.
- When the fix becomes stale, the app hides position and motion values rather than displaying them as live.
- The GPS service maintains a resettable trip odometer from plausible, moving RMC fixes while the receiver is active, even when the GPS application is not open.
- The trip counter ignores stationary jitter, implausible position jumps, and gaps in fix reception. It cannot reconstruct distance traveled without valid GPS fixes.
- Select opens GPS Settings. Rotate to choose an entry; select to change it (or reset the trip), Back or long-press exits the menu. Breadcrumb, Movement trigger and GPS Timepulse preferences are shared with Home; those receiver-dependent rows are shown unavailable and refuse changes until a GPS receiver is detected. Direction LED preferences control only the GPS application's use of the ring.
- The distance is checkpointed to internal flash once at least 250 m of new movement **and** five minutes since the previous checkpoint are reached, including while stationary or when nRF24 has temporarily suspended the GPS UART. A sudden power loss can discard the most recent uncheckpointed distance; SD media is never used.
- The display refreshes twice per second. Normal global navigation applies outside the GPS Settings menu.
- GPS app periodic SD telemetry includes receiver/fix state, trip distance, and fresh speed/altitude/satellites-used values when available. The normal passive-app audio feedback policy applies.

## Planned follow-up: breadcrumb cadence and retention

The [breadcrumb policy design baseline](../BREADCRUMB_POLICY.md) specifies a
60-to-1-second filtered-velocity sampling curve (1 Hz from 10 m/s), a
60-second stationary heartbeat requiring a fresh GPS fix, and editable
1 day / 1 week / 1 month / Forever retention of dated files. **Not yet
implemented:** the 5/2/1-second movement-gated policy and archive-All UI
above remain current on the parent branch.

## Receiver and radio interaction

GPS detection and valid GPS position are distinct. A receiver may be present while
searching for a fix. The platform GPS UART uses pins shared with nRF24; this application
does not override the platform's established arbitration rules. With the GPS provider
inactive, the UI reports that state instead of attempting to take over the pins.

## Supported receiver configuration

RFScout supports GPS receivers configured to emit valid NMEA position/speed updates
at **1 Hz or faster** (an interval of 1000 ms or less). Configure slower receivers
to at least 1 Hz before using them with RFScout. Receivers reporting an initial
update interval greater than 1000 ms are outside the supported configuration,
including for adaptive-rate discovery, speed-sample freshness, and UART baud-switch
verification. The firmware does not guarantee reliable detection, baud negotiation,
or adaptive upshifts from such slow initial configurations. This requirement is
independent of the time required to obtain a GPS position fix.

## Breadcrumb sampling and storage

Breadcrumb logging remains a system service even if the GPS app is closed or disabled. A breadcrumb requires fresh location, GPS UTC and at least the shared **Movement trigger** distance since the last queued point. The sample cadence is **5 s below 3 mph, 2 s between 3 and 15 mph, and 1 s at 15 mph or higher**, using filtered GPS speed when available; there is never more than one record per UTC second and no stationary spam. Records are append-only JSONL in `system/breadcrumbs/breadcrumbs-YYYY-MM-DD.jsonl`, one file per UTC day. Older files, including files from the previous segmented-journal system, remain on the card unless explicitly deleted by the user. Home/GPS show **Breadcrumb archive: All** as a non-editable status; the previous retained-file count preference is ignored. Enablement and the shared movement trigger remain editable in both menus. See [Architecture](../ARCHITECTURE.md#gps-movement-trigger) for the shared trigger and storage convention.

## Receiver timepulse and LED ring

**GPS Timepulse** controls the receiver's timepulse output, known on the tested module to drive its flashing indicator. Configuration is best-effort: an unsupported/rejected/timed-out update is logged, and the receiver's actual state is checked on a subsequent receiver configuration query. The menu currently displays the persisted desired value, not a measured receiver state; a future query can reveal a mismatch, but automatically updating the menu from that readback is not yet implemented. No separate persistence-recovery or indefinite retry loop is added. It is a shared, persisted On/Off preference shown in Home and GPS Settings; the GPS service applies it through CASIC CFG-TP read/modify/write and verification. This is distinct from the eight-pixel directional LED ring, which belongs to the foreground GPS application and is cleared/released on exit. Hardware calibration maps North to physical LED index 2 (index 0 is West), a +90-degree/two-LED offset. Ring enablement, brightness, and color are GPS-app-only settings. Baud probing and automatic receiver update-rate selection remain platform-managed and are not settings menu choices.

## Receiver rate management

The shared GPS service detects NMEA at the receiver's existing UART baud (9600 on the tested HT1818Z3G5L). It probes CASIC read-only commands; this receiver NAKs MON-VER/MON-HW but answers CFG-PRT and CFG-RATE. CASIC commands use a four-second response deadline. Timed-out discovery probes and the initial read-only CFG-RATE query are retried twice before giving up, because the receiver may already be producing higher-rate NMEA traffic on a 9600-baud link. Retries are recorded in the main SD log. A valid CFG-RATE interval enables adaptive updates; otherwise generic NMEA operation continues.

The adaptive policy characterizes receiver update intervals from 1000 ms down to
100 ms in 100 ms steps and builds speed buckets from only the ACK/readback-verified
intervals. The slowest verified rate is used below 3 mph; the fastest is selected
at 15 mph and above. When three or more rates are supported, intermediate
thresholds are spaced evenly between 3 and 15 mph. For example, a receiver
supporting 1000, 500, 200, and 100 ms switches at 3, 9, and 15 mph.
If only one rate is verified, it stays fixed. Rejected intervals are excluded
from bucket generation, including after a later runtime rejection.

Rate increases require 5 seconds in a higher tier, decreases require 20 seconds, and at least 30 seconds separates changes. The rate policy uses a separate exponentially smoothed GPS speed estimate (90% previous/10% new at 1 Hz, adjusted for each sample interval), without changing displayed raw speed or trip calculations. After 115200-baud NMEA verification, firmware probes the ten candidate update intervals from 100 through 1000 ms, then restores the original verified interval before enabling speed-driven changes. This is a bounded startup characterization, but it can take noticeably longer than the previous four-rate scan. Every probe requires an ACK and readback. Explicitly rejected intervals are cached as unsupported for the GPS service session; the policy then selects the fastest non-rejected interval no faster than the speed target. A rejection no longer disables other verified rates. Ambiguous timeouts and readback mismatches still stop automatic adjustments rather than assuming a receiver rate. After confirming CASIC support and the initial rate, firmware attempts a session-only transition to 115200 baud when a matching CFG-PRT UART profile is available. It preserves the profile's protocol/mode fields, keeps ownership of the shared UART pins while waiting for fresh checksum-valid NMEA at the new baud, and resumes baud discovery only if the dedicated eight-second verification deadline expires. No configuration-save command, sentence-output change, or persistent receiver configuration is sent.

Serial logs summarize CASIC discovery, initial configuration, rate changes, and errors without routine raw packet dumps. The normal SD logs record `gps_policy` JSON events for receiver detection, protocol confirmation, initial rate, rate requests, acknowledgments, verified changes, and failures. Records include baud, requested/actual interval, and filtered speed when available, independent of the active app. The optional SD breadcrumb journal records latitude/longitude, raw speed, filtered speed (mph), and current/target update intervals with each movement-triggered sample, replacing periodic `gps_position` records in the main log. Allow time to flush SD logs before power-off. Higher-rate behavior still requires moving hardware validation.

Hardware validation and ESP-IDF CI are required before treating this application as complete.


## Status indicator

The system GPS indicator uses a compact satellite silhouette: warning when detected without a fresh fix, success with a fresh position fix, and no indicator when the receiver is absent.
