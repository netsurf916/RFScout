# Application lifecycle

RFScout treats application navigation and peripheral ownership as separate phases. This keeps rapid browsing responsive without repeatedly starting and stopping radios, sensors, or audio hardware.

## Lifecycle states

An application transition has four conceptual phases:

1. **Enter** — the destination becomes the active visible application and prepares only inexpensive UI/runtime state.
2. **Settle** — hardware activation is deferred for a short monotonic-time interval. Navigation remains available and leaving during this interval cancels activation.
3. **Activate** — after the settle interval, the still-active application may start workers, scanning, DMA, continuous sensor acquisition, or other peripheral activity.
4. **Quiesce** — before ownership moves to another application, active workers stop and shared hardware is left idle. Quiesce is bounded but synchronous from the application manager's point of view.

The settle interval is not a blocking delay. It is evaluated from the normal application-manager poll path using monotonic time. Home and other applications that do not need delayed peripheral activation do not pay an artificial delay.

A meaningful action inside an application may activate it immediately when the action requires that application's hardware. Merely rotating through applications does not.

Application availability is owned by the application framework rather than hard-coded in navigation. Each application descriptor declares one of three probe policies: `none` for applications with no runtime hardware gate, `startup` for fixed/on-board hardware whose result is cached for the boot, or `hotplug` for attachable resources that may be re-probed before entry and periodically while inactive. Detection mechanics stay with the relevant service/driver; the framework only invokes the callback and retains its semantic availability state. CC1101, nRF24, and the board PDM microphone use startup-cached probing. Thermal uses hot-plug probing because it is an explicitly attachable I2C peripheral. Future UART/I2C Quick-port applications should normally use the same hot-plug policy. Probing is distinct from activation: a successful probe may prepare reusable driver state, but it must not start scanning, continuous acquisition, recording, or another app-owned workload.

## Transactional exit boundary

Application exit is transactional until the final reachability gate passes. The reusable coordinator in `app_lifecycle.c/.h` separates source-app exit into four stages:

1. **Prepare** requests bounded worker/peripheral quiescence. Operations here must be reversible or have an explicit source-app reactivation path.
2. **Reachability** verifies that no source worker, callback, timer, DMA completion path, or other execution context can still access app-owned dynamic state.
3. **Commit** performs irreversible app-specific cleanup only after the final fallible safety gate succeeds.
4. **Reclaim** releases all remaining managed application/operation allocations and verifies that the exited app owns zero managed allocations.

A Prepare or Reachability failure aborts the transition before reclamation. The source application remains selected and its lifecycle hook reactivates any resource that was successfully stopped during the attempted transition. Managed app memory remains intact. The destination is not entered.

Once Commit begins, rollback is no longer valid. After Commit, `app_lifecycle_transition()` owns `memory_manager_release_app()` and records the zero-allocation postcondition. A post-reclamation accounting failure is reported as a committed memory-invariant diagnostic; the destination still has to be entered because source memory may already have been released.

The coordinator retains the most recent transition diagnostics: source and destination IDs, monotonically increasing sequence, outcome/error, reclaimed managed bytes, remaining managed allocation count, and elapsed time. Transition logs also include current free and largest-block PSRAM values for lifecycle/fragmentation testing.

## Peripheral ownership contract

When application exit/quiesce returns, the application must satisfy all of the following:

- no worker is still issuing transactions to a shared peripheral bus;
- no timer or callback can mutate application runtime state that has been reset for the next entry;
- DMA/I2S acquisition owned by the application has stopped or reached a documented safe state;
- chip-select, enable, RF-path, and similar control signals are left in their inactive state;
- pending display data may remain cached, but it must not imply that acquisition is still running.

Long-lived platform services may remain initialized. Initialization and activation are deliberately different concepts: creating a reusable driver/service once is preferable to repeated allocation, while active acquisition belongs to the application lifecycle.

If bounded quiesce fails, the application transition is aborted. RFScout must retain ownership with the current application rather than allow the next application to begin using a resource that may still be active. A stop implementation that has already withdrawn its run request must roll that request back on timeout so the retained application remains operational rather than becoming a selected-but-inactive view. Failed quiesce must also preserve the retained application's runtime counters, detection history, and menu state; exit cleanup occurs only after quiescence succeeds. A rollback that resumes a persistent worker must explicitly wake that worker so a race with its active-to-waiting transition cannot leave the retained app dormant. The notification may be delivered while the worker is still unwinding; FreeRTOS retains it until the worker reaches its next notification wait, which closes that race without starting a second worker.

## Shared SPI radios

CC1101 and nRF24 use persistent worker tasks. Their stop paths must therefore wait for the worker to observe the stop request and leave its active sweep before the application manager starts another shared-SPI peripheral. A stop flag alone is not a quiesce guarantee. If the bounded wait expires while the worker is still active, the radio restores its `running` state and reports failure so the manager can abort the transition without freezing acquisition in the retained app.

Wi-Fi and BLE likewise remain initialized as platform services while their active scanning/monitoring is controlled by application activation. A partially completed Wi-Fi/BLE stop attempt must restore whichever collectors were active before the transition if the transaction aborts.

## Recorder

Entering Recorder should not open the microphone. `recorder_init()` is intentionally allowed at entry because it only prepares reusable synchronization state and refreshes the recording catalog; I2S microphone ownership begins only for recording or monitor actions. Leaving Recorder must synchronously stop recording/monitor/playback activity according to the existing bounded shutdown rules before the reachability gate can pass.

## UI interaction policy

Menu behavior should follow the semantic type of an item rather than the menu that contains it:

- **Action** — center executes immediately.
- **Toggle** — center changes On/Off immediately.
- **Choice** — center cycles a small finite set when direct cycling is unambiguous.
- **Value** — center enters edit mode, rotation adjusts, center exits.
- **Submenu** — center enters the child view.

Simple On/Off settings should not require center, rotate, center. Rotation is reserved for navigation unless a value genuinely benefits from edit mode.

## Theme contract

Application chrome and ordinary application content use semantic colors from `ui_theme`. Raw colors are reserved for literal data imagery such as thermal pixels. Discipline intentionally uses the Night visual palette; therefore any visual difference between Night and Discipline outside an explicit policy/status indicator is a presentation bug. Their intended difference is environmental sound policy.

## Stability diagnostics

Lifecycle work should be tested by rapidly rotating through all enabled applications for an extended period. Transition diagnostics include the source/destination application, transition outcome, reclaimed managed bytes, remaining managed allocations, elapsed time, free PSRAM, and largest free PSRAM block. Existing activation diagnostics continue to expose general heap information.

The v0.4.0 release validation must still exercise failed quiesce/reachability paths, rapid Home/application cycles, low-memory conditions, and callback/timer races on hardware. A falling largest-free-block value with otherwise stable total free heap suggests fragmentation; stable heap with failures points more strongly toward lifecycle races or peripheral ownership violations. Successful app exit must leave zero managed allocations for the exited app, while an aborted transition must preserve the source app and its managed state.
