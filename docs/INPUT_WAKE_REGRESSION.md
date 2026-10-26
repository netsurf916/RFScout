# Rotary wake and input-lock regression investigation

**Status:** Investigation baseline only; no behavior or firmware changes in this initial PR. Base: `main` at `e29b2c4d1f8a`. Related: [merged PR #138](https://github.com/netsurf916/RFScout/pull/138), which introduced the rotary quadrature decoder and coalesced navigation.

## Reported device symptoms (2026-10-26)

1. **Input unlocked:** A **single physical encoder detent** wakes a dimmed/off display **and switches to another application**. Expected: the wake gesture is swallowed; the current application does not change.
2. **Input locked:** A rotary detent **sometimes fails to wake the dimmed/off display**. Expected: input lock suppresses UI actions, **not screen-wake activity**.
3. Existing behavior to preserve: the dedicated power key and center-button Hold unlock path work when input is locked. Application lock still allows local app input and blocks *unhandled* global app navigation.

These are device observations, not host-reproduced failures yet. Both symptoms belong to the shared input/display-idle path; do not patch an individual app or PR #140.

## Verified current execution order

In `main/rfscout.c`, one main-loop iteration calls:

```text
input_poll()
display_idle_poll()                  // checks input_activity_pending()
GPS / power / services / housekeeping
app_manager_poll()                   // input_get(); display_idle_input_should_wake()
display_present()
10 ms sleep (plus variable loop work)
```

Relevant contracts:

- `input.c:300-317` `input_activity_pending()` tests `rotary_decoder_pending()` under `navigation_lock`, **independent of `locked`**.
- `input.c:96-106` `consume_navigation_delta()` returns zero **and calls `rotary_decoder_clear_pending()` when locked**, as part of `input_get()` (`input.c:340-374`).
- `input.c:319-338` `input_consume_wake()` clears pending/partial movement and button events.
- `display_idle.c:297-317` `display_idle_poll()` wakes when activity is pending and the display is not active, consumes wake input, and starts a **200 ms** guard.
- `display_idle.c:274-295` `display_idle_input_should_wake()` also catches an event delivered after the idle poll; while the guard is running, input is discarded. The guard is a **fixed interval from wake**, not from the latest physical encoder edge.
- `app_manager.c:388-435` calls `input_get()` before the display-idle guard check; an unhandled navigation event after the guard can invoke `switch_app_for_navigation()`.
- `rotary_decoder.c` recognizes half-steps at opposite 00/11 latches; the existing host test intentionally demonstrates **two navigation half-steps for one full Gray-code cycle**, and coalescing only occurs on each consumption. It does not test wake-service integration.

## Hypothesis A — lost locked wake on interleaving (high confidence as a possible race)

A complete detent arrives **after** `display_idle_poll()` checked for activity but **before** `app_manager_poll()`:

1. `display_idle_poll()` sees no activity and stays off.
2. The encoder ISR adds a pending detent.
3. `app_manager_poll()` calls `input_get()`.
4. When `locked` is true, `consume_navigation_delta()` discards the pending detent, returns no input, and therefore never calls `display_idle_input_should_wake()`.
5. On the next loop the wake detector finds no pending movement. **The entire physical detent was lost as a wake request.**

This ordering is possible from source inspection, not measured as the exact hardware race yet. Input lock's policy is currently conflating *discard semantic navigation* with *erase the only pending wake evidence*.

## Hypothesis B — unlocked single-detent app switch (requires timestamp validation)

If the display is genuinely dimmed/off when `app_manager_poll()` processes a navigation event, `display_idle_input_should_wake()` returns true; that **single semantic event cannot both wake and switch an app** through the straightforward path.

Two plausible alternatives, requiring discrimination:

- One physical detent produces multiple half-steps spread across main-loop iterations (the decoder's 00/11 calibration can create two navigation events per full A/B cycle). The first wakes/clears; a later decoded half-step becomes input after the **fixed 200 ms guard** has expired, perhaps due to slow rotation or scheduler/display stalls.
- The LCD is made active by an earlier activity/attention/device-wake path, and the later decoder event is allowed through because the active-state guard is no longer running.

The existing guard was introduced on October 12 in commit `bd6fdb1` to handle trailing detents; PR #138 changed the encoder decoder on October 25 without changing that guard. Correlation with #138 is compelling, but it is **not proven** to be the only cause.

## Minimum trace and test plan — before choosing a fix

Instrument a **bounded, opt-in, low-overhead** input/display trace (prefer counters and timestamps over logging in GPIO ISRs). Each event should record monotonic microseconds and, where relevant:

- Rotary A/B transition / decoded half-step, decoder pending count, invalid transitions and whether this was an input-lock discard.
- Display state at the start of idle poll, wake reason, wake-guard deadline and its most recent refresh.
- `input_activity_pending`, `input_get` result, `input_consume_wake`, and app navigation dispatch.
- Whether app lock and input lock are active, and whether a system/attention wake intervened.
- A test event/trace should distinguish physical detent boundaries from complete decoded half-steps; do not infer them solely from pending net direction.

Repro matrix (with normal and deliberately delayed loop interleavings):

| Display at start | Input lock | Gesture | Required result |
| --- | --- | --- | --- |
| Off | Unlocked | One slow physical detent | Wake, **zero** app transitions |
| Off | Locked | One slow physical detent | Wake, **zero** semantic input or app transitions |
| Dimmed | Unlocked | One detent | Restore brightness, no app transition |
| Dimmed | Locked | One detent | Restore brightness, no semantic input |
| Off | Unlocked | Continuous rotation spanning >200 ms | Consume the **wake gesture** entirely; no delayed app switch |
| Active after a completed wake gesture | Unlocked | A new separate detent | Exactly one intended semantic navigation action |
| Active | Locked | Detent | No semantic navigation; maintain locked state |
| Off | App lock only | One detent | Wake, no app switch or local action from wake gesture |
| Off | Input lock | Center Hold / power key | Existing unlock / physical power policy preserved |

Specifically force the detent to complete **between** `display_idle_poll()` and `app_manager_poll()` to expose Hypothesis A. Force a two-half-step input to cross the guard deadline to test Hypothesis B. Do not rely only on decoder unit tests; add a service-integration regression capable of observing wake and app dispatch.

## Proposed design questions (not implemented)

- **Wake evidence must survive ordinary semantic input suppression.** Consider a separate activity-generation counter or wake-pending latch that can only be acknowledged by the display-idle policy, while the locked semantic consumer may still discard navigation. Ensure latched activity does not falsely wake a later idle cycle.
- **Suppress an entire wake gesture.** Consider ending suppression after a quiet interval since the *last physical rotary transition* rather than a one-shot guard after the first wake, but distinguish mechanical bounce from deliberate new turns and preserve prompt post-wake response.
- Maintain a single shared policy for all applications and for both manual screen-off and idle dim/off. Avoid simply lengthening the 200 ms timeout without a regression covering both behaviors.

**Scope rule:** This PR starts with documentation only. Do not modify PR #140, merge anything, or change firmware until the evidence and proposed behavior are discussed with the owner.
