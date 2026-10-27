# Rotary input decoder and wake follow-up

## Summary

PR [#141](https://github.com/netsurf916/RFScout/pull/141) restores the original
T-Embed rotary quadrature decoding behavior after navigation became jumpy and
occasionally reversed following [#138](https://github.com/netsurf916/RFScout/pull/138).
The owner reports that #141 is **substantially better** on the board, but has
not declared the remaining behavior fully verified. No changes to the common
wake, input-lock or application-lock policies are included.

The independent camera-service work in PR #140 is outside this change.

## Production behavior in PR #141

- GPIO4/GPIO5 still use the existing interrupts on both edges and internal
  pull-ups; hardware glitch filtering is **not** enabled.
- The decoder uses the original 16-entry Gray-code lookup table, shared with
  the earlier RFScout implementation and LilyGO factory firmware.
- Repeated and two-bit diagonal A/B changes contribute zero to the signed
  transition accumulator. Diagonal jumps increment an in-memory counter but
  **do not** reset partial movement or force synchronization.
- At either `00` or `11` latch, two signed transitions count as a step,
  matching LilyGO's `RotaryEncoder::LatchMode::TWO03` calibration.
- The shared input API coalesces all pending movement to one net `-1/0/+1`
  direction per consumption. Opposite movement can cancel; it does not
  replay every detent delayed by an application stall.
- `input_consume_wake()` and input lock still clear pending and partial
  decoder movement. The 200 ms display-wake guard is unchanged.

Host tests cover both directions, bounce, duplicate states, skipped diagonal
transitions, reversal cancellation, startup between latches, wake/lock clears,
and coalescing over a delayed input poll.

## Diagnostic logging removed

The earlier diagnostic build `candidate-283cc3c1474b` emitted best-effort
`rotary_edge` and `input_trace` SD JSONL records. A capture in
`rfscout-03.log` showed rapid illegal A/B transitions and apparent navigation
reversals, but its bounded ring overflowed substantially (31,746 dropped
samples), and the logging itself could perturb input-loop timing.

**No ISR trace ring, extra SD record types, trace enable macro, or temporary
trace calls remain in PR #141's effective code.** Normal existing application
and system logging is unaffected. Earlier diagnostic commits remain visible
in the PR's Git history; they are not part of the resulting firmware tree.

## Screen-wake issues not fixed by the decoder restoration

The main loop calls `input_poll()`, `display_idle_poll()`, service polling,
and then `app_manager_poll()`. The display checks physical pending movement
before the application manager consumes semantic input.

1. **Locked wake may be lost.** If a detent is decoded after
   `display_idle_poll()` checks input but before `app_manager_poll()`
   calls `input_get()`, the locked semantic input consumer can clear that
   movement before the next display wake check. This is a source-level race
   hypothesis; it has not been confirmed in a recent focused hardware test.
2. **A wake gesture may leak into app navigation.** A slow detent can span
   multiple input polls. The existing wake guard expires 200 ms after wake,
   rather than after the last rotary edge. A later step may therefore be
   delivered after the guard. The exact causal sequence is unconfirmed.

Address these at the shared input/display boundary only after a focused
regression test; changing the decoder alone must not be presented as proof
that wake behavior has been corrected.

## Hardware verification before owner-directed merge

| Display | Input lock | Gesture | Expected result |
| --- | --- | --- | --- |
| Active | Unlocked | Slow single detents | Predictable direction and one intended action per detent |
| Active | Unlocked | Fast turns and reversals | No unexpected jumps, reversal, or delayed backlog |
| Active | Unlocked | App-owned menus | Correct local navigation and Select behavior |
| Off/dimmed | Unlocked | One detent | Wake display without switching apps |
| Off/dimmed | Locked | One detent | Wake display without delivering semantic navigation |
| Off/dimmed | Unlocked | Slow rotation across 200 ms | No leftover wake-gesture navigation |
| Active | Locked | Rotation | No semantic navigation |
| Off/dimmed | Locked | Center Hold / physical power key | Existing unlock / power behavior preserved |

The navigation improvement is owner-confirmed; the complete wake matrix above
is still a follow-up checklist, not a claim of passing hardware verification.
