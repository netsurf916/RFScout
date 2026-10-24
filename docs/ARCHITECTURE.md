# RFScout Platform Architecture

This is the canonical platform reference for RFScout's **implemented** shared architecture. The [README](../README.md) is the product/current-state anchor, application behavior lives under [`docs/apps/`](apps/), and future design or release planning belongs only in [ROADMAP.md](ROADMAP.md).

> This page describes implemented behavior. Future lifecycle proposals live only in [ROADMAP.md](ROADMAP.md).

## Documentation and ownership boundary

Platform documentation answers questions about shared contracts: board/profile legality, application lifecycle, resource ownership, memory, persistence, input/presentation, power, storage, audio, networking, and OTA.

Application pages own app-specific UI, interpretation, settings, and current limitations:

- [Home / Device Settings](apps/HOME.md)
- [System Events](apps/SYSTEM_EVENTS.md)
- [WiFi / BLE](apps/WIFI_BLE.md)
- [Hotspots](apps/HOTSPOTS.md)
- [CC1101](apps/CC1101.md)
- [nRF24](apps/NRF24.md)
- [Recorder](apps/RECORDER.md)
- [Audio Spectrum](apps/AUDIO_SPECTRUM.md)
- [Thermal](apps/THERMAL.md)
- [GPS](apps/GPS.md)
- [Wardriving](apps/Wardriving.md)

## Shared contracts and exceptions

Shared platform behavior is the default across all applications, including dialogs,
modal text sizing, input conventions, display-idle transitions, and other common
UI components. **An application-specific deviation must not be introduced without
explicit permission from the project owner.** A bug report or a suspected
workaround is not, by itself, permission to change the shared contract.

Every explicitly approved deviation **must be documented in this architecture
page** before or alongside implementation, identifying the affected application
or component, the shared behavior it differs from, the reason for the exception,
its intended scope, and any relevant validation or removal conditions. App-specific
documentation may link to that entry but does not replace it. When permission
has not been granted, investigate and fix the shared implementation instead
of silently introducing an exception.

## Board, app, and service boundaries

RFScout currently supports one runtime hardware profile, **[LILYGO T-Embed CC1101 Plus](hardware/T_EMBED_CC1101_PLUS.md)**, while preserving the profile/capability abstraction for future boards.

A runtime profile owns physical facts and expected capabilities: GPIO assignments, buses, display geometry, peripheral addresses, control pins, and which semantic providers RFScout is prepared to initialize. Applications depend on services/capabilities rather than board names or IDs.

Board detection and provider probing are separate:

1. perform the smallest passive fingerprint needed to recognize a supported profile;
2. select one immutable runtime profile;
3. initialize only buses/pins legal for that profile; and
4. let ordinary provider probes determine whether expected devices actually respond.

The current T-Embed fingerprint uses the early I2C path and onboard BQ25896/BQ27220 presence. The temporary detection bus is deleted before normal shared-I2C initialization.

Unknown or ambiguous hardware is a deliberate recovery state: RFScout leaves ordinary board GPIO/peripherals untouched and remains awake for recovery flashing.

### Current T-Embed profile

The hardware-validated profile owns the current 320x170 ST7789 display, rotary/center input, CC1101, nRF24, PDM microphone, speaker, SD, battery/charger telemetry, internal-temperature protection, display low-power behavior, deep sleep, and physical power-off semantics.

Shared physical resources include:

- LCD, microSD, CC1101, and nRF24 on shared SPI resources;
- battery/power devices and compatible external sensors on shared I2C;
- PDM capture on I2S0 behind the audio-capture service; and
- asynchronous speaker output on I2S1.

Providers must respect the selected runtime profile before configuring chip-select, interrupt, control, or destructive power pins.

### Runtime dispatch contract

The compiled application descriptor registry is authoritative for application identity, persistence identity, service membership, probe policy, activation behavior, runtime dispatch, and optional semantic transition veto.

Application availability is derived from configured enablement plus services/capabilities legal and available on the selected profile. Board-specific branching belongs in board/provider integration code, not application modules.

### Hardware probe contract

Optional application hardware uses one retry-safe **hot-plug** probe contract. The framework owns cached availability; individual drivers must not permanently cache a failed probe or substitute prior initialization state for a current hardware check.

There are only two descriptor policies:

- **None** — no runtime hardware availability gate is required.
- **Hot-plug** — run the same retry-safe probe during initial discovery and whenever navigation explicitly checks the app's availability (rotary navigation evaluating the next candidate, or a system-owned selection such as Home choosing an app).

There is deliberately **no background sweep** of inactive apps. Probes wake radios, power the microphone, and add shared-bus traffic, so they run only when the answer is about to be used. Hardware attached after startup is therefore discovered the next time navigation reaches that app, not announced in the background.

Probe diagnostics are quiet: per-attempt probe details log at debug level, and the framework logs/emits events only when an app's retained availability actually changes. Microphone availability probes check PDM data activity without the physical-clock measurement that real capture sessions perform, so a probe never replaces the last capture session's clock diagnostic.

A hot-plug probe may prepare reusable driver plumbing, but it must leave acquisition inactive and be safe to invoke again after either success or failure. Probe answers only whether the required hardware/dependency responds now. Initialization and activation remain separate lifecycle concerns.

When a shared provider is legitimately busy in another active application, a probe returns `ESP_ERR_INVALID_STATE`. The framework treats that as **deferred** and retains the previous availability instead of manufacturing a hardware-removal event. This is scheduling behavior, not a third availability state.

Runtime-observed loss may report the app unavailable. Once the app is inactive, the next navigation availability check can discover recovery again.

CC1101, nRF24, microphone-backed audio apps, Thermal, and Recorder all use this model. Radio probes perform real identity checks rather than treating an earlier successful initialization as permanent presence. Recorder rechecks both microphone and storage dependencies; when no card has ever mounted, a later probe may retry the normal storage mount.

The same retry principle applies to optional platform telemetry where practical. Battery gauge/charger handles are re-probed when absent and dropped/reacquired if the I2C device stops responding. GPS already continuously searches its supported UART rates until valid NMEA traffic appears.

Two platform cases remain intentionally distinct:

- **Board profile / mandatory UI providers** — these establish the hardware identity and minimum environment needed to run RFScout safely; failure is a platform startup/recovery condition, not an optional app being unavailable.
- **Mounted SD media after physical loss** — the storage probe latches the current volume unavailable. A subsequent media-store probe invalidates pooled file handles under storage arbitration, unmounts the lost volume, and attempts a fresh mount. Stateful clients must detect a changed media generation and rebuild volume-specific state.


The terminal deep-sleep path also clears the board-owned WS2812 LED ring after
successful lifecycle preparation and before entering sleep. This is required even
when the GPS app is foregrounded, because deep sleep does not dispatch an ordinary
app-leave event. Failure to clear the ring cancels sleep and rolls back terminal
preparation so a latched navigation pixel is not left powered during sleep.

