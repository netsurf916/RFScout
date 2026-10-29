# Breadcrumb cadence and retention — implementation contract

**State: implemented and merged on PR #139, following merged PR #137's UTC-dated JSONL writer.** The policy below describes the current code contract, not an unmerged draft or a new prerequisite. Hardware cadence and retention validation remains part of the v0.4.0 release gate.

## Goals

Preserve frequent vehicle breadcrumbs without high-volume stationary writes, provide a fresh-position confirmation once per minute when stationary, and offer clear time-based retention without surprising data loss. Reuse the current GPS speed filter, breadcrumb worker, media collection and configuration menus; no new scheduling subsystem.

## Velocity-dependent capture

Use a read-only getter for the GPS service's existing filtered speed (m/s) with an independent freshness check, **not** a new filter or a getter dependent on CASIC rate-control support. The existing GPS smoothing uses elapsed-time-adjusted decay, equivalent to approximately 90% previous / 10% current at 1 Hz. If filtered speed is stale, use a fresh raw speed; if neither exists, use zero speed.

Interpolate linearly between these knots and clamp at the endpoints (a deliberately nonlinear overall shape):

| Speed, m/s | Minimum elapsed time since last queued record |
| ---: | ---: |
| 0 | 60 s |
| 0.5 | 45 s |
| 1 | 30 s |
| 2 | 12 s |
| 3 | 6 s |
| 5 | 3 s |
| 7 | 2 s |
| 10 or faster | 1 s |

A breadcrumb is eligible only when **all** of the following hold:

1. Current GPS position and GPS UTC are valid and fresh; never invent a stationary heartbeat from stale coordinates.
2. The monotonic elapsed time since the last *queued* record exceeds the interpolated minimum, and the UTC second differs from the last queued UTC second.
3. Either the **existing shared movement trigger** has been crossed relative to the last *queued* position, **or** 60 seconds have elapsed since the last queued record (stationary heartbeat).

The heartbeat intentionally bypasses only the movement gate. The 1-record-per-UTC-second maximum remains absolute, even at 10+ m/s. Treat speed as a sampling minimum, not a guarantee: a larger movement threshold can still defer vehicle records. Advance the movement gate and last-queued times only after successful enqueue; preserve the bounded queue, dedicated SD writer and existing failure-disable rules.

## Per-day storage and retention

Use the parent PR's append-only `system/breadcrumbs/breadcrumbs-YYYY-MM-DD.jsonl` files; UTC **from the record** determines the filename. Retention choices, editable through the **existing** Home and GPS settings rows:

| Label | Duration |
| --- | --- |
| 1 day | 24 hours |
| 1 week | 7 days |
| 1 month | 30 days |
| Forever | Do not prune automatically |

Default to **Forever**, including upgrades; never silently remove previously kept data. Preserve the existing breadcrumb enabled/disabled preference across migration. The obsolete persisted segment-count value must not be interpreted as a duration. Replace the parent PR's informational `Breadcrumb archive: All` row with editable `Breadcrumb retention` in both menus.

For a finite duration, delete a dated file only when **the entire UTC day covered by its filename falls before the cutoff** (trusted UTC now minus the duration). This intentionally allows less-than-one extra calendar day to remain. Parse filenames strictly; ignore legacy numbered or CSV files, unknown entries, future dates and files in other collections. Do not prune today's open file. With missing trusted UTC or SD media, skip cleanup and try again later.

Use existing `media_list()`/`media_remove()` through the breadcrumb worker, in bounded increments. Trigger pruning during safe worker opportunities after boot/recovery, UTC day rollover and retention edits; never run it in GPS polling, radio callbacks or the menu event handler. Do not add a dedicated cleanup task.

## Test and hardware acceptance

- Cadence knots and interpolation, stale speed fallback, 60-second stationary heartbeat, movement crossings, UTC-second cap, full queue behavior.
- UTC date rollover, leap day, cutoff boundary, retained partial day, legacy-file safety, unavailable media/clock and no unintended deletes on upgrade.
- The Home and GPS menus agree on retention; previously persisted On/Off survives.
- Hardware: parked for several minutes, walk, cycle and drive up to/exceeding 10 m/s; verify both frequency and coordinate validity; inspect SD writes and queue drops.

**Release status:** Both #137 and #139 have merged. Validate on the actual v0.4.0 candidate hardware; this document does not by itself record those tests as passed.
