# Application lifecycle and resources

RFScout treats every active application as a temporary **resource session**. The active app may use available application PSRAM and platform services; closing the app safely stops those services and releases the entire app-owned memory set. App implementations should not need allocator owner IDs, callback counters, or custom memory-reachability protocols.

## Application-facing contract

The intended application model is deliberately small:

1. **Enter** — prepare inexpensive UI/runtime state.
2. **Settle** — optionally defer hardware activation so rapid navigation stays responsive.
3. **Activate** — use platform services and app-session memory as needed.
4. **Close** — perform only semantic cleanup the app itself understands. Platform services and app memory are closed/reclaimed by the framework.

Large/growable app data should use `app_session_calloc()`, `app_session_resize()`, and `app_session_free()`. The session layer binds those allocations to the active app automatically. The lower-level `memory_manager` remains an internal ownership/accounting mechanism and should not normally appear in application code.

Every active app has a session, including apps that currently need no dynamic PSRAM or asynchronous platform service. This gives future applications the same resource model without requiring a new lifecycle API.

## Platform-service contract

Platform services register their start/stop behavior with the current session. A successful service stop means:

- no new callback/work that can reach app state will be admitted;
- already-admitted callbacks/workers have drained;
- shared hardware is left in a safe inactive state; and
- app-owned memory can no longer be reached through that service.

The app therefore does not implement service-specific reachability tests. The platform owns that proof.

Current session-managed services are:

- **WiFi/BLE** — Wi-Fi promiscuous observation and BLE scanning;
- **CC1101** — the receive-only sweep worker and RF path;
- **nRF24** — the receive-only activity worker and radio state; and
- **Recorder** — Recorder worker/channel shutdown and reachability proof.

Home/Device Settings, System Events, and Thermal currently have no asynchronous service requiring registered close behavior. They still use the same resource-session lifetime. Thermal acquisition is synchronous from the app poll/render path today; if a future Thermal implementation adds a worker, DMA path, or callback, that mechanism belongs behind a registered platform service rather than a new app-specific lifecycle API.

## Transactional close

`app_lifecycle.c` keeps a transactional implementation underneath the simple app API:

1. ensure the source session exists;
2. ask the session to close all registered platform services;
3. abort without reclaiming memory if any service cannot close safely;
4. run optional app-semantic Prepare work when needed;
5. verify the session plus any remaining specialized semantic reachability condition;
6. commit irreversible app cleanup;
7. bulk-reclaim all source-app managed memory;
8. verify zero source allocations remain; and
9. create the destination session before the destination app begins using app resources.

If a transition aborts, `app_session_abort_close()` restarts only services that were active and whose stop operation actually succeeded before a later failure. A service whose stop call itself fails is not generically restarted because that stop implementation may already have restored its active state. App-specific rollback is reserved for semantic state that cannot be inferred by the platform—for example restoring the exact Recorder operation the user had active. Memory stays intact until close succeeds.

This keeps the Prepare/Reachability/Commit/Reclaim machinery as platform fault-containment rather than an API each new app must implement.

## Memory policy

App-session PSRAM is for retained observations, histories, captures, and other meaningfully sized growable state. Small transient objects, task stacks, DMA buffers, driver-required internal allocations, and hardware-constrained buffers continue to use the ESP-IDF memory class required by their platform service.

There is deliberately no arbitrary per-app item quota. Collections grow according to available resources and their retention semantics. UI code should bound presentation work, not retained data. When memory pressure occurs, prefer correctness/current data, release reconstructable scratch, evict stale/low-value history where semantically safe, surface pressure, and continue in a bounded state.

PSRAM is a supported-platform requirement for this model. RFScout does not silently convert internal SRAM into a reduced-capacity substitute for large app-session storage.

## WiFi/BLE migration

WiFi/BLE is the first data-heavy app moved to session memory. The normalized observation store no longer reserves 1000 entries at boot and no longer has a 1000-item retention ceiling. It grows geometrically in app-session PSRAM.

The summary UI no longer keeps another fixed 1000-entry observation/brand mirror. It requests aggregate counts and only stores the rows that physically fit on the display. Growable summary scratch is also session-owned.

The store uses the app-session generation to invalidate cached pointers after a prior WiFi/BLE session has been reclaimed. It does not inspect lower-level allocator ownership metadata.

## Recorder semantic cleanup

Recorder is the main example of the distinction between **resource safety** and **semantic cleanup**. The session layer proves that Recorder workers/channels are no longer able to reach app memory. Recorder/app logic may still need to finalize a WAV, preserve catalog state, or restore the exact recording/monitor/playback operation after an aborted transition. Those are semantic responsibilities, not reasons to expose worker reachability or allocator ownership to every application.

## Availability and dependency loss

Application availability remains owned by the application framework using `none`, `startup`, or `hotplug` probe policies. Probing is separate from activation and must not leave continuous acquisition running.

If an active app loses a required dependency, RFScout returns transactionally to Home. The same session-close contract applies. If safe close cannot complete, the source app and its memory are preserved rather than reclaiming potentially reachable state.

## UI interaction policy

Menu behavior follows semantic item type:

- **Action** — center executes immediately.
- **Toggle** — center changes On/Off immediately.
- **Choice** — center cycles a small finite set when direct cycling is unambiguous.
- **Value** — center enters edit mode, rotation adjusts, center exits.
- **Submenu** — center enters the child view.

Simple On/Off settings should not require center, rotate, center.

## Validation

Release testing should rapidly rotate through every enabled application and exercise low memory, callback races, worker-stop timeouts, dependency loss, Recorder transitions, and failed-close rollback.

Successful app exit must leave zero managed allocations for the exited app. An aborted transition must preserve its app memory. Baseline free PSRAM and largest-free-block behavior should not decline monotonically across repeated app cycles.
