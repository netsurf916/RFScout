# Application lifecycle and resources

RFScout treats an application as a temporary **resource session**. The active app may use the platform's available application PSRAM and platform services; closing the app stops those services and releases the entire app-owned memory set. App implementations should not need to understand allocator ownership tables, callback reachability counters, or a multi-stage reclamation protocol.

## Simple application contract

The application-facing model is intentionally small:

1. **Enter** — prepare inexpensive UI/runtime state.
2. **Settle** — optionally defer hardware activation briefly so rapid navigation stays responsive.
3. **Activate** — use platform services and allocate app-session memory as needed.
4. **Close** — perform only semantic cleanup that the app itself understands; the platform closes registered services and reclaims app-session memory.

Platform services are responsible for their own safe-close contract. A successful service close means it admits no new callbacks/work and all already-admitted execution contexts that could reach app-owned memory have drained. Applications therefore do not implement callback counters or bespoke `reachability_clear()` logic for platform services.

The resource-session layer in `app_session.c/.h` sits above the owner-aware allocator. Application/session allocation helpers automatically attach memory to the current app. The lower-level `memory_manager.c/.h` retains explicit ownership/accounting internally, but ordinary application code should prefer the session abstraction rather than passing app IDs through allocation call sites.

This keeps future applications easy to add while preserving the core resource-isolation goal: inactive apps do not reserve large chunks of PSRAM merely because they might need them later.

## Platform-managed close

`app_lifecycle.c` remains transactional internally, but those mechanics are a platform concern rather than an application API design goal.

For a transition, the platform:

1. ensures a resource session exists for the source app;
2. asks the session to close every registered platform service;
3. aborts without reclaiming memory if any service cannot prove safe closure;
4. runs any app-specific semantic Prepare hook that is still required, such as finishing Recorder storage work;
5. verifies the session and any legacy/specialized app reachability condition are safe;
6. performs irreversible app-specific cleanup;
7. releases the entire app-owned allocation set;
8. verifies zero managed allocations remain; and
9. enters the destination app.

If platform-service close fails, the session reactivates services touched by the failed close attempt on a best-effort basis and preserves all app memory. A later close retries the actual service close contract; it must not infer safety from an application-manager `ready` flag or from the fact that a previous stop was attempted.

The existing Prepare/Reachability/Commit/Reclaim coordinator is retained underneath this abstraction because it provides useful fault containment and diagnostics. New applications should not have to reproduce those phases themselves.

## Memory policy

Large or growable retained observations, histories, captures, and processing buffers should use app-session PSRAM. Small transient objects, task stacks, DMA buffers, driver-required internal allocations, and other hardware-constrained memory continue to use the ESP-IDF memory class required by the platform service.

Managed allocation growth is overflow-checked and preserves the original allocation on failure. Application exit bulk-reclaims all remaining managed allocations. Per-app accounting retains current bytes, peak bytes, allocation count, and allocation failures for diagnostics.

There is deliberately no arbitrary per-application item quota. Collections may grow according to available resources and their semantic retention policy. UI code should bound its own presentation/scratch workload rather than imposing its display capacity on retained data. When memory pressure occurs, prefer preserving correctness/current data, releasing reconstructable scratch, evicting stale or low-value history where semantics permit, surfacing pressure, and continuing in a bounded state.

PSRAM is a supported-platform requirement for this model. RFScout does not silently turn internal SRAM into a reduced-capacity substitute for large app-session storage.

## WiFi/BLE first migration

WiFi/BLE is the first data-heavy app using this model. Its normalized observation collection no longer reserves 1000 entries at boot and no longer has a 1000-item retention ceiling. It grows geometrically in app-owned PSRAM. The summary UI asks for aggregate results/top visible rows instead of maintaining a second fixed-capacity mirror of the retained collection.

Wi-Fi and BLE are registered platform services for the WiFi/BLE resource session. Their stop operations withdraw observation admission and wait for already-admitted callbacks to drain before reporting success. The lifecycle coordinator can therefore reclaim WiFi/BLE app memory based on the service/session contract rather than requiring the WiFi/BLE app to understand callback reachability.

The observation store's mutex and other genuinely platform-resident synchronization state may remain initialized across app sessions. Retained observations and growable summary scratch are app-owned and reclaimed on successful exit.

Other data-heavy applications will migrate eligible storage to the same session abstraction during v0.4.0. DMA/internal-memory constraints remain owned by their platform services rather than being forced into PSRAM.

## Availability, activation, and dependency loss

Application availability is owned by the application framework. Descriptors use `none`, `startup`, or `hotplug` probe policies. Probing is distinct from activation: a successful probe may prepare reusable platform state but must not start continuous app-owned acquisition.

A user navigation step may skip unavailable apps. If an active app loses a required dependency, RFScout returns transactionally to Home rather than cascading through other apps. The same resource-session close rules apply; if safe close cannot complete, RFScout preserves the source app and its memory instead of reclaiming potentially reachable state.

## Shared SPI radios

CC1101 and nRF24 currently use persistent worker tasks. Their stop paths synchronously wait for the worker to leave active acquisition before shared-SPI ownership can move. These are candidates to move behind the same registered platform-service contract so future app code does not need radio-specific lifecycle knowledge.

## Recorder

Recorder has semantic close requirements beyond ordinary memory safety because recordings and SD-backed work may need finalization. Its app-specific Prepare/reactivation logic therefore remains appropriate for now. This is intentionally separate from allocator ownership: the app knows that a recording must be finalized; the platform should ultimately own microphone/output worker reachability and app-session memory reclamation.

Recorder availability requires both the fixed board microphone and healthy SD storage. Physical SD loss does not tear down the shared VFS mount while other platform services may own files. Runtime Recorder memory remains subject to the normal app-session reclamation boundary after safe close.

## UI interaction policy

Menu behavior follows semantic item type:

- **Action** — center executes immediately.
- **Toggle** — center changes On/Off immediately.
- **Choice** — center cycles a small finite set when direct cycling is unambiguous.
- **Value** — center enters edit mode, rotation adjusts, center exits.
- **Submenu** — center enters the child view.

Simple On/Off settings should not require center, rotate, center. Rotation is reserved for navigation unless a value genuinely benefits from edit mode.

## Stability diagnostics

Lifecycle testing should rapidly rotate through all enabled applications and exercise low-memory, callback races, dependency loss, and forced close failures. Transition diagnostics include source/destination app, outcome, reclaimed bytes, remaining allocations, elapsed time, free PSRAM, and largest free PSRAM block.

Successful app exit must leave zero managed allocations for the exited app. An aborted transition must preserve app-owned memory. Release validation should also verify that baseline free memory and largest-free-block behavior do not decline monotonically across repeated app cycles.
