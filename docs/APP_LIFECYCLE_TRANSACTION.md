# Transactional application exit

RFScout application changes use an explicit transactional exit boundary so dynamic application memory is never reclaimed while a worker, callback, timer, DMA completion path, or peripheral operation can still reference it.

This document complements [APP_LIFECYCLE.md](APP_LIFECYCLE.md), which describes the broader Enter / Settle / Activate / Quiesce model. The implementation of the transactional exit coordinator lives in `app_lifecycle.c/.h`.

## Exit sequence

A transition from one application to another is divided into four stages:

1. **Prepare** — request bounded quiescence from the source application's workers and peripherals. Any work performed here must be reversible or have an explicit reactivation path.
2. **Reachability gate** — verify that the source application no longer has an execution path that can access app-owned dynamic state. Failure here aborts the transition and reactivates the source application.
3. **Commit** — perform irreversible application-specific exit cleanup only after the final fallible safety gate has passed.
4. **Reclaim** — release every managed application/operation allocation owned by the source application and verify that its managed allocation count is zero.

Only the first two stages may abort normally. Once Commit begins, the source application is considered exited and the destination must be entered even if a post-reclamation accounting invariant reports a defect.

## Aborted transitions

A failed Prepare or Reachability gate does not reclaim managed memory. The lifecycle coordinator invokes the source application's reactivation hook before returning the aborted result. This is important for partial shutdown cases: a transition must not leave the currently selected application visible but silently inactive.

Radio stop implementations that already restore their run request on timeout remain compatible with this model. Wi-Fi/BLE reactivation must likewise restore any collector that was active before a partial stop attempt.

## Managed-memory boundary

`app_lifecycle_transition()` owns the call to `memory_manager_release_app()`. Application code must not independently bulk-release its app-owned memory during Prepare or Reachability checks.

After reclamation, `memory_manager_app_allocation_count()` must return zero for the exited application. A non-zero result is recorded as `RFSCOUT_APP_TRANSITION_COMMITTED_MEMORY_INVARIANT`. This is a committed transition with a diagnostic failure, not a reason to attempt rollback after memory has already been released.

## Diagnostics

The coordinator retains the most recent transition result, including:

- source and destination application IDs;
- monotonically increasing transition sequence;
- outcome and ESP-IDF error;
- reclaimed managed payload bytes;
- remaining managed allocation count;
- elapsed transition time.

Each result is also logged with free and largest-block PSRAM values. These diagnostics are intended to support the v0.4.0 stress tests for rapid application rotation, failed quiesce/reachability paths, and memory-baseline stability.

## Initialization and PSRAM

`app_lifecycle_init()` initializes the managed-memory subsystem. Because managed RFScout application storage requires usable PSRAM, lifecycle initialization propagates the unsupported-target error rather than silently degrading managed allocations into internal SRAM.

## Application migration

The lifecycle coordinator establishes the safety/reclamation boundary before individual applications move retained state into managed PSRAM. Subsequent v0.4.0 work can therefore migrate WiFi/BLE, CC1101, nRF24, Recorder, and Thermal state incrementally while using one common exit invariant.
