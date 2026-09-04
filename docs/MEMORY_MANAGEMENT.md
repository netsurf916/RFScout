# Memory management

RFScout uses explicit lifetime ownership for large dynamic allocations that are eligible for external PSRAM. The implementation lives in `memory_manager.c/.h` and is intentionally separate from ordinary ESP-IDF allocations that must remain in internal or DMA-capable memory.

## Ownership model

Managed allocations have one of three lifetimes:

- **Platform/service** allocations live independently of the selected application and are not reclaimed by application exit.
- **Application** allocations belong to one `rfscout_app_id_t` and are eligible for bulk reclamation after that application has safely quiesced.
- **Operation** allocations are nested beneath an application and may be reclaimed at an earlier mode/operation boundary. Application-wide reclamation also includes these nested allocations.

The manager does not infer ownership from call sites. Callers pass an explicit owner to allocation, resize, and free operations. A mismatched owner is rejected and logged rather than freeing memory belonging to another lifetime.

## Allocation policy

`memory_manager_calloc()` and `memory_manager_resize()` request `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`; managed application storage does not silently fall back to internal SRAM. `memory_manager_init()` reports an unsupported-target error when no usable PSRAM heap exists.

The manager is intended for retained observations, histories, captures, and other meaningfully sized growable state. Small transient objects, task stacks, DMA buffers, driver-required internal allocations, and other hardware-constrained memory should continue to use the appropriate ESP-IDF allocation path instead of paying managed-allocation metadata overhead.

Resize operations check multiplication overflow before calculating the requested byte capacity. A failed resize preserves the original allocation and its contents. Successful growth zero-initializes the newly added tail so callers do not accidentally consume uninitialized retained state.

## Accounting and reclamation

For each application, the manager tracks:

- current managed payload bytes;
- peak managed payload bytes;
- current managed allocation count;
- allocation-failure count.

`memory_manager_release_operation()` reclaims one nested operation. `memory_manager_release_app()` reclaims every application and operation allocation associated with the selected app and returns the number of payload bytes released. `memory_manager_app_allocation_count()` provides the invariant used by lifecycle code to verify that successful app exit leaves no managed allocations behind.

Metadata for managed allocations is intentionally small and kept in internal memory so the ownership list remains available while PSRAM payloads are being reclaimed. `memory_manager_pointer_owned_by()` lets a module compare a cached pointer value against that metadata without dereferencing the pointer. This is used when lifecycle bulk reclamation owns the physical free but a long-lived service object must lazily invalidate its cached app-owned pointer before the next session.

## Lifecycle boundary

The memory manager provides reclamation mechanics; `app_lifecycle.c` owns the application-exit reclamation boundary. Application workers, callbacks, timers, DMA completion paths, and peripherals first pass the transactional Prepare and Reachability stages described in [APP_LIFECYCLE.md](APP_LIFECYCLE.md). Only after that final fallible safety gate does the coordinator run irreversible app cleanup and call `memory_manager_release_app()`.

After reclamation, the coordinator verifies `memory_manager_app_allocation_count()` is zero. This postcondition checks allocator/accounting state only; it is not a substitute for proving reachability before memory is freed. A non-zero count is recorded as a committed memory-invariant diagnostic because rollback is unsafe once source allocations may already have been released.

An aborted Prepare or Reachability stage never bulk-reclaims the source application's managed memory. The source remains selected and its resources are reactivated as necessary.

The WiFi/BLE normalized observation store is the first retained application dataset migrated onto this boundary. It allocates no observation payload at boot, grows geometrically from a small initial capacity while the app is collecting, and is reclaimed by the normal WiFi/BLE lifecycle transition after both collectors have stopped. The observation-store synchronization object remains platform-resident so the next WiFi/BLE session can safely detect reclamation and allocate a fresh app-owned collection.

Other application storage is not automatically migrated merely because lifecycle reclamation is integrated. CC1101, nRF24, Recorder, Thermal, WiFi/BLE surveillance history, and future WiFi/BLE identity/relationship state move eligible retained state to managed PSRAM in subsequent v0.4.0 work, with their own memory-pressure semantics and quiesce validation.

## Supported-target requirement

Managed storage reinforces RFScout's platform requirement for usable external PSRAM. `app_lifecycle_init()` initializes the managed allocator during application-manager startup and propagates an unsupported-target error rather than silently substituting internal SRAM. A future board port must provide a working SPIRAM heap; internal SRAM is not a reduced-capacity fallback for managed application history. See [BOARD_PORTING.md](BOARD_PORTING.md) for the board-level requirement.
