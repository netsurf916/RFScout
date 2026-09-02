# Audio policy and semantic sound classes

RFScout separates **what a sound means** from **how it is presented**. Applications emit semantic events or request application content audio; the shared audio policy decides whether that class is allowed and which global gain applies.

## Sound classes

RFScout defines four semantic audio classes:

| Class | Purpose | Global incidental volume |
| --- | --- | ---: |
| UI / device | Navigation, accepted actions, lock state, device interaction feedback | UI volume |
| Event | Lightweight informational activity, such as ordinary detection beeps | Event volume |
| Alert | Warnings/errors and configurable tone/Morse attention events | Alert volume |
| Content | Explicit application audio such as Recorder playback | App-owned |

The first three are device-wide incidental classes. Content is intentionally separate because an application that is explicitly playing or monitoring audio has different ownership and arbitration requirements.

## Event classification

`rfscout_event_audio_category()` is the canonical mapping for semantic events:

- navigation, activation, enter/back, enable/disable, and lock/unlock are **UI**;
- ordinary `RFSCOUT_EVENT_DETECTED` activity is **Event**;
- `RFSCOUT_EVENT_NOTIFICATION`, warning, and error semantics are **Alert**;
- theme/policy change events are silent control events.

Configurable `RFSCOUT_EVENT_NOTIFICATION` presentation remains handled by `alert.c`, where per-alert enablement and tone/Morse mappings are applied. Warning/error tones use the Alert class as well but do not require a configurable Morse descriptor.

## Device policy

`audio_policy` describes whether each semantic class is permitted. **Normal** permits UI, Event, Alert, and Content. **Quiet** suppresses UI, Event, and Alert while leaving explicitly requested Content available.

Quiet changes presentation only. It must not overwrite the user's saved volumes, per-app event switches, alert enablement, or Morse mappings.

## Application policy / SDK contract

`app_audio_policy` describes which classes make sense for each application. This is a capability/policy declaration, not a second set of volume controls. New application code should ask `app_audio_policy_allows()` or use the shared semantic event/alert APIs rather than choosing a raw output class itself.

The legacy `notifications_supported` and `notifications_enabled_default` fields remain distinct from the general Alert capability because an application may legitimately emit warning/error Alert-class audio without owning configurable detection-alert descriptors.

Global volume is intentionally shared across applications. An application menu may expose a convenient route to a global category volume, but changing it changes the same device-wide value. App-local settings should describe app semantics, such as whether ordinary detections beep or which Morse character represents an alert.

## Volume state and schema-v3 transition

PR #60 introduced independent audio policy and migration gains before the semantic class names were finalized. During the pre-1.0 transition this implementation maps those persisted values as follows:

- `audio.interaction_gain_percent` -> **UI volume**;
- `audio.notification_gain_percent` -> **Event volume**;
- `notifications.volume` -> **Alert volume**.

The runtime/API terminology is authoritative for new code. A later configuration-schema cleanup may rename these persisted keys; application code must not depend on the historical JSON names.

## Arbitration

UI and Event sounds are disposable feedback and use the feedback audio path. Alert sounds use notification-class output so they remain distinct from ordinary activity feedback. Intentional Content audio owns the speaker while active; disposable UI/Event/Alert sounds are dropped rather than queued behind content playback.
