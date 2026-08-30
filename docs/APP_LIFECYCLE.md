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

Application availability is resolved before this lifecycle begins. Fixed optional hardware such as CC1101, nRF24, and the board PDM microphone is probed once during startup; absent hardware removes its application from navigation for that boot. The resulting fixed-hardware availability is cached for the lifetime of the boot and is not re-probed during ordinary app navigation. Radio probes may initialize reusable driver state without starting scans. The microphone probe briefly clocks the PDM interface, verifies data-line activity, and releases I2S0 immediately; it does not start recording or monitoring. Because PDM has no device-identity transaction, microphone presence is an electrical activity test rather than an identity check and should be validated for each board implementation. Thermal remains dynamically probed because it is an explicitly attachable external peripheral.

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

Wi-Fi and BLE likewise remain initialized as platform services while their active scanning/monitoring is controlled by application activation.

## Recorder

Entering Recorder should not open the microphone. `recorder_init()` is intentionally allowed at entry because it only prepares reusable synchronization state and refreshes the recording catalog; I2S microphone ownership begins only for recording or monitor actions. Leaving Recorder must synchronously stop recording/monitor/playback activity according to the existing bounded shutdown rules.

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

Lifecycle work should be tested by rapidly rotating through all enabled applications for an extended period. Transition diagnostics should include the source/destination application, activation/quiesce duration, free heap, minimum-ever free heap, and largest free block where practical. A falling largest-free-block value with otherwise stable total free heap suggests fragmentation; stable heap with failures points more strongly toward lifecycle races or peripheral ownership violations.