The ring is a board-owned RMT provider (`led_ring`) exposing indexed RGB, intensity, clear,
and atomic frame updates; GPIO/RMT details never reach applications. The foreground GPS
application holds the only ring lease. Its `prepare_leave` hook clears and releases the ring
before an application transition commits; after one bounded retry for an RMT timeout, a
release failure vetoes the transition so no other application inherits a latched GPS
navigation pixel. No additional LED ownership state machine exists.

## Shared network profiles

`services/wireless/saved_networks` is the one credential store for Home/OTA and Hotspots. It holds up to eight profiles in Home's `networks` record. Each profile has a stable ID, SSID, password, the auth mode learned on its first association, and up to 32 BSSIDs marked **associated** (accepted the passphrase) or **rejected** (refused it). Several profiles may share an SSID; screens label them `SSID #n`.

- Every operation loads the record, applies one change, saves it, and erases its working copy, so a change made in either app is visible to the other at once. A failed save is returned to the caller and shown, never presented as saved.
- `saved_networks_list()` returns a non-secret snapshot. `saved_networks_password()` copies one password for one association; callers erase it as soon as the association has started. Passwords are never copied into observation tables, SD journals, logs, or telemetry.
- A BSSID can be associated with only one profile, and only under the auth mode the profile learned. Changing a password clears what the profile learned, because that evidence belonged to the old secret. Learned rejections are evicted oldest first when the BSSID list is full.
- Older record layouts are rejected and deleted like any other unsupported record ([Save/default semantics](#savedefault-semantics)).

Learning comes from association outcomes only: a successful join records the BSSID as associated, and an authentication failure or handshake timeout records it as rejected. Connectivity results (green/yellow/red) are not profile evidence. Home connects by profile ID and records the BSSID it reached; Hotspots records the BSSID of every secured check.

Hotspots resolves which profile a secured BSSID may use automatically: an exact associated BSSID with the same auth mode first; otherwise a same-SSID, auth-compatible profile that this BSSID has not rejected, but only with evidence — one of the profile's associated BSSIDs in the same scan, or with history within the configured match distance of a fresh fix. One such profile is used; several, or same-SSID profiles without evidence, require the user to choose one for a manual join. GPS is supplementary evidence only; resolution, association, characterization, and history all work without it. Secured association is BSSID- and channel-pinned, accepts only WPA2/WPA3 personal, and never accepts a WPA3-only BSSID under a WPA2 minimum.

The shared screens in `apps/network_common/network_credentials` give Home and Hotspots the same interaction: a profile view with the password masked until **Show**, **Edit password** and **Forget**; saving a password for an SSID with existing profiles as **Replace** or **New profile**; and picking an auth-compatible profile for one join.

Profile selection is kept separate from Hotspots' network grouping, which uses its own characterization evidence (gateway, netmask, gateway MAC). Neither layer classifies rogue access points.

## Application lifecycle, views, and resource sessions

Descriptors provide an optional `telemetry` callback. Core schedules it for the active app and owns structured record framing; apps own their metric fields. Terminal events such as Wardriving storage failures are emitted by the owning app on the runtime event path, not inferred by core polling.


Every active app has a temporary **resource session**, including apps that currently allocate no dynamic PSRAM. An existing session cannot be replaced by calling `app_session_ensure()` for another app, even if it has zero managed allocations: only a successful lifecycle close may retire that session and prove service quiescence. During destination setup, successful service initializations are stopped in reverse order if a later initialization or view acquisition fails. A service's `init()` must remain inactive and its `stop()` must safely release initialized-but-inactive state. If any initialization rollback `stop()` fails, the session retains ownership in a degraded state and blocks all subsequent session acquisition (including the same application). Destination recovery then fails closed rather than overlapping uncertain live resources; a restart is required. The same session owns the app's managed `rfscout_view_t`.

Application lock deliberately sits **above** this lifecycle boundary. Locking/unlocking the active application does not enter, leave, reactivate, quiesce, reclaim, or otherwise mutate its resource session; it only changes whether unhandled rotary input is permitted to request a different application.

The current canonical view is 320x170 and maps to the full T-Embed display. Applications render through their assigned view rather than owning the physical display.

The application-facing lifecycle is intentionally small:

1. **Enter** — establish inexpensive app-local state against the assigned view.
2. **Settle** — allow rapid navigation before expensive activation.
3. **Activate** — begin normal app/service work after the common settle point.
4. **Close** — optionally veto an unsafe semantic transition; otherwise platform teardown, view cleanup, and memory reclamation remain framework-owned.

A transition is transactional:

1. ensure the source session/view exists;
2. allow the source app's optional semantic `prepare_leave` veto;
3. quiesce each platform service declared by the source descriptor;
4. if service close fails, restore only safely restartable service state and keep source memory/presentation intact;
5. dispatch committed LEAVE cleanup after successful service close;
6. release/sanitize the source view;
7. bulk-reclaim the exited app's managed memory and verify no managed allocation remains;
8. force-close any SD media handle still owned by the exited app and record the count; and
9. establish the destination session/view before ENTER can use destination resources.

A successful service stop is the platform reachability proof: new work that can reference app state cannot enter, admitted workers/callbacks have drained, and shared hardware is inactive. Apps do not implement a second allocator/reachability protocol.

### Platform service lifecycle contract

Platform services use one lifecycle vocabulary with ownership split deliberately between the framework and the application:

- `probe()` answers app/provider availability only. It is retry-safe, minimally invasive, and leaves acquisition inactive.
- `init()` is framework-owned, idempotent, and lightweight. It may validate dependencies, initialize reusable driver/library state, register callbacks, or create dormant worker infrastructure, but it must leave the service inactive.
- `start()` is app-owned and idempotent for persistent services. It begins the service's ongoing activity after the app's normal activation point.
- Parameterized transactional services use explicit `begin_*(...)` operations instead of a generic `start()`; the application still owns when those transactions begin.
- `stop()` is idempotent and framework-safe. It quiesces active work and may also clear residual transaction state when already inactive.
- `is_active()` is authoritative for whether ongoing work currently owns resources.

The framework calls `init()` for every service declared by the destination app before ENTER/ACTIVATE can use it. Initialization success means a later app-driven `start()` or `begin_*(...)` is valid without repeating availability policy. Initialization does not imply activation.

When an app transition closes a session, the framework calls `stop()` for every declared service in reverse dependency order. If a later close step fails, persistent services that were active before the attempted close are restored with their same idempotent `start()`; parameterized transactions are not reconstructed implicitly. There is no separate resume/recover verb.

Capability bits name logical services, not raw drivers. Apps may still consume non-owning read/query APIs from lower layers where appropriate, but activation and teardown semantics must not bypass the declared platform-service boundary.

Once source cleanup has committed, RFScout does not resurrect the old application. If destination-session creation fails, lifecycle diagnostics record a committed-destination failure and the app manager attempts Home fallback without dispatching ENTER against an invalid view.

Current session-managed asynchronous services include Wi-Fi monitoring, framework network sessions, connectivity probing, OTA acquisition/installation, BLE monitoring, CC1101, nRF24, and shared audio capture/Recorder activity. Thermal acquisition is synchronous today.

## Memory policy

Growable retained app state uses `app_session_calloc()`, `app_session_resize()`, and `app_session_free()`. The lower-level memory manager supplies owner accounting, failure tracking, and bulk reclamation.

**Active-list eviction has two distinct purposes:** relevance expiration (completed observation cycles and, where meaningful, geographic distance) and resource-protection admission (a fixed allocation ceiling with deterministic least-recently-observed eviction). Expiration expressed in completed scans avoids time thresholds shorter than an application’s scan/check cycle. GPS-based Hotspots expiration measures how far the receiver is beyond the current observation session’s estimated coverage circle (half the latitude/longitude bounding-box diagonal), and never expels a BSSID seen during the current scan; with no fresh GPS fix, scan-count and capacity policies still operate. Active observation eviction must not erase durable SD history or saved profiles. Hotspots-specific defaults, configurations, and provisional memory ceiling are defined in [Hotspots](apps/HOTSPOTS.md#scanning-and-observation-sessions). The live cap does not bound the separately growing history index or guarantee internal-DMA/contiguous-PSRAM reserves; those require measured worst-case audits.

Shared SPI peripherals use the ESP-IDF SPI driver's device arbitration **plus** a board-level recursive SPI lock (`board_spi_lock()` / `board_spi_unlock()`). Every SPI2 client holds the board lock around each group of transactions it issues: LCD flushes per DMA chunk and panel commands, every SD/FatFs transaction (taken automatically inside the storage I/O arbitration, so storage users never take it directly), and each CC1101/nRF24 register transfer. The board lock is defensive: it keeps clients from ever contending inside the driver's own bus lock, which has had shared-bus races in some ESP-IDF releases (for example the `spi_ll_get_running_cmd(hw) == 0` assert in `spi_hal_setup_trans`). Hold times stay short; the LCD releases between chunks so radios and SD can interleave. When both locks are needed, the storage I/O lock is always taken before the board SPI lock.

A provider that must perform a **manual GPIO-level SPI sequence** outside a normal transaction must first reserve the shared bus through the board SPI abstraction (`board_spi_manual_acquire()`, which takes the board lock and acquires the driver bus), perform the manual sequence, release that reservation, and only then resume normal device transactions. Device drivers must not independently manipulate a shared-bus CS line while unrelated SPI clients can transact.

App-session **PSRAM** is for retained observations, histories, captures, and other meaningfully sized/growable state. Internal SRAM remains appropriate for RTOS/system state, task stacks, latency-sensitive allocations, and small bounded DMA/peripheral buffers. Providers should prefer the simplest memory class that satisfies the fixed capacity and peripheral constraints; external PSRAM is reserved for cases where retained/growable size materially justifies it.

Retained, dynamically growing records must reserve capacity before updating any index or
other in-memory state that is expected to survive a journal replay. Allocation failure
must not leave a visible but unjournaled record. Short-lived credential and HTTPS buffers
are released by their owning operation rather than retained until the next app exit;
app-session reclamation remains the final safety net, not the ordinary cleanup strategy.

There is no blanket per-app item quota: retained collections grow according to their semantics and resources. Explicitly bounded active collections, such as the Hotspots Live table, are exceptions justified by transient memory headroom; durable history remains independent. Presentation scratch can be bounded separately.

Usable external PSRAM is a supported-platform requirement. RFScout does not silently substitute internal SRAM with a hidden lower capacity.

## Semantic input and presentation

Applications consume semantic input rather than GPIOs. The current T-Embed provider maps the rotary encoder/buttons into signed navigation, Select, Long Press, and the global Hold shortcut. There is no global Back input semantic; explicit Back rows and app-local actions are ordinary Select-driven UI choices. The dedicated user/power key remains a separate power-policy input.

The user-facing gesture vocabulary is intentional:

- **Center / Select** — short center-button press and release; ordinary activation.
- **Long Press** — center-button release after at least 0.8 seconds but before the Hold action fires; app-defined secondary action.
- **Hold** — center button remains pressed for 2 seconds; framework-owned global Input Lock / App Lock shortcut.

UI hints use `LONG PRESS` and `HOLD` with those meanings.

### Input lock and application lock

RFScout exposes two deliberately different global lock semantics:

- **Input lock** suppresses ordinary semantic navigation/select input. The dedicated user/power key and the Hold unlock path remain available.
- **Application lock** keeps ordinary input active but prevents rotary navigation that the active application leaves unhandled from falling through to a global application transition.

Application lock is owned by the application manager. It does **not** create a new resource session, change application identity, or alter service ownership. Its purpose is to preserve the active application/session and its retained data while allowing the application to use rotary navigation for local modes, views, cursors, or other app-specific interaction.

The dispatch rule is therefore:

1. deliver semantic input to the active application;
2. if the application handles it, stop;
3. if an unhandled event is rotary navigation and application lock is active, remain in the current application;
4. otherwise apply the normal framework fallback; unhandled rotary navigation changes applications only when application lock is inactive.

The global Hold shortcut is configurable as **Input Lock** or **App Lock**. When input lock is already active, Hold always unlocks input regardless of the configured shortcut so the stronger lock retains an unambiguous escape gesture. Applications may also expose app-lock state through their own settings by calling the framework-owned lock API; they do not own a second private lock bit.

Long Press remains an app-level semantic. In the shared rotary text-entry component it means **Cancel**; accepting text requires rotating to the visible **Done** item and pressing Select. This avoids a second hidden accept path and keeps cancellation available without a separate Back gesture.

`RFSCOUT_EVENT_BACK` remains a logical UI/history/feedback event for explicit actions that move outward one local UI level (for example selecting a visible Back row). It is not an input event and does not trigger global application navigation.

Application lock is transient runtime state. It is cleared when an application transition commits and is not a distinct persistence/resource-ownership domain. A shared status-bar indicator renders an **A-marked lock** for application lock, distinct from the ordinary input-lock symbol. When both locks are active, the stronger Input Lock symbol temporarily supersedes the A-lock; the app-lock indicator returns when input is unlocked.

Edge capture survives temporary main-loop/display stalls. Input used only to wake a dimmed/off display is consumed before normal navigation or power handling.

Common presentation overlays are platform-owned. Applications render their view, while power/system-event/app-modal presentation is composed by the shared display service so application lifecycle state is not replaced by a transient notification.

## Audio architecture

Audio capture is board-neutral. The current T-Embed PDM provider uses I2S0; the service owns provider probing, open/read/close, DMA/I2S lifetime, and physical PDM-clock diagnostics. Recorder and Audio Spectrum consume normalized PCM rather than raw I2S handles.

The low-level `audio_capture` provider reports the configured capture rate and measured PDM-clock diagnostics. The shared Recorder/monitor layer currently measures delivered PCM throughput over wall time for its consumers: Recorder uses a valid measured rate in WAV metadata, and Audio Spectrum uses a valid measured rate for FFT frequency labels. That measurement has not yet been promoted into a generic calibrated `audio_capture` telemetry contract for future consumers; bounded clock/rate correction and generalized calibration remain roadmap work.

Speaker output is a separate semantic service on the validated T-Embed I2S1 path. UI/event/alert feedback policy is independent from explicitly requested content playback such as Recorder audio.

## Radio-service boundaries

The Wi-Fi and BLE monitor services normalize passive observations for the [WiFi / BLE app](apps/WIFI_BLE.md). Their app-owned collection/index lives in the active app session and both collectors must quiesce before that session is reclaimed.

CC1101 and nRF24 are receive-only scanner services advertised by the current T-Embed profile. Each owns a session-managed worker that must stop before app memory release. App-specific interpretation and current retained-history limits are documented in [CC1101](apps/CC1101.md) and [nRF24](apps/NRF24.md).

Infrastructure Wi-Fi is split into a low-level station driver and a framework-owned **network session**. Apps may consume passive scan snapshots from the station driver, but they do not own association, DHCP/NTP sequencing, connection deadlines, MAC policy, or teardown. Home and Hotspots both request a logical network session and observe only its high-level ASSOCIATING / TIME_SYNCING / LIMITED / READY / FAILED state.

The network-session policy is common across applications: every new association requests a fresh random locally administered station MAC by default, association/DHCP is bounded to 30 seconds, and NTP synchronization is attempted for up to 15 seconds only if the clock has not accepted valid GPS or NTP time within the previous hour of this boot. Association plus DHCP establishes the LAN session; recently trusted time or successful NTP promotes it to READY. If time synchronization is required and fails, the connection remains LIMITED rather than being torn down. Only READY sessions may start HTTPS consumers. Release synchronously tears the station down before another logical session may reuse it. The station driver drains its event-loop work during stop so stale ESP-IDF disconnect events remain below the framework boundary. If the factory MAC cannot be captured, randomization degrades to the current station MAC rather than preventing connectivity. The factory MAC is restored after randomized sessions whenever the station stops.

The station driver still provides a strongest-per-SSID scan projection for Home and a per-BSSID projection with security classification for Hotspots. The framework selects credentialed SSID association (Home), credential-free BSSID-pinned Open/OWE association, or passphrase BSSID-pinned WPA2/WPA3 association (Hotspots), then applies the same network-session lifecycle to each. HTTPS remains a consumer of READY network sessions: OTA acquisition and the Hotspots connectivity probe retain separate transfer semantics while sharing connection/time policy.

The connectivity probe is a separate transactional session-managed service whose parameterized `begin` performs one bounded, cancellable, certificate-validated HTTPS request (Cloudflare `/cdn-cgi/trace`) over whatever station connection is up and reports the HTTP status plus public egress metadata. Session close cancels it and waits for its worker to exit.

## Configuration and persistent records

RFScout starts from compiled defaults and overlays firmware-managed records from internal LittleFS. Removable SD is **not** configuration authority and editable `rfscout.json` files are not imported or synchronized.

Durable state uses logical owner/key namespaces:

- `system/config` — ordinary device-wide policy such as display/power behavior, themes, timezone/clock presentation, SD logging, app enablement/startup choice, global audio policy/gains, and alert policy;
- `app/<persistence_key>/config` — descriptor-confined app preference overrides;
- `system/time.policy` — retired (automatic time sync needs no stored mode); device configuration deletes it at startup;
- `system/breadcrumbs` — breadcrumb enablement/retention policy; and
- additional bounded `system/*` or app-scoped records owned by their service/application.

These are logical API names, not literal filesystem paths. The persistent-store service hashes logical keys and app identities into filesystem-safe private paths.

### Store guarantees

The persistent-store service owns LittleFS mount/path construction and serializes filesystem operations internally.

Current rules include:

- records are bounded to 16 KiB;
- a write atomically replaces one record using same-directory temporary + flush/fsync + rename;
- atomicity is per record, not a multi-record transaction;
- interrupted temporary files are ignored as authority and cleaned once at startup;
- reads/existence checks do not create missing app directories; and
- asynchronous app work can use a captured app/session generation so stale contexts are rejected after transition.

Normal app-facing operations cannot choose another app's namespace. Core orchestration has the privileged path needed to load/save system and app configuration records.

### Save/default semantics

Ordinary setting setters mark owned configuration dirty and the normal service loop flushes it automatically. There is no Home **Save config** action.

A missing record means compiled defaults. Malformed/unsupported owned records fall back to defaults for that owner. App configuration records are sparse overrides: returning all app preferences to their defaults removes the app config record.

Every reader must **delete a record it rejects** — wrong size, magic, version, or out-of-range values. Nothing short of erasing the device can otherwise clear an internal record, so a rejected record is never left in place. Fixed-size records use `system_store_load()` / `app_store_load()`, which read the exact size, apply the owner's validator, and remove the record on rejection; variable-length readers such as device configuration remove rejected records themselves. Records are not migrated across format changes: an older layout is rejected and removed like any other unsupported record, and records retired from the firmware are deleted rather than ignored.

The retired monolithic internal `rfscout.json` representation is not authoritative or migrated forward as an editable interchange format.

## Visual presentation

### Shared menu and icon contract

Applications retain ownership of menu contents, value formatting, current focus, edit mode, and action availability. The shared presentation layer owns the visual mapping of those states. Normal available labels use the normal theme foreground, focused available labels use selected styling, and values use subordinate/value styling. An OFF configuration value is not a disabled action. An unavailable or locked action keeps unavailable styling even when focused; selection must not make it appear executable. Status colors (warning, error, success, info) describe operational facts, not navigation focus. Do not use structural divider color for ordinary menu labels or reassign info to mean selected.

`ui_menu` exposes a small state contract with independent `selected`, `editing`, `available`, and `locked` flags. It resolves label/value colors and markers using `ui_theme`. Callers must not override unavailable styling with selection colors. Where a restriction is an actual lock, the shared padlock artwork may accompany the control; lack of hardware, media, or network is unavailable, not locked. The shared renderer never enables an action, and applications continue to guard actions in their input handlers. Both Simple and Pixel Quest retain the established marker semantics and platform chrome boundaries.

`ui_icon` owns shared immutable pixel artwork and the common status glyph geometry (including input lock, application lock, SD, thermal, GPS, and Pixel Quest title icons). Existing `ui_game` icon functions delegate to the same centralized artwork. Drawing uses the display framebuffer primitives, requires no allocations, workers, or lifecycle service, and never calls `display_present()`. A caller chooses whether an icon is shown and supplies semantic state/color and placement. Existing status-bar lock meanings remain distinct. Icon geometry must remain bounded by the intended drawing region, with host previews/tests and hardware display review for changes.

The shared palette remains RGB565 color authority; menu roles are resolved centrally rather than creating a duplicate stored palette. An additional theme color is warranted only when visual contrast testing demonstrates a distinct semantic role cannot be represented adequately by existing colors.


`ui_theme` exposes two independent persisted dimensions: **Style** (Simple, Pixel Quest) controls pixel-console geometry and artwork, while **Colors** (Default, Outdoor, Night) selects semantic RGB565 colors for either style. Outdoor increases contrast for daylight viewing; Night uses red-family indicators. New configurations start with Pixel Quest + Default. Existing combined `display.theme` preferences are migrated on load (Indoor to Simple/Default, Outdoor to Simple/Outdoor, Night to Simple/Night, Pixel Quest to Pixel Quest/Default); explicit saved style/color preferences override legacy values. Both preferences save through the device configuration and theme-change notification path, independently of audio policy.

Pixel Quest supplies original fixed 8×8 silhouettes and integer-scale pixel art through `ui_game`.
The widgets allocate no retained memory or workers. All apps use the same `ui_chrome` header,
including Home, System Events, WiFi/BLE and surveillance, Hotspots, CC1101 discovery/receive,
nRF24 spectrum/packets, Recorder, Thermal, Audio Spectrum, GPS, and Wardriving. Nested Home
network/update/time/power/app menus, radio/source/channel settings, and notification settings
inherit the same chrome and explicit menu markers. Device names and packet text are never
reinterpreted as selection markers.

The title icon occupies x=6–21 and the bounded title ends before the status region at x=142. Every theme bounds long titles to that same status boundary.
The game header's divider occupies y=20–21; menu selection bands clamp to y≥22, and the footer
endcaps occupy y=168. Existing data origins and rotary/Select/Long Press/Hold semantics remain
unchanged. Blue cursor glyphs and corner tabs identify the selected row consistently across apps within its existing band. The selection triangle becomes a hollow diamond while editing. In other themes,
these menu helpers render the original textual markers and graphs retain continuous columns.

Radio/audio graph gaps decorate the columns without quantizing their height, moving
peaks, or changing source measurements. Audio Spectrum uses a centered 304×128 plot below the header, with nine pixels reserved above the bars for peak labels; its automatic range follows the measured floor/peak without a fixed span clamp. Segmented battery and Recorder playback-volume meters preserve proportional fill, with dim unlit capacity cells. Zero/unknown/charging battery states leave the capacity cells unlit; the existing percentage/unknown text and charging bolt remain visible. Battery urgency, unavailable states, lock distinctions, and charging presentation remain semantic. Thermal's literal image pixels continue to bypass
palette translation; its relative-IR/calibration warnings remain visible.

Global dialogs use clipped corners and short colored brackets contained entirely inside the existing 236×70 saved
rectangle. Long titles reduce their integer font scale; long detail lines wrap into two lines.
Power overlays keep their existing priority over app modals and system notifications. The shared
presentation service restores the complete saved framebuffer rectangle after flushing the popup;
no themed frame/shadow draws outside that backup region.

Wardriving uses four instrument panels within its fixed app area. The Pixel Quest panels reuse the clipped-corner primitive; the three retained themes use semantic surface fills and rectangular divider borders. GPS/recording/channel status and a combined Wi-Fi/BLE/SAVE/DROP counter panel occupy a compact upper row, with taller independently scaled packet-rate and Wi-Fi-channel activity charts along the lower row. The platform status bar remains outside app drawing, and the dashboard has no influence on capture or channel sampling semantics.

WiFi/BLE uses four bounded, theme-aware instruments in three bands for its coherent observation snapshot: a full-height vendor panel on the left, compact stacked WIFI/BLE and UNKNOWN/RANDOM counts at the upper right, surveillance signatures below those counts, and a full-width single-row 14-channel AP-evidence matrix at the bottom. Pixel Quest retains its clipped-corner instrument primitive; other styles use semantic surface-filled rectangular borders. These panels change presentation only, not observation ownership or radio collection. Wardriving is adjacent to WiFi/BLE in the explicit navigation order while persisted numeric application IDs remain unchanged.

The scanner palette uses cyan instrument frames and amber controls on a dark navy surface.
Home places the original Scout inside a reticle above two fixed command panels. Shared panels
use clipped corners and short brackets; the footer uses quiet endcaps. Dense instruments retain their data
origins, graph extents, and Thermal image area. These motifs are static and allocation-free;
they add no animation timers or acquisition work. Application windows remain the existing
fixed 320×170 managed view; no flexible-window or app-resizing contract is introduced.

The [host preview command](../CONTRIBUTING.md#host-ui-previews) compiles production C views and
extracts the production driver's drawing primitives and font, with deterministic mocked services.
The production battery widget also runs against simulated gauge/charger states to check unlit capacity cells and the charging bolt. Its images verify presentation only, not RF acquisition or target-hardware performance.

## Time, location, and sleep state

`device_time` owns RTC-backed wall-clock validity/source, automatic GPS/NTP synchronization, timezone conversion, and manual time independently from any location provider.

The current T-Embed profile advertises optional external UART GPS on GPIO43/44. A compatible GPS/location provider remains capability-gated. Home infrastructure networking can provide NTP after DHCP; GPS and NTP update system time automatically; manual clock entry is disabled whenever trusted GPS or NTP time was accepted within the last hour. This is enforced in both Home and `device_time`; timezone controls remain available. Manual clock adjustments after that window invalidate trusted synchronization freshness, while retained and manually set time do not qualify as recent trusted sources. The existing five-second GPS signal freshness check is distinct from this one-hour network clock policy.

Deep sleep loses ordinary RAM, so supported profiles keep a small RTC-backed session envelope separate from persistent configuration. Current state preserves active application and input-lock state; menus, edit state, queued sounds, and active Recorder operations are not reconstructed.

## Power, battery, and display idle

Power actions are semantic and filtered by runtime profile capability. T-Embed supports screen-off, deep-sleep, and physical-power-off behavior.

Display dim/off is presentation policy, not application lifecycle or ESP32 sleep. The display provider can disable output/enter controller low power while acquisition/logging/audio continue; wake restores the latest framebuffer before brightness.

The current T-Embed fuel gauge/charger provides battery percentage and authoritative external-power evidence. Periodic battery measurements and the diagnostic UI use a two-second cadence, while gauge startup configuration advances independently through its nonblocking state machine. Protective low-battery action requires fresh battery data plus evidence that external power is absent and is normally evaluated every five seconds, with two consecutive qualifying checks to preserve approximately five seconds of debounce and the existing hysteresis/transition delay. Once a low-battery transition is latched, rechecks run every second to cancel promptly on external power or a disabled policy. Internal-temperature protection retains its one-second polling/confirmation cadence and is constrained by the active profile's supported terminal actions. Direct power-transition safety checks may refresh battery telemetry immediately.

Terminal sleep/power-off preparation flushes dirty configuration and quiesces required storage users. Active Recorder recording/finalization can veto the transition.

## SD storage and logging

SD is removable/bulk storage for recordings, logs, breadcrumbs, captures, and future explicit transfer packages. Internal configuration must remain usable without an SD card.

Optional SD-backed services attempt their ordinary storage operations while enabled, subject to the shared failure cooldown. A service may disable itself after its documented failure threshold, and that transition must produce a single system warning popup naming the affected service. Disabling an optional service must not disable unrelated recording or global storage. No service may turn its periodic update loop into an automatic card-reinsertion probe. A later explicit storage request may recover the media, but auto-disabled features remain disabled until re-enabled by policy or the user.

The storage service owns SD mount/recovery. A failed mount, card-health probe, or media I/O operation starts a shared three-second cooldown. Storage requests during that period fail immediately without accessing the card. The first request after the cooldown may attempt one synchronous recovery; no timer, background retry, or status display triggers recovery. A replacement volume invalidates existing handles, and clients discard optional data rather than queueing retries.

### Media store contract

All SD file access goes through the **media store** (`services/storage/media_store.h`). No system service or application constructs `/sdcard/...` paths, calls `fopen`/`opendir`/`mkdir` on the card, or depends on the mount point. The mount point is private to the storage service.

Files are addressed by **owner**, **collection**, and **name**, which map to readable FAT paths:

```text
/sdcard/<owner>/<collection>/<name>
```

- **Owner** is either the reserved `system` owner or an application's descriptor `persistence_key` (for example `audio` for Recorder). Applications cannot name another owner: app collections are resolved from the current app session. System collections are privileged platform API, in the same way as `system_store_*` on internal persistence.
- **Collection** is one owner-local directory such as `logs`, `breadcrumbs`, or `recordings`. Collection names are lowercase ASCII letters, digits, `_`, and `-`.
- **Name** is one file name within the collection. Names are validated: no path separators, no leading `.`, no control characters, and bounded length. Applications never pass paths.

Directory names are deliberately readable rather than hashed, because SD is the interchange medium a user inspects on a computer. Descriptor validation rejects a persistence key that is not a valid owner name or that collides with `system`.

A resolved collection is a small value that carries its owner identity. App collections also capture the app-session generation, so asynchronous work holding a collection from an exited app session is rejected rather than writing into the next session's namespace. Reads, listing, stat, and removal never create directories; opening a file for writing creates the owner/collection directories on demand.

Current layout:

| Owner | Collection | Contents |
| --- | --- | --- |
| `system` | `logs` | Rotating structured logs `rfscout-00.log` … `rfscout-99.log` |
| `system` | `breadcrumbs` | Optional movement-triggered GPS breadcrumb journal `breadcrumbs-*.jsonl` |
| `audio` (Recorder) | `recordings` | Recorder 16-bit mono PCM WAV files |
| `wardrive` (Wardriving) | `captures` | Wardriving capture files |
| `hotspots` (Hotspots) | `history` | Successful-characterization journal `hotspots-*.jsonl` |

Applications add their own collections under their persistence key, for example the WiFi/BLE `locations` collection for `surveillance-*.jsonl`.

Files written by earlier firmware under `/sdcard/audio/`, `/sdcard/logs/`, or `/sdcard/logs/breadcrumbs/`, earlier CSV breadcrumb buckets, and earlier unsegmented `surveillance.jsonl` journals are not migrated, browsed, rotated, or deleted by the current layout; they remain on the card for manual removal.

### Media file handles and arbitration

Open files are represented by opaque, generation-checked handles from a small fixed pool sized to the FAT open-file limit. A stale handle (already closed, or forcibly closed at app exit) is rejected rather than aliasing another owner's file.

Media file operations run under storage-service I/O arbitration, with a 250 ms lock-acquisition timeout on ordinary requests. Closing handles and app-exit cleanup remain available during the cooldown to release descriptors. Raw card-health probes therefore cannot interleave with a multi-command filesystem operation from any user, and no user takes the storage lock directly. Large transfers are naturally split into per-call transactions by their callers.

Handles opened through an app collection belong to that app session. The lifecycle coordinator closes any app-owned media handle still open after the source app's services have quiesced, records the count in the transition diagnostics, and treats a nonzero count as a committed resource-invariant violation in the same way as remaining managed allocations. Services that write asynchronously on behalf of an app (for example Recorder recording or WAV playback) must still finish or stop through their ordinary session service/veto path; forced close is a safety net, not a shutdown protocol.

After physical media loss, new opens and directory operations fail with `ESP_ERR_INVALID_STATE`; existing handles fail their next I/O but can still be closed. There is no remount beneath open handles.

The media store also exposes an explicit presence probe for navigation dependency checks. Failed media starts a shared three-second cooldown; a later file operation, capacity query, or explicit probe may attempt one synchronous mount/recovery. Cached availability reads never trigger I/O.

### Storage management

Home → Device Settings → **Storage** is the user's only tool for removing data, so it works per owner through the platform stores rather than by path. It is privileged platform UI, in the same way as `system_store_*` and `media_system_collection()`.

- **Internal records.** `persistent_store_space()` reports LittleFS capacity. `persistent_store_usage()` / `persistent_store_clear()` count or delete every record of one scope: `system`, one compiled app, *unused* app scopes whose hashed directory matches no compiled app (left behind by removed apps or renamed persistence keys), or everything. Owners hold loaded records in RAM and would write them back, so Home flushes dirty configuration, clears, and restarts the device; an erased owner then starts from compiled defaults.
- **SD collections.** `media_collections()` lists the collection directories that exist for the system owner and for each compiled app, and `media_owner_collection()` resolves an app collection without an app session (valid only while that app is not running, which holds while Home is foreground). `media_clear()` deletes a collection in bounded steps; it never deletes a file that a media handle holds open (for example the live log segment or breadcrumb journal), and removes the emptied collection and owner directories. Files outside `/<owner>/<collection>/`, such as legacy layouts, are never touched.
- **Confirmation.** Every erase opens a confirmation that names the owner, the record or file count, and the size, with Cancel selected; internal erases state that the device restarts. SD deletion runs a few files per poll with progress and Stop, and halts on the first media error. Each erase is recorded in the system log.

Storage has no persistent state of its own. Formatting the card is intentionally not offered on the device.

### Logging

Rotating logs use `rfscout-00.log` through `rfscout-99.log` in `system/logs`, up to 4 MiB each, while attempting to preserve at least 64 MiB free. The conservative hardware-tested SD baseline remains a FAT-compatible SanDisk card of 32 GB or less.

Breadcrumb recording is independent from the ordinary SD logging toggle and requires fresh valid GPS position **and GPS UTC**. A breadcrumb is queued once the receiver has moved at least the shared movement trigger since the previous breadcrumb, no more than once per second and never twice for the same GPS UTC second; nothing is written while stationary. Each record carries UTC, latitude/longitude, raw speed when fresh, and the GPS service's filtered speed and current/target receiver update intervals when available. Breadcrumbs use a `system/breadcrumbs` journal of 64 KiB segments; the retained-file setting is the journal's segment count. The Home and GPS menus access the same service-owned recording and retention settings.

### SD journals

Every SD event log (breadcrumbs, WiFi/BLE surveillance locations, Hotspots history) uses
the shared `media_journal` service rather than its own file handling. A journal is a numbered
series of segments in one media collection, `<prefix>-<8 hex digits>.jsonl`, where each line
is one JSON object with a `type` field, an optional integer `utc`, and `lat`/`lon` with seven
decimals when the record has a location. Callers own buffering, formatting, and scheduling;
the journal is not thread-safe and runs from the caller's own task or poll.

- An append writes one batch, syncs it, and truncates back to the previous size on failure.
  When that outcome is uncertain (truncate or close failed), the segment is abandoned and the
  next append starts a fresh `CREATE_NEW` segment, so a torn line is never followed by more
  records. Callers can tell a rolled-back batch (safe to retry) from an uncertain one (dropped
  rather than duplicated). Ordinary segment rotation and a failed attempt to create a new
  segment do not by themselves make an unwritten batch uncertain; a sequence number is
  committed only after its file was opened successfully.
- On first use after a mount the journal scans its collection; the newest segment is
  continued only when it ends with a complete line and is below the segment size.
- A segment that reaches its size bound is closed and a new one is started; segments beyond
  the retention count are deleted oldest first.
- Compaction writes a replacement base `<prefix>-<n>c.jsonl` through a temporary file,
  renames it into place, then deletes every older segment. A base supersedes all older
  segments; an interrupted compaction leaves either the old segments or the new base, and
  leftover temporary files or superseded segments are removed on the next scan.
- Readers replay complete lines oldest first. An unterminated final line of a segment is
  treated as a torn write and skipped.

### GPS movement trigger

One system setting, **Movement trigger** (5, 10, 25, 50, or 100 m; default 10 m), decides when
any SD log records another receiver location. A point qualifies only with a fresh GPS fix and
only once the receiver has moved at least that distance from the writer's previous logged
point (`geo_movement_gate`). Each writer keeps its own reference point: breadcrumbs keep one,
the WiFi/BLE journal one per surveillance identity, and Hotspots one per access point. The
trigger never limits live GPS data: the GPS service, dashboard, trip odometer, LED guidance,
live receiver bounds and distances, Wardriving packet metadata, and structured-log position
fields continue to see every fix.

The setting lives in system configuration and appears in Home **Device Settings** and GPS
Settings. Like the other receiver-dependent rows (breadcrumbs, retained files, GPS Timepulse),
it is shown unavailable and refuses changes until a GPS receiver is detected.


## Firmware image and OTA architecture

The current 16 MiB layout uses ESP-IDF A/B application slots rather than a single factory application partition. It reserves NVS/PHY metadata, OTA-data, two approximately 6 MiB application slots, and the remaining internal LittleFS storage.

Rollback support is bootloader-enabled. OTA-data is ESP-IDF/bootloader state rather than application configuration.

The combined image remains the clean-install/recovery artifact and contains the bootloader, A/B partition table, initialized OTA metadata, and application. Normal in-device OTA writes only the application image.

### Home network/update ownership

Infrastructure networking and Update are nested Home workflows, but the framework network session belongs to Home itself. Leaving Network or Update preserves a READY Home session; explicit Disconnect or leaving Home releases it.

Update requires a READY network session. A LIMITED Home session remains connected and can expose LAN state, but OTA acquisition does not attempt HTTPS until a later session reaches READY. The framework therefore preserves the proven Home behavior of keeping a DHCP-successful connection after NTP failure without duplicating connection/time logic in the OTA UI.

### Acquisition and installer

Catalog/image HTTPS acquisition is cancellable and uses ESP-IDF certificate-bundle validation.

The transport-neutral installer:

1. asks ESP-IDF for the running and next eligible OTA partition;
2. verifies a non-running destination and image fit;
3. streams bytes directly with exact byte counting and SHA-256;
4. completes ESP-IDF image validation;
5. verifies embedded RFScout project/version identity;
6. reads back exactly the declared artifact byte count and verifies SHA-256;
7. commits the destination boot partition only after every check succeeds; and
8. reboots immediately after commit.

SD and LittleFS are never firmware staging areas. The app/UI does not know partition offsets, labels, or OTA sequence counters.

The running firmware also logs SHA-256 over the exact on-flash application artifact range so it can be compared directly with the packaged `*-app.bin` digest.

### First-boot confirmation

A newly installed image remains pending verification until bounded mandatory common startup reaches the application-manager/display-idle checkpoint. RFScout then cancels rollback. Optional peripherals, removable storage, Internet availability, and reopening Update are not health prerequisites.

A reset/power loss before confirmation intentionally leaves ESP-IDF rollback semantics able to return to the previous working slot.

Pages/browser-flasher publication details and release procedure are operational concerns documented in [Firmware releases](operations/RELEASING.md). Remaining OTA fault-injection/hardware validation belongs in [ROADMAP.md](ROADMAP.md).

## System events and diagnostics boundary

The common System Events facility owns a bounded volatile structured event ring. Applications/services can emit semantic events without taking over presentation/lifecycle state. The [System Events app](apps/SYSTEM_EVENTS.md) is only a view of that platform history.

Persistent configuration/storage failures, hardware/service discoveries, and other supported diagnostics can therefore be represented independently of ordinary SD logs.

## Adding another board profile

A future compatible board profile should:

1. add an immutable profile under `services/board`;
2. add a distinct, minimally invasive early fingerprint;
3. leave unknown/ambiguous hardware inert;
4. advertise only capabilities RFScout can initialize safely;
5. keep applications dependent on semantic services/managed views;
6. implement new providers behind existing service boundaries where practical; and
7. validate power, shared buses, display transforms, RF/audio/storage behavior, and destructive/low-power actions on real hardware before advertising them.

Do not add a second profile merely because hardware is electrically similar. A profile should have a meaningful RFScout use case and enough hardware validation to justify its maintenance cost.

## Validation invariants

Release validation should exercise rapid app rotation, low memory, callback/timer races, worker-stop failures, dependency loss, Recorder recording/save vetoes, Audio Spectrum entry/exit, and failed-close restoration.

Key invariants are:

- successful app exit leaves zero managed allocations and zero open media handles for the exited app;
- an aborted transition preserves source memory/state/presentation;
- no worker/callback can reach reclaimed app state;
- baseline free PSRAM and largest-free-block behavior do not decline monotonically through repeated cycles;
- destination-session failure never dispatches ENTER without a valid view;
- unknown hardware remains inert and providers never initialize outside profile legality;
- internal configuration works without SD and corrupted/missing records recover to defaults;
- no module outside the storage service constructs SD paths or performs direct SD filesystem calls;
- Recorder operations cannot be truncated by application or terminal power transitions; and
- OTA never marks an image bootable before identity, structure, byte-count, streaming SHA, and flash-readback checks all succeed.

Build/release procedure is documented in [Firmware releases](operations/RELEASING.md). Application-specific behavior is documented under [`docs/apps/`](apps/).

## T-Embed GPS and nRF24 shared-pin priority

The T-Embed GPIO43/44 pair is shared by GPS UART TX/RX and the nRF24 CE/CSN interface. nRF24 identity discovery occurs during framework startup before GPS UART initialization. The probe powers the nRF24 down (`CONFIG=0x00`), detaches its SPI device, and releases both pins. GPS then probes five UART baud rates (38400, 9600, 19200, 57600, 115200) with a minimum two-cycle, 15-second initial detection window.

Checksum-valid RMC/GGA reception establishes GPS receiver presence independently of fix freshness. Presence expires after the Home-configurable NMEA inactivity timeout (default 5 seconds, 2–30 second range). While GPS is present, nRF24 acquisition is forbidden. After the initial startup detection window, a demand-driven nRF24 probe may temporarily stop an absent GPS provider, claim the pins, then restore GPS monitoring. An active nRF24 session holds the pins until app exit and cannot be preempted by GPS. Stopping the radio must quiesce its worker, power down, detach SPI, release the pins, and restore GPS detection; stopping an unstarted radio must not touch GPS-owned pins. GPS UART polling must never run while nRF24 owns these pins. Connecting or powering a GPS receiver during an active radio session is unsupported and may cause undefined hardware behavior.

This is a software pin-ownership policy, not galvanic isolation: nRF24 CSN remains electrically connected to GPIO44 in power-down. The target hardware must be checked for UART-induced CSN transitions, other SPI traffic, and any unintended radio behavior.

### Surveillance receiver locations and packet journal

The WiFi/BLE application's canonical observation store retains currently active identities
until their source-specific timeouts. For surveillance-qualified observations with a fresh
valid GPS fix, each identity expands a receiver-location latitude/longitude bounding box.
This box records **where RFScout received packets**, not a transmitter's verified position.
Expired identities are removed without any timeout-triggered storage write.

Every qualified packet still expands the live bounding box. A journal record is queued only
when the identity's receiver position has moved by the shared movement trigger since that
identity's previous record (its first qualified packet always records). Records go to the
app-owned `locations` collection through the shared `surveillance` journal (1 MiB segments,
64 retained) and contain full MAC, address type, classification, receiver latitude/longitude,
valid UTC when available, and last-seen milliseconds. The app poll batches SD writes outside
the observation-store lock; a rolled-back batch is retried during the current app session,
and an uncertain batch is dropped rather than duplicated. App exit attempts a final flush.
Insufficient memory or unavailable media can still result in loss; no cross-session queue
recovery is promised. The storage service records SD mount, loss, and recovery transitions in
System Events without checking the card when no client requests storage.

The WiFi/BLE dashboard's surveillance signature rows show the nearest available distance
among retained identities with that signature, without replaying the SD journal or
maintaining a second live list. Distance is computed to the nearest point of each
identity's receiver-observation bounding rectangle. Without a fresh fix or recorded
bounds, distance is unknown. The journal is for offline analysis, not live proximity replay.
There is no historical index, journal compaction, or geographic reload threshold.


## Current radio-scan ownership

Wi-Fi passive capture uses the framework's promiscuous receiver and hop scheduler. WiFi/BLE and Wardriving configure independent app-local Auto/Manual channel selections; manual selection supports an individual-channel mask, sequential/shuffled complete sweeps, and dwell presets. BLE uses NimBLE advertising discovery with independently selected passive/active mode, duplicate filtering, and scan-duty presets. An active BLE scan may issue scan requests to scannable advertisers, but it does not perform Wi-Fi station discovery. Native active Wi-Fi discovery is **not implemented**. See [WiFi/BLE](apps/WIFI_BLE.md), [Wardriving](apps/Wardriving.md), and [ROADMAP](ROADMAP.md).

## Semantic presentation colors

Application UI must use `ui_theme_get()` semantic colors rather than hardcoded RGB565 constants for backgrounds, text, title chrome, warning/error labels, or selection highlight regions. The shared `ui_color_pair_t` interface provides foreground/background roles via `ui_theme_pair()` for normal, secondary, surface, selected, warning, and error text; `ui_color_pair_reverse()` swaps the pair for intentional inverse video. Resolve pairs at drawing time so theme updates take effect without per-app state. Selected-row text and fill should come from the *same* selected pair. Literal image data and signal visualization color scales may remain independent of the UI theme when explicitly justified; graphs should otherwise use `graph_primary` / `graph_peak`. Display primitives render the exact RGB565 color supplied by the caller; no compatibility translation is performed. Applications must provide semantic palette values explicitly.

## Startup application service ordering

The display-idle policy is initialized **before** `app_manager_init()` dispatches the configured startup app's `ENTER` event. This ensures screen-awake requests from Thermal or Audio Spectrum are not cleared by later service initialization. App lifecycle callbacks should only depend on already initialized shared services, and future service dependencies must follow the same ordering rule. Note that platform, device configuration, theme and input initialization precede app entry.

## Opt-in display diagnostics

Diagnostics is a persistent, disabled-by-default application listed immediately before System Events in the navigation order. Enable it from Home > Device Settings > Apps > Diagnostics. Its appended application ID preserves all existing persisted IDs. The first page draws six canonical RGB565 swatches (red, green, blue, orange, cyan, white) with raw, non-theme-translated colors to validate display channel/byte ordering on hardware. Press the center/select control to switch to semantic palette swatches (background, text, accent, logo primary, logo secondary, selected). Turn the encoder to navigate to another application. Diagnostics owns no radio service, and no display-driver RGB565 configuration is altered.
