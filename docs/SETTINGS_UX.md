# Application settings UX

RFScout application settings should feel like one interface even when individual applications expose different capabilities.

## Canonical organization

Application menus use the following relative category order when those categories are present:

1. **Back**
2. **Alerts / notifications**
3. **Detection or acquisition behavior** (profiles, scan ranges, dwell, retention, thresholds)
4. **Display / presentation**
5. **Storage / logging / recording**
6. **Advanced or hardware-specific settings**
7. **Destructive or reset actions**

Applications do not add empty placeholder categories. The rule is relative ordering: if two applications expose the same category, users should encounter it in the same part of the menu.

Alert configuration therefore belongs near the top of every detecting application's settings rather than at an app-specific arbitrary position. Reset/clear actions belong after ordinary configuration so an action is not interleaved with persistent preferences.

## Interaction model

Small finite choices use direct center-button cycling when a separate edit mode adds no useful precision. This includes ordinary On/Off settings and short enumerations such as Audio policy and Alert presentation mode. Numeric ranges and values that benefit from repeated encoder movement continue to use enter/turn/done editing.

Submenus use center to enter and Back to return to the owning application menu. The same setting should not use direct-cycle behavior in one app and enter/turn/done behavior in another without a functional reason.

## Audio classification

RFScout uses semantic audio classes rather than allowing each application to invent its own meaning for a beep:

- **UI / device** — navigation, accepted actions, lock state, and other device-interaction feedback.
- **Event** — lightweight informational activity such as ordinary WiFi/BLE detection beeps.
- **Alert** — sounds intended to get attention, including warning/error semantics and configurable tone/Morse detection alerts.
- **Content** — explicitly requested application audio such as Recorder playback. Content audio is not one of the three global incidental-volume categories.

Device Settings exposes **UI volume**, **Event volume**, and **Alert volume** together with the device-wide **Audio policy**. Normal permits UI, Event, and Alert presentation subject to app policy; Quiet suppresses those incidental classes without mutating their saved volumes or app-specific alert mappings. Content audio remains separately app-controlled.

The schema-v3 fields introduced by the visual-theme/audio-policy split predate this terminology. During this pre-1.0 transition, `audio.interaction_gain_percent` backs UI volume, `audio.notification_gain_percent` backs Event volume, and `notifications.volume` backs Alert volume. The public audio/event APIs use semantic class names so a later schema cleanup can rename persistence keys without making applications depend on those historical names.

## Application audio policy

The app policy/API layer answers whether an application natively permits each semantic audio class. Applications should emit semantic events or request alert/content presentation; they should not bypass global policy by choosing a raw gain themselves.

Application menus may provide convenient access to a global volume when the user is already configuring related behavior. Such a control changes the same global category volume rather than creating an app-local copy. App-local settings should be reserved for real semantic differences, such as whether ordinary WiFi/BLE events beep, whether an alert category is enabled, or which Morse character represents that alert.

## Alert audio mappings

Global Alert presentation remains **Off**, **Tone**, or **Morse**, with global Alert volume. Detecting applications continue to own their alert categories and enablement.

Each alert-level audio/Morse mapping also has an **Off** choice. Off suppresses audible presentation for that alert category regardless of whether the global presentation mode is Tone or Morse; the semantic event itself still exists for logging and other non-audio consumers.

The per-alert Off choice uses the supported blank mapping sentinel. Existing mappings retain their current/default characters unless the user explicitly selects Off.

## Maintenance rule

When adding a new application setting, first classify it into the canonical categories above and follow the established interaction model. A new app should not establish its own ordering, sound taxonomy, or toggle workflow merely because its menu is implemented separately.
