# Rotary input and display-wake regression reference

## Current implementation (merged PRs #145 and #149)

RFScout samples the T-Embed GPIO4/GPIO5 rotary phases with a dedicated
**1000-microsecond ESP timer callback**, not A/B GPIO edge interrupts. The
LilyGO `TWO03` 16-entry Gray-code decoder publishes only the `00`/`11`
latches with the established semantic direction inversion. Navigation
coalesces pending movement; incomplete wake/lock gestures are suppressed
until the next latch. The **center button** still has its GPIO interrupt.
The 200-ms display-wake guard is independent of the 1-ms sampling timer.

Merged PR #149 also separates all 13 application runtimes' state handling
from framework-owned rendering: immediate invalidations for input/menu changes,
background invalidations coalesced to at most 5 Hz, and a single shared
display presentation path. Thermal acquires sensor frames during service
polling rather than inside its render callback. None of these changes makes
the ST7789's whole-frame SPI transport or wireless shutdown asynchronous.

Owner testing of the #149 candidate reported substantially improved rotary
behavior and a responsive UI. This is **positive field feedback**, not a
recorded pass for the complete v0.4.0 wake/lock and app-regression matrix.

## Historical background: PRs #138, #141 and #145

PR #141 restored the earlier rotary decoder after #138 introduced jitter.
PR #145 then adopted LilyGO's `TWO03` latch behavior, using
`A | (B << 1)`, a cumulative signed raw position, and publication only
on `00`/`11`. It corrected semantic direction and suppressed the remainder
of partially consumed wake/lock detents. The fixed GPIO filter and PCNT
experiments were abandoned. **At the time of PR #145** quadrature input
used edge interrupts; **PR #149 superseded only the sampling mechanism**,
moving it to a periodic 1-ms timer while retaining the decoder semantics.

Earlier debug builds generated `rotary_edge`/`input_trace` logs that could
themselves perturb timing. The per-edge trace ring was removed. The separate
bounded `ui_latency` and `connectivity_probe_timing` records introduced
in #149 remain in the merged firmware, so release cleanup should evaluate
their operational cost before claiming diagnostics were removed.

Reference: [LilyGO factory example](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101/blob/master/examples/encode_test/encode_test.ino)
and [RotaryEncoder library](https://github.com/mathertel/RotaryEncoder/blob/master/src/RotaryEncoder.cpp).

## Open wake/lock hardware questions

The code checks for pending physical input during display-idle processing,
then consumes semantic input during app-manager polling. A movement that
arrives between those operations may be cleared by the input-lock consumer
before the next wake check. Similarly, very slow gestures crossing the
200-ms wake guard could expose a later detent as navigation. These remain
**source-level hypotheses**, not demonstrated current failures. Validate
on hardware rather than asserting they are fixed or present.

## v0.4.0 hardware acceptance (not yet recorded complete)

| Display | Input lock | Gesture | Expected result |
| --- | --- | --- | --- |
| Active | Unlocked | Slow single detents | Predictable direction and one intended action per detent |
| Active | Unlocked | Fast turns/reversals | No unexpected jumps, reversal, or delayed backlog |
| Active | Unlocked | App-owned/nested menus | Correct local navigation, Select and App Lock redraw |
| Off/dimmed | Unlocked | One detent | Wake display without switching apps |
| Off/dimmed | Locked | One detent | Wake display without delivering semantic navigation |
| Off/dimmed | Unlocked | Slow rotation across 200 ms | No leftover wake-gesture navigation |
| Active | Locked | Rotation | No semantic navigation |
| Off/dimmed | Locked | Center Hold / physical power key | Existing unlock and power behavior preserved |

Also exercise all 13 app runtimes, the Home/Network/OTA settings hierarchy,
System Events clock, modal overlays, live-data updates and transitions during
scanning and TLS. Record the exact candidate SHA, test setup and explicit
Pass/Fail/Untested results in [release validation](RELEASE_VALIDATION.md).
