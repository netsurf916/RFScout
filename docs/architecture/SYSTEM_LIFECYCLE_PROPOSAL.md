# Proposed framework startup and system power lifecycle

> **Status: design proposal, not an implemented contract.**
>
> This document records the intended lifecycle semantics for review before firmware changes.
> The [platform architecture](../ARCHITECTURE.md) remains authoritative for current behavior.
> No CASIC low-power support or generalized lifecycle coordinator is implied to exist today.

## Goals and boundaries

The framework owns scheduling and ordering of lifecycle operations. Each service owns
its hardware-specific implementation, and applications declare capabilities and needs rather
than directly orchestrating global peripheral shutdown.

This proposal separates three related but distinct lifetimes:

1. **Platform providers**: initialized by the framework according to the selected board profile.
2. **Application sessions**: selected applications and their required services, with existing
   transactional activation/deactivation behavior and resource ownership.
3. **System power**: whole-device transitions (sleep, physical power-off, restart), independent
   of which application is currently selected.

A screen-off or dimmed display is **not** a system-sleep transition: recording, reception,
logging, and other work may continue while the display is dark.

## Startup semantics

1. Recognize a legal board profile using the existing minimal/passive detection path. Do not
   touch unsupported peripherals while identifying the board.
2. Initialize shared buses and eligible providers in declared dependency order. `init()`
   establishes lightweight capability and internal state; it must not implicitly begin long-lived
   scanning, acquisition, or network connections.
3. Restore durable configuration and the limited RTC-backed session envelope where supported.
   Do not assume ordinary RAM or prior application tasks survive deep sleep.
4. Select the application and its services through the existing application-session coordinator.
   Call `start()` to acquire active resources, and `stop()` to release them. Starting after
   stopping is a supported lifecycle, not a special `resume()` entry point.
5. A failed initialization/start should be reported and rolled back within the existing
   ownership boundaries; never silently leave a provider partially active.

The application session continues to own app-scoped callbacks, memory, and media handles.
System-owned services (for example GPS location/time) should not be destroyed merely because
the foreground application changes.

## System power targets

| Target | Meaning | Continuity expectation |
| --- | --- | --- |
| Display idle | Presentation only; hardware acquisition can continue | Application and services stay active |
| Deep sleep | Whole-device terminal transition into ESP32 deep sleep | Ordinary tasks/RAM do not survive; boot restores only persisted state |
| Physical power-off | Request hardware power removal | No in-process recovery unless the request fails |
| Restart | Controlled reboot after required quiescence | New boot, not a continuation |
| Future light sleep | Potentially resumable SoC sleep | **Not yet designed or implemented**; requires a separate contract |

`power.c` currently prepares configuration and explicitly quiesces Recorder, breadcrumbs,
and log writers before handling the display and entering sleep or requesting power-off.
The future coordinator should replace service-specific knowledge in the power manager,
without weakening those protections or Recorder's ability to veto an unsafe transition.

## Proposed coordinated transition

The power manager requests a **target**; a single framework coordinator performs the transition.
The coordinator tracks phase, target, deadlines, and the services already affected.

1. **Prepare**: ask participating services whether the requested target is safe. An active
   recorder may veto. This phase should avoid irreversible operations.
2. **Quiesce**: in dependency order, stop acquisition and asynchronous callbacks, drain and
   flush writers, persist required state, and make peripherals safe. Return a completion result,
   not merely a fire-and-forget notification.
3. **Commit**: store supported session resume metadata, place presentation hardware in its
   required state, configure wake sources, and invoke the board-specific deep-sleep,
   power-off, or reboot operation.
4. **Abort / rollback**: if preparation or quiescence fails, restore any affected components
   in reverse dependency order where possible; report partial failure and do not proceed to
   sleep with unflushed data. A failed physical power-off must also restore interactive
   display and storage behavior.

A possible *illustrative*, not committed, interface is:

```c
typedef enum {
    SYSTEM_TARGET_DEEP_SLEEP,
    SYSTEM_TARGET_POWER_OFF,
    SYSTEM_TARGET_RESTART
} system_target_t;

typedef struct {
    esp_err_t (*prepare)(system_target_t target);
    esp_err_t (*quiesce)(system_target_t target);
    esp_err_t (*abort)(system_target_t target);
} system_lifecycle_ops_t;
```

Precise ownership, callback signature, registration, asynchronous completion,
timeout policy, error propagation and dependency declarations must be settled
before implementation. Do **not** add a second application lifecycle or treat
the example above as an ABI.

### Why not only use system events?

State notifications such as *sleep requested*, *shutting down*, or *transition aborted*
are useful for diagnostics and observability. But broadcasts cannot guarantee that
storage was flushed or peripheral activity stopped. Operations requiring confirmation
must run through the coordinator; notifications may describe their outcome.

## CASIC GPS candidate behavior

The current GPS service uses NMEA for fixes and CASIC commands for receiver policy,
including baud and navigation update-rate configuration. Potential CASIC-controlled
navigation stop/start belongs to the GPS service implementation, **not** `power.c`.

Before introducing receiver control:

- Verify the HT1818Z3G5L's exact supported command, response/ACK and stop/wake semantics
  from module documentation and on-device tests. Do not conflate a navigation stop command
  with a verified low-current sleep state.
- Determine whether wake is UART-command-driven, requires a reset/power cycle, or depends
  on receiver wiring. The installed configuration has no separate power-control line.
- Measure receiver current in active, stopped, and restarted states. Document whether
  backup data and fast reacquisition survive each transition.
- Preserve GPS/NMEA presence detection, GPS-versus-nRF24 UART ownership/arbitration,
  the baud-probing sequence and valid-fix requirements.
- On failed or unsupported CASIC commands, maintain safe generic NMEA behavior; never
  block system shutdown indefinitely waiting for GPS.
- On ordinary app switches, retain system GPS service ownership unless the framework's
  service policy specifically requires stopping it.

Time-pulse LED experiments belong to a separate GPS configuration change and should
not be interpreted as evidence of power-saving or guaranteed LED behavior.

## Implementation strategy (future work)

1. Agree on these semantics and the relationship to existing `app_session` transitions.
2. Inventory every system-owned service and existing shutdown-veto/flush contract.
3. Introduce coordinator tests for ordering, veto, timeout, failure and rollback, including
   failed physical power-off, unavailable SD media, and Recorder activity.
4. Migrate existing `power.c` cleanup into registered participants without changing
   display-only idle or hardware-capability gating.
5. Add an optional GPS participant; only then introduce validated CASIC stop/start commands.
6. Treat resumable light sleep as a separate design decision; avoid introducing a
   general-purpose `resume()` lifecycle method prematurely.

## Open design questions

- Should services register static dependencies, or should board profiles provide the order?
- How are long-running asynchronous drains signaled and cancelled within a bounded deadline?
- Which service failures should veto sleep, and which should only be logged?
- Is recovery from a failed quiesce always possible, or must a degraded-state policy exist?
- How should a future light-sleep wake map onto `start()` versus an explicit reversible suspend?
- What happens when the GPS is electrically powered while the ESP32 enters deep sleep?
