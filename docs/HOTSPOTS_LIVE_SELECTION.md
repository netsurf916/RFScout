# Hotspots Live list selection consistency

The Live and History lists reorder as sightings, signal strengths, session
expiry and network characterization change. A numeric cursor index is not a
stable identity; navigation and selection must be reconciled to an associated
BSSID (or Saved profile ID) **after each model rebuild, before processing the
next input event**.

## Identity rules

- Network menus bind selection to a BSSID from the group, not to the group's
  previous numeric row. Both render and input paths call
  `resolve_menu_selection()` after the current model is built.
- Member menus bind selection to their BSSID; both render and input paths call
  `resolve_member_selection()`.
- When the selected group/profile disappears, return selection to **Back**,
  rather than acting on the same numeric row after it was reassigned.
- When the selected member disappears or moves outside the current group,
  select **Back**. No event is redirected to a different BSSID.
- Evidence-based SSID splitting and group ordering are not changed by this PR.

This addresses a **list-index race**, which is distinct from rotary
mechanical bounce, illegal quadrature states, and the separate screen-wake
regressions. No GPIO filtering, interrupt timing changes, extra SD trace
logging, or rotary decoder changes are included.

## Device verification

1. Enter Hotspots Live while the list grows and RSSI changes, highlight an
   existing group, wait for group reordering, then rotate once. The cursor
   should move one row relative to the *same group*.
2. Highlight a BSSID in a group, wait for reordering, and rotate once or
   press Select. It should continue to operate on that BSSID.
3. Let selected items expire or regroup; no stale Select should open a
   different group/member. The Back row becomes selected.
4. Repeat in History and Saved, including edits that remove a selected
   saved profile, and in steady menus where no list updates occur.
5. If jitter remains in static menus after this build, investigate encoder
   electrical/mechanical filtering separately without hiding this fix.
