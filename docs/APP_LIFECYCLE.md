# Application lifecycle and resources

RFScout treats every active application as a temporary **resource session**. The active app may use available application PSRAM and platform services; closing the app safely stops those services and releases the entire app-owned memory set. App implementations should not need allocator owner IDs, callback counters, service-stop bookkeeping, or custom memory-reachability protocols.

## Application-facing contract

The intended application model is deliberately small:

1. **Enter** — prepare inexpensive UI/runtime state.
2. **Settle** — optionally defer hardware activation so rapid navigation stays responsive.
3. **Activate** — use platform services and app-session memory as needed.
4. **Close** — preserve or finalize only user-visible semantic state the app itself understands. Platform services and app memory are closed/reclaimed by the framework.

Large/growable app data should use `app_session_calloc()`, `app_session_resize()`, and `app_session_free()`. The session layer binds those allocations to the active app automatically. The lower-level `memory_manager` remains internal ownership/accounting machinery and should not normally appear in application code.

Every active app has a session, including apps that currently need no dynamic PSRAM or asynchronous platform service. This gives future applications the same resource model without requiring a new lifecycle API.

## Platform-service contract

The platform internally associates asynchronous services with each app session. Applications use those services; they do not register raw start/stop callbacks themselves.

A successful platform-service stop means:

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

Home/Device Settings, System Events, and Thermal currently have no asynchronous service requiring session teardown. They still use the same resource-session lifetime. Thermal acquisition is synchronous from the app poll/render path today; if a future Thermal implementation adds a worker, DMA path, or callback, that mechanism belongs behind a platform service rather than a new app-specific lifecycle API.

## Transactional close

`app_lifecycle.c` keeps the fault-containment machinery underneath the small app API:

1. ensure the source session exists;
2. run optional app-semantic preparation, when needed;
3. ask the session to quiesce every platform service belonging to the source app;
4. abort without reclaiming memory if semantic preparation or service teardown cannot complete safely;
5. commit app-visible exit cleanup;
6. bulk-reclaim all source-app managed memory;
7. verify zero source allocations remain; and
8. create the destination session before the destination app begins using app resources.

There is no separate application reachability phase anymore. A successful session close *is* the platform reachability proof. This keeps the safety invariant in one place and avoids parallel per-app stop/restart state in `app_manager`.

If a transition aborts, `app_session_abort_close()` restores the original platform-service set according to each service's failure semantics. Services that were active and successfully stopped are restarted. A service whose stop call fails may either have already self-restored its active state or use a platform-internal failed-stop recovery callback; the session records which case applies so it does not double-start self-restoring workers.

App-specific rollback is reserved for semantic state the platform cannot infer. Recorder is the current example: the manager snapshots the recording/monitor/playback operation before transition, while the session owns worker/channel safety. If the transition aborts, Recorder semantic rollback restores the user's prior operation after the platform has restored a safe service state.

## Memory policy

App-session PSRAM is for retained observations, histories, captures, and other meaningfully sized growable state. Small transient objects, task stacks, DMA buffers, driver-required internal allocations, and hardware-constrained buffers continue to use the ESP-IDF memory class required by their platform service.

There is deliberately no arbitrary per-app item quota. Collections grow according to available resources and their retention semantics. UI code should bound presentation work, not retained data. When memory pressure occurs, prefer correctness/current data, release reconstructable scratch, evict stale/low-value history where semantically safe, surface pressure, and continue in a bounded state.

PSRAM is a supported-platform requirement for this model. RFScout does not silently convert internal SRAM into a reduced-capacity substitute for large app-session storage.

## WiFi/BLE migration

WiFi/BLE is the first data-heavy app moved to session memory. The normalized observation store no longer reserves 1000 entries at boot and no longer has a 1000-item retention ceiling. It grows geometrically in app-session PSRAM.

The summary UI no longer keeps another fixed 1000-entry observation/brand mirror. It derives exact aggregate counts from the live store and only stores the rows that physically fit on the display. Summary scratch is also session-owned.

The store uses the app-session generation to invalidate cached pointers after a prior WiFi/BLE session has been reclaimed. It refreshes that generation after any allocation that may lazily bind the active session, so a restored startup app cannot cause a newly allocated collection to be mistaken for stale state. It does not inspect lower-level allocator ownership metadata.

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
