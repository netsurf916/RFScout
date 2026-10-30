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

## Merged PR #145: LilyGO TWO03 software-decoder implementation

Hardware feedback showed that neither the original #145 fixed GPIO filter nor
the subsequent PCNT quadrature decoder eliminated repeated back-and-forth
navigation (PCNT was somewhat better). The PCNT experiment has therefore been
removed before #145 merged. The shared encoder uses GPIO4/GPIO5 both-edge
interrupts and the pre-existing, coalesced semantic navigation API.

This revision explicitly follows the LilyGO T-Embed CC1101 factory encoder
example and Matthias Hertel's `RotaryEncoder::tick()` in `LatchMode::TWO03`:

- Encode inputs as `A | (B << 1)` (LilyGO's first GPIO4 signal is bit zero).
- On a *changed* sampled A/B state, apply the library's 16-entry Gray-code
  lookup table to a **cumulative**, signed raw position; do not zero that
  position after an accepted step. Duplicate states do nothing; illegal
  diagonal changes contribute zero but still update the previous state.
- Only at state `00` or `11` publish `raw_position >> 1`. The intermediate
  `01` and `10` states cannot themselves publish an input action.
- Keep LilyGO's raw transition counting unchanged, but **invert the semantic
  navigation direction** to restore the physical UI direction confirmed during
  on-device PR #145 testing. The factory library's raw position sign remains
  available in tests independently of RFScout's navigation sign.
- Preserve RFScout's independent input lock, wake-clearing and one-net-direction
  per application poll. Pending navigation reflects the published latch
  position, not a fractional edge. Wake/lock rebases the decoder and, if a
  clear occurs in intermediate state `01` or `10`, discards all transitions
  until the next `00` or `11` latch to prevent the wake gesture's last edge
  from leaking into navigation. The center button is unchanged.
- Host tests compare synthetic streams against an independent model of the
  reference `tick()` logic: clean turns, duplicate states, bounce, diagonal
  transitions, reversals, mid-latch startup, delayed poll coalescing, and
  mid-cycle wake/lock resets in both directions.

LilyGO's demonstration polls `encoder.tick()` with a roughly 1 ms loop delay.
RFScout still samples using *both-edge GPIO interrupts*. Therefore #145
aligns the **decoder state machine and latch rule**, not the sampling cadence.
The ISR can observe contact bounce that LilyGO's 1 ms polling may skip. This
change does not add speculative software debounce or SD logging. The earlier
PCNT-only Codex direction finding is no longer applicable.

Reference: [LilyGO factory example](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101/blob/master/examples/encode_test/encode_test.ino)
and [RotaryEncoder library](https://github.com/mathertel/RotaryEncoder/blob/master/src/RotaryEncoder.cpp).

**Hardware acceptance:** Confirm exactly one movement per physical detent in
both directions, especially at slow speeds. Test stopping between detents,
rocking gently around a latch, fast spins, immediate reversals, Home and
application menus, and the display-off/locked-input wake matrix below. Check
that clockwise and counterclockwise actions now match the device's expected
menu direction. The RFScout semantic sign is intentionally opposite the raw
LilyGO position sign. If bounce remains, separately
investigate time-qualified input sampling rather than changing detent counts.

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

## v0.4.0 hardware acceptance after merge (still open)

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

## PR #149 UI stabilization pilot (hardware validation pending)

PR #149 replaces GPIO4/5 quadrature edge interrupts with a dedicated periodic
ESP timer callback sampling both pins at **1000 microsecond intervals**. It keeps
the LilyGO TWO03 state machine, semantic inversion, coalescing, input lock and
button GPIO interrupt unchanged. Like LilyGO's demonstration, this is a
software sampling cadence, not a hardware contact-debounce guarantee. ESP timer
callback jitter under load and the existing wake matrix still need on-device tests.

The same PR introduces a framework-owned deferred render event and migrates
WiFi/BLE dashboard refreshing as the first pilot. The rest of the apps continue
their existing rendering paths until explicitly migrated. Do not describe the
whole UI as event-driven until that migration is complete.
