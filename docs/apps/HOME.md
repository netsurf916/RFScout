# Home / Device Settings

Home is RFScout's always-available landing application and the fallback when another application is disabled, unavailable, or cannot be entered safely. Shared platform contracts referenced here are defined in [Platform architecture](../ARCHITECTURE.md); future Home work belongs in [ROADMAP.md](../ROADMAP.md).

## Purpose

Home provides the low-allocation device/settings surface. Turning the rotary control from the landing view navigates applications; the center control opens Device Settings.

The Home descriptor requires no activation delay and owns the temporary infrastructure-network and OTA services used by its nested **Network** and **Update** menus.

## Device Settings

Current settings include:

- brightness and automatic dim/screen-off timing;
- Indoor / Outdoor / Night visual theme;
- independent Normal / Quiet audio policy;
- UI, Event, and Alert presentation volumes;
- 12/24-hour clock presentation;
- Auto/Manual time policy, timezone, and manual date/time fields;
- preferred startup application and per-app enablement;
- SD logging to the `system/logs` media collection;
- supported power-button actions and protective power policy;
- breadcrumb recording to the `system/breadcrumbs` media collection and retained breadcrumb-file count;
- global Hold shortcut selection between **Input Lock** and **App Lock**; and
- application notification preferences exposed through the relevant app settings.

Ordinary setting changes are persisted automatically through RFScout's internal persistent-record service. There is no user-facing Save step.

Home remains system policy: app enablement and preferred startup app do not prove hardware availability. If a preferred app is disabled or unavailable, RFScout falls back without erasing the stored preference.

## Network

**Device Settings → Network** provides deliberate infrastructure Wi-Fi access for update/time use without coupling station operation to the passive WiFi/BLE monitor application.

The current workflow:

- passively scans for nearby APs;
- stores up to eight Home-owned remembered SSID/password entries;
- highlights remembered networks observed in the latest scan;
- requires an explicit connect action;
- presents the framework network session as a modal **Connecting...** → **Setting time...** transaction, then returns to the Network main screen once the session is READY or LIMITED;
- lets a short center click cancel the transaction; cancellation releases the whole logical network session through the framework;
- inherits the common framework deadlines: 30 seconds for association/DHCP and 15 seconds for required NTP synchronization;
- keeps normal connection/time progress out of the persistent Network screen, reserving its footer for terminal errors;
- supports rotary password entry, disconnect, and forget;
- can use a targeted **Connect anyway** active search for a remembered network not seen passively;
- obtains DHCP state; and
- uses the same randomized-MAC-by-default policy as every other infrastructure connection.

The network session belongs to Home rather than to the nested Network or Update page. Leaving Network therefore keeps either a READY or LIMITED LAN session connected. Explicit Disconnect or leaving Home releases it and clears transient driver credentials/results.

ESP-IDF Wi-Fi persistence remains RAM-only so the driver does not become an independent credential authority.

## Firmware Update

**Device Settings → Update** only starts network acquisition when the Home-owned network session is READY. READY means association, DHCP, and NTP synchronization all completed under the common framework policy. LIMITED means DHCP succeeded but NTP did not; Home retains that LAN connection, but Update deliberately does not attempt HTTPS while it is limited.

The UI supports:

- checking for the newest compatible published stable release;
- showing the running **Installed** version separately from the stable **Update** candidate/status and current **Network** state;
- browsing retained main, pull-request, draft-release, prerelease, and published-release builds while keeping the installed version visible for comparison;
- showing **Installed**, **Selected**, and candidate commit metadata before an explicit/manual build installation;
- explicit/manual installation of an exact retained build;
- download progress, verification, and reboot presentation; and
- cancellation when Update/Home is left before the final boot commit.

Catalog acquisition uses HTTPS with ESP-IDF certificate-bundle validation. Firmware bytes stream directly into the platform OTA installer; SD and LittleFS are not staging areas.

Leaving Update cancels acquisition/installation but deliberately preserves the Home network session. The platform-level installer, A/B partition, image-validation, and rollback contracts are documented in [Platform architecture](../ARCHITECTURE.md).

## Time

`device_time` owns wall-clock validity/source and timezone conversion.

- **Manual** exposes local Year/Month/Day/Hour/Minute controls and persists Manual policy before changing the clock.
- **Auto** hides manual fields and may accept a valid GPS UTC source when such a provider is available.
- A successful Home Network connection applies synchronized NTP UTC without changing the stored Auto/Manual policy.

The T-Embed profile enables optional UART GPS detection on GPIO43/44, which are also used by the nRF24 shield. After startup nRF24 discovery and power-down, GPS probes 38400, 9600, 19200, 57600, and 115200 baud. A checksum-valid RMC/GGA sentence establishes receiver presence even without a location fix and disables nRF24 activation until reboot. If no receiver is detected, nRF24 may use the pins after the initial two-cycle (15-second) GPS detection window; GPS polling pauses during radio use and resumes after radio shutdown.

## Power, display, and persistence

The current T-Embed profile supports semantic screen-off, deep-sleep, and physical-power-off actions. Display dim/off is presentation policy only; applications and acquisition continue until an actual terminal power transition occurs.

Active Recorder recording/finalization can veto terminal transitions until the file is safe. Dirty configuration is flushed before terminal sleep/power-off preparation.

The small deep-sleep RTC envelope preserves active-app and input-lock session state. Application lock is deliberately transient navigation state and is not restored after deep sleep. Menus, edit state, queued sounds, and active Recorder operations are not reconstructed.

## Current limitations

Home does not yet expose the richer memory/lifecycle diagnostics planned for v0.4.0. Shared capability/status presentation is also intentionally limited to the current application-availability and service state exposed by the implemented menus.
